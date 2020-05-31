/********************************************************************
	Lost buzzer firmware
	
	Target STM8S003: 8kB FLASH, 1kB RAM, 128B EEPROM
	AFR0 and AFR7 must be set for TIM1 PWM outputs at PortC
********************************************************************/

#include "global_def.h"
#include "buzzer.h"
#include "buttons.h"



//=================================================================//
// Data types and definitions

// Maximum value for timers used in FSM
#define MAX_TMR                 65535



//=================================================================//
// Data

static struct {

    // Alarm for power battery monitor
    struct {
        uint8_t doLowBatBeep;
    } powBat;

    // Alarm for direct control (PWM or level)
    struct {
        uint8_t dirCtrlState;
        uint8_t startContBeep;
        uint8_t stopContBeep;
        uint8_t startTimeoutBeep;
    } directControl;
} alarms;



static struct {
    uint16_t tick;
    uint16_t state;
    uint16_t evt;
    uint16_t dly;
} timers;


volatile uint8_t sysFlag_TmrTick;
static bState_t state;
static uint8_t buzzerVolume = VolumeLow;

// Global structure for storing settings
config_t cfg;


//=================================================================//
// AWU management (used as timebase for FSM)
// AWU is active only when CPU is executing power-saving instructions, WFI or HALT
// The cycle time will depend on the time CPU works in active mode
// So when used as system timer, it is not as accurate as normal timer

#define AWU_1MS     ((3 << 8) | (32 - 2))
#define AWU_10MS    ((6 << 8) | (40 - 2))
#define AWU_100MS   ((9 << 8) | (50 - 2))


void setAwuPeriod(uint16_t period)
{
    // Set the TimeBase
    AWU->TBR &= (uint8_t)(~AWU_TBR_AWUTB);
    AWU->TBR |= (uint8_t)(period >> 8);

    // Set the APR divider
    AWU->APR &= (uint8_t)(~AWU_APR_APR);
    AWU->APR |= (uint8_t)(period & 0x3F);
}

void startAwu(uint16_t period)
{
	AWU->CSR |= AWU_CSR_AWUEN;
	setAwuPeriod(period);
}

void stopAwu(void)
{
    AWU->CSR &= (uint8_t)(~AWU_CSR_AWUEN);
    AWU->TBR = (uint8_t)(~AWU_TBR_AWUTB);
}


//=================================================================//
// GPIO management

ledCtrl_t ledCtrl[] = {
    {.GPIO = GPIOA, .pin = GPA_LED1_PIN},
    {.GPIO = GPIOA, .pin = GPA_LED2_PIN},
    {.GPIO = GPIOA, .pin = GPA_LED3_PIN}
};

struct {
    uint8_t led1;
    uint8_t led2;
    uint8_t led3;
} volumeLedIndication[VolumeCount] = {
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, 1},
    {0, 1, 1}
};


// Fast LED set/clear macros
#define SET_LED(led, state) {(state) ? GPIO_WriteLow(ledCtrl[led].GPIO, ledCtrl[led].pin) : \
                                       GPIO_WriteHigh(ledCtrl[led].GPIO, ledCtrl[led].pin);}


void initGpio(void)
{
    uint8_t i;
    
    // LEDs
    for (i=0; i<sizeof(ledCtrl) / sizeof(ledCtrl_t); i++)
    {
        GPIO_Init(ledCtrl[i].GPIO, (GPIO_Pin_TypeDef)ledCtrl[i].pin, GPIO_MODE_OUT_PP_HIGH_SLOW);
        GPIO_WriteHigh(ledCtrl[i].GPIO, ledCtrl[i].pin);
    }

    // Button, VCC_SEN
    GPIO_Init(GPIOB, (GPIO_Pin_TypeDef)(GPB_BTN_PIN | GPB_VCCSEN_PIN), GPIO_MODE_IN_FL_NO_IT);

    // BTN and VCC share PortB, this is common interrupt sensivity setting
    EXTI_SetExtIntSensitivity(EXTI_PORT_GPIOB, EXTI_SENSITIVITY_RISE_FALL);

    // SIG
    GPIO_Init(GPIOC, GPC_SIG_PIN, GPIO_MODE_IN_FL_NO_IT);

    // UART
    GPIO_Init(GPIOD, GPD_UART_PIN, GPIO_MODE_IN_FL_NO_IT);

    // VBAT, VREF
    //GPIO_Init(GPIOD, (GPIO_Pin_TypeDef)(GPD_VBAT_PIN | GPD_VREF_PIN), GPIO_MODE_IN_FL_NO_IT);
    GPIO_Init(GPIOD, (GPIO_Pin_TypeDef)(GPD_VBAT_PIN | GPD_VREF_PIN), GPIO_MODE_OUT_PP_LOW_FAST);       // TODO - replace with analog input

    // VREF_SUPP
    GPIO_Init(GPIOD, GPD_VREF_SUPP_PIN, GPIO_MODE_OUT_PP_LOW_FAST);

    // PWM outputs
    GPIO_Init(GPIOC, (GPIO_Pin_TypeDef)(GPIO_PIN_3 | GPIO_PIN_4), GPIO_MODE_OUT_PP_HIGH_FAST);
    GPIO_Init(GPIOC, (GPIO_Pin_TypeDef)(GPIO_PIN_6 | GPIO_PIN_7), GPIO_MODE_OUT_PP_LOW_FAST);

    // Unused pins to prevent floating
    GPIO_Init(GPIOD, (GPIO_Pin_TypeDef)GPD_RESERVED_PINS, GPIO_MODE_IN_PU_NO_IT);
}




// External function for button processor
btn_type_t GetRawButtonState(void)
{
    uint8_t pinState = (GPIOB->IDR & GPB_BTN_PIN);
    return (pinState) ? 0 : BTN;
}


uint8_t isMainSupplyPresent(void)
{
    uint8_t pinState = (GPIOB->IDR & GPB_VCCSEN_PIN);
    return pinState;
}


uint8_t isDirectControlInputActive(void)
{
    uint8_t pinState = (GPIOC->IDR & GPC_SIG_PIN);
    return (cfg.io.directControlActiveHigh) ? pinState : !pinState;
}



//=================================================================//
// FSM and main loop

void smallDelay(uint8_t n)
{
    volatile uint8_t i;
    for (i=0; i<n; i++);
}

// Switch state of the FSM
// PWM outputs and LEDs are disabled
void swState(bState_t newState)
{
    state = newState;
    timers.tick = 0;
    timers.state = 0;
    timers.dly = 0;
    timers.evt = 0;
    
    buttons.action_down = 0;
    buttons.action_up = 0;
    buttons.action_hold = 0;

    alarms.directControl.dirCtrlState = 0;
    
    Buzz_Stop();
    SET_LED(Led1, 0);
    SET_LED(Led2, 0);
    SET_LED(Led3, 0);
}


void incTimer(uint16_t *tmr)
{
    if (*tmr < MAX_TMR)
        (*tmr)++;
}


void testAdc(void)
{
    // FIXME - test only
    // Measure voltage on VREF pin
    ADC1_Cmd(ENABLE);
    ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, adcChVref, ADC1_ALIGN_RIGHT);
    ADC1_StartConversion();
    while (ADC1_GetFlagStatus(ADC1_FLAG_EOC) == RESET);
    volatile uint16_t adcVref = ADC1_GetConversionValue();

    // Measure voltage on VREF pin
    ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, adcChVbat, ADC1_ALIGN_RIGHT);
    ADC1_StartConversion();
    while (ADC1_GetFlagStatus(ADC1_FLAG_EOC) == RESET);
    volatile uint16_t adcVbat = ADC1_GetConversionValue();

    // Capture input parameters (volume level / mute / etc)
    // Measure voltage level on BTN pin. Depending on it, mute or disable buzzer
    ADC1_Cmd(ENABLE);
    ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, adcChBtn, ADC1_ALIGN_RIGHT);
    ADC1_StartConversion();
    while (ADC1_GetFlagStatus(ADC1_FLAG_EOC) == RESET);
    uint16_t adcBtn = ADC1_GetConversionValue();
}


/*
 // FIXME - test only
                        // Measure voltage on VREF pin
                        ADC1_Cmd(ENABLE);
                        ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, adcChVref, ADC1_ALIGN_RIGHT);
                        ADC1_StartConversion();
                        while (ADC1_GetFlagStatus(ADC1_FLAG_EOC) == RESET);
                        volatile uint16_t adcVref = ADC1_GetConversionValue();

                        // Measure voltage on VREF pin
                        ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, adcChVbat, ADC1_ALIGN_RIGHT);
                        ADC1_StartConversion();
                        while (ADC1_GetFlagStatus(ADC1_FLAG_EOC) == RESET);
                        volatile uint16_t adcVbat = ADC1_GetConversionValue();

                        // Capture input parameters (volume level / mute / etc)
                        // Measure voltage level on BTN pin. Depending on it, mute or disable buzzer
                        ADC1_Cmd(ENABLE);
                        ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, adcChBtn, ADC1_ALIGN_RIGHT);
                        ADC1_StartConversion();
                        while (ADC1_GetFlagStatus(ADC1_FLAG_EOC) == RESET);
                        uint16_t adcBtn = ADC1_GetConversionValue();

                        // Select battery channel to enable digital function on BTN pin
                        ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, adcChVbat, ADC1_ALIGN_RIGHT);
*/




/*
 TODO:
    - PWM dead time (mute level), frequency
    + Buzzer signal queue
    - UART RX/TX, autobaud
    + PWM input capture
    - VREF ADC check
    - VBAT ADC check
    - SWIM pull-up
    - EEPROM CFG
*/



int main()
{
    uint8_t tmp8u, i;
    uint8_t exit;
    
    CLK_SYSCLKConfig(CLK_PRESCALER_HSIDIV8);    // Fmaster = 2MHz
    CLK_SYSCLKConfig(CLK_PRESCALER_CPUDIV2);    // Fcpu = 1MHz
    
    initGpio();
    Buzz_Init(buzzerVolume);
    
    // GPIO PWM test for buzzer
    //testPwm();
      
    // Enable LSI
    CLK_LSICmd(ENABLE);

    // Setup ADC
    ADC1_PrescalerConfig(ADC1_PRESSEL_FCPU_D4);

    // AWU setup for WAKEUP timebase
    startAwu(AWU_10MS);
    
    // Use Active-halt with main voltage regulator (MVR) powered off 
    // (increased startup time of ~50us is acceptable)
    // Do not used fast clock wakeup since HSI is always used
    //CLK_SlowActiveHaltWakeUpCmd(ENABLE);
    //CLK_SlowActiveHaltWakeUpCmd(DISABLE);
    
    // Init FSM
    state = ST_WAKEUP;
    sysFlag_TmrTick = 0;
    timers.state = 0;
    
    // Start
    enableInterrupts();  
        
    while(1)
    {
        // Wait for interrupt from AWU
        
        // FIXME
        //SET_LED(Led3, 0);
        
        while (!sysFlag_TmrTick)
        {
            if ((state == ST_RUN)/* || (state == ST_WAKEUP)*/)
            {
                // Peripherals must be enabled for this states
                asm("WFI");
            }
            else
            {
                // Pre-alarm or alarm, backup battery supply, stay halted when timer is disabled
                if (Buzz_IsActive())
                    asm("WFI");
                else
                    asm("HALT");    // Active halt - AFU is enabled
            }
        }
        sysFlag_TmrTick = 0;
        
        // FIXME
        //SET_LED(Led3, 1);
        
        // Check BTN state
        ProcessButtons();
        
        // Process state timer (common for all states)
        incTimer(&timers.state);
        
        // Process FSM controller
        exit = 0;
        while (!exit)
        {
            exit = 1;       // Run FSM once by default
            switch (state)
            {
                case ST_WAKEUP:                         // [10ms]
                    // Small delay for debounce
                    if (buttons.raw_state & BTN)
                    {
                        swState(ST_NOSUPPLY);
                        // Show current buzze level
                        SET_LED(Led1, volumeLedIndication[buzzerVolume].led1);
                        SET_LED(Led2, volumeLedIndication[buzzerVolume].led2);
                        SET_LED(Led3, volumeLedIndication[buzzerVolume].led3);
                    }
                    else if (isMainSupplyPresent())
                    {
                        // Wait for supply to stabilize
                        swState(ST_WAITSUPPLY);
                        SET_LED(Led1, 1);
                        SET_LED(Led2, 1);
                        SET_LED(Led3, 1);
                    }
                    else
                    {
                        swState(ST_SLEEP);
                        exit = 0;
                    }
                    break;
                    
                case ST_NOSUPPLY:
                    if (timers.state >= 100)
                    {
                        // Beep at selected level
                        Buzz_PutTone(Tone1, 100);
                        swState(ST_NOSUPPLY_EXIT);
                        exit = 0;
                    }
                    else if (buttons.action_down & BTN)
                    {
                        buzzerVolume = (buzzerVolume < (VolumeCount - 1)) ? buzzerVolume + 1 : 0;
                        Buzz_SetVolume(buzzerVolume);
                        SET_LED(Led1, volumeLedIndication[buzzerVolume].led1);
                        SET_LED(Led2, volumeLedIndication[buzzerVolume].led2);
                        SET_LED(Led3, volumeLedIndication[buzzerVolume].led3);
                        timers.state = 0;
                    }
                    break;

                case ST_NOSUPPLY_EXIT:
                    Buzz_Process();
                    if (!Buzz_IsActive())
                    {
                        swState(ST_SLEEP);
                        exit = 0;
                    }
                    break;
                
                case ST_WAITSUPPLY:                        // [10ms]      
                    // Provide some delay to ensure supply voltage is OK
                    if (timers.state >= 10)
                    {
                        if (isMainSupplyPresent())
                        {
                            // Reset data for RUN state
                            alarms.directControl.dirCtrlState = 0;
                            // add more here ...
                        
                            // TODO: Detect cell count for power battery
                            // TODO: Enable other peripherals
                            
                            //for (i=0; i<2; i++)
                            {
                                //Buzz_PutTone(Tone4, 100);
                                //Buzz_PutTone(Tone1, 100);
                                //Buzz_PutTone(ToneSilence, 100);
                            }
                            swState(ST_RUN);
                            setAwuPeriod(AWU_1MS);
                            exit = 0;
                        }
                        else
                        {
                            swState(ST_SLEEP);
                            exit = 0;
                        }
                    }
                    break;
                
                case ST_RUN:                            // [1ms]
                    if (isMainSupplyPresent())
                    {                      
                        // Process buzzer controller
                        if (++timers.tick >= 10)
                        {
                            timers.tick = 0;
                            Buzz_Process();
                        }

                        if (buttons.action_down & BTN)
                        {
                            swState(ST_RUN_SETUP_VOLUME);
                            setAwuPeriod(AWU_10MS);
                            // Show current buzzer level
                            SET_LED(Led1, volumeLedIndication[buzzerVolume].led1);
                            SET_LED(Led2, volumeLedIndication[buzzerVolume].led2);
                            SET_LED(Led3, volumeLedIndication[buzzerVolume].led3);
                        }
                    
                        // Process direct buzzer control (level / pwm)
                        if (1)  // __cfg.dirCtrl == __LEVEL__
                        {
                            tmp8u = isDirectControlInputActive();
                            // Detect changes
                            if (tmp8u != alarms.directControl.dirCtrlState)
                            {
                                alarms.directControl.dirCtrlState = tmp8u;
                                alarms.directControl.startContBeep = alarms.directControl.dirCtrlState;
                                alarms.directControl.stopContBeep = !alarms.directControl.dirCtrlState;
                            }
                        }
                        else
                        {
                            // TODO
                        }
                        
                        // Process power battery alarm
                        {
                            // TODO
                        }
                        
                        
                        //-----------------------------------//
                        
                        // Apply direct control alarm state
                        if (alarms.directControl.startContBeep)
                            Buzz_BeepContinuous(Tone1);
                        else if (alarms.directControl.stopContBeep)
                            Buzz_Stop();
                        
                        // Apply other alarms
                        //if (alarms.powBat.doLowBatBeep && !Buzz_IsActive())
                        //    Buzz_StartMs(200);      // TODO
                        //if (alarms.directControl.startTimeoutBeep && !Buzz_IsActive())
                        //    Buzz_StartMs(100);      // TODO
                    }
                    else
                    {
#if 0
                        // Main supply is not OK
                        if (timers.state < 3000)
                        {
                            // __disable_uart();        // And other peripherals - TODO
                            swState(ST_SLEEP);
                            exit = 0;
                        }
                        else
#endif
                        {
                            // __disable_uart();        // And other peripherals - TODO
                            swState(ST_PREALARM);
                            setAwuPeriod(AWU_10MS);
                        }
                    }
                    break;

                case ST_RUN_SETUP_VOLUME:
                    if (timers.state >= 100)
                    {
                        // Beep at selected level
                        Buzz_PutTone(Tone1, 100);
                        swState(ST_RUN_SETUP_VOLUME_EXIT);
                        exit = 0;
                    }
                    if (buttons.action_down & BTN)
                    {
                        buzzerVolume = (buzzerVolume < (VolumeCount - 1)) ? buzzerVolume + 1 : 0;
                        Buzz_SetVolume(buzzerVolume);
                        SET_LED(Led1, volumeLedIndication[buzzerVolume].led1);
                        SET_LED(Led2, volumeLedIndication[buzzerVolume].led2);
                        SET_LED(Led3, volumeLedIndication[buzzerVolume].led3);
                        timers.state = 0;
                    }
                    break;

                case ST_RUN_SETUP_VOLUME_EXIT:
                    Buzz_Process();
                    if (!Buzz_IsActive())
                    {
                        swState(ST_RUN);
                        setAwuPeriod(AWU_1MS);
                    }
                    break;
                    
                case ST_PREALARM:                       // [10ms]
                    if (isMainSupplyPresent())
                    {
                        swState(ST_WAKEUP);
                    }
                    else
                    {
                        // Process buzzer controller
                        Buzz_Process();
                        
                        if (buttons.action_down & BTN)
                        {
                            // User wants to disable buzzer
                            swState(ST_SLEEP);
                            exit = 0;
                        }
                        if (++timers.dly >= 100)
                        {
                            timers.dly = 0;
                            if (++timers.evt == 5)
                            {
                                swState(ST_ALARM);
                            }
                            else
                            {
                                // Beep shortly few times
                                Buzz_PutTone(Tone1, 20);
                            }
                        }
                    }
                    break;
                    
                case ST_ALARM:                          // [10ms]
                    // Alarm is emitted until battery is drained, button is pressed or 
                    // main supply voltage is reapplied
                    if (isMainSupplyPresent())
                    {
                        swState(ST_WAKEUP);
                    }
                    else
                    {
                        // Process buzzer controller
                        Buzz_Process();
                        
                        if (buttons.action_down & BTN)
                        {
                            // User wants to disable buzzer
                            swState(ST_SLEEP);
                            exit = 0;
                            break;
                        }
                        if (timers.dly == 0)
                        {
                            // Beep long few times
                            Buzz_PutTone(Tone1, 250);
                        }
                        if (++timers.dly >= 250)
                        {
                            timers.dly = 0;
                        }
                    }
                    break;
                    
                    
                case ST_SLEEP:

                    // Enable interrupt from main supply IRQ and BTN
                    GPIO_Init(GPIOB, GPB_BTN_PIN, GPIO_MODE_IN_FL_IT);
                    GPIO_Init(GPIOB, GPB_VCCSEN_PIN, GPIO_MODE_IN_FL_IT);

                    stopAwu();       		// No interrupts from AWU in sleep mode - only external irq
                    asm("HALT");            // Halt - AFU is disabled
                    // *** halted ***
                    // Woke up from halt by main supply IRQ or BTN press - the only sources of interrupts for this state
                    
                    // Disable interrupt from button
                    GPIO_Init(GPIOB, GPB_BTN_PIN, GPIO_MODE_IN_FL_NO_IT);
                    GPIO_Init(GPIOB, GPB_VCCSEN_PIN, GPIO_MODE_IN_FL_NO_IT);
                   
                    // See what happened
                    swState(ST_WAKEUP);
                    startAwu(AWU_10MS);
                    break;

            }  // ~switch (state)
        } // ~while (!exit)
        
        
        alarms.directControl.startContBeep = 0;
        alarms.directControl.stopContBeep = 0;
        alarms.directControl.startTimeoutBeep = 0;
        alarms.powBat.doLowBatBeep = 0;
        alarms.directControl.startTimeoutBeep = 0;
    }
}


// Callback from buzzer FSM
void onBuzzerStateChanged(uint8_t isActive)
{
    SET_LED(Led3, isActive);
}




//=================================================================//
// Interrupt handlers


//INTERRUPT_HANDLER(IRQ_Handler_TIM4, 23)
//{
//    TIM4_ClearITPendingBit(TIM4_IT_UPDATE);
//    sysFlag_TmrTick = 1;
//}


INTERRUPT_HANDLER(AWU_IRQHandler, 1)
{
    volatile unsigned char reg;
    sysFlag_TmrTick = 1;
    reg = AWU->CSR;     // Reading AWU_CSR register clears the interrupt flag.

}

INTERRUPT_HANDLER(IRQ_Handler_GPIOB, 4)
{
    // Do nothing. Interrupt handler is used to run main loop.
}





//=================================================================//
// Expiremental


void testPwm(void)
{
    CLK_SYSCLKConfig(CLK_PRESCALER_HSIDIV8);    // Fmaster = 2MHz
    CLK_SYSCLKConfig(CLK_PRESCALER_CPUDIV2);    // Fcpu = 1MHz
    
    // System timer init
    TIM4_DeInit();
    TIM4_TimeBaseInit(TIM4_PRESCALER_8, 90);     // F = Fmaster / TimeBase
    TIM4_ITConfig(TIM4_IT_UPDATE, ENABLE);
    
    sysFlag_TmrTick = 0;
    enableInterrupts(); 
    TIM4_Cmd(ENABLE);
    
    while (1)
    {
        if (sysFlag_TmrTick)
        {
            SET_LED(Led3, 1);
            
            smallDelay(20);
            
            SET_LED(Led3, 0);
            
            /*
            if (isButtonPressed())
            {
                SET_LED(Led3, 1);
                
                // Phase 1 ON
                GPIO_WriteLow(GPIOC, GPC_CH1N_PIN);
                GPIO_WriteHigh(GPIOC, GPC_CH2_PIN);
                
                smallDelay(150);
                
                // Phase 1 OFF
                GPIO_WriteHigh(GPIOC, GPC_CH1N_PIN);
                GPIO_WriteLow(GPIOC, GPC_CH2_PIN);
                
                // Phase 2 ON
                GPIO_WriteLow(GPIOC, GPC_CH2N_PIN);
                GPIO_WriteHigh(GPIOC, GPC_CH1_PIN);
                
                smallDelay(150);
                
                // Phase 2 OFF
                GPIO_WriteHigh(GPIOC, GPC_CH2N_PIN);
                GPIO_WriteLow(GPIOC, GPC_CH1_PIN);
                
                // Phase 3 On
                GPIO_WriteLow(GPIOC, GPC_CH1N_PIN);
                GPIO_WriteHigh(GPIOC, GPC_CH2_PIN);
                
                smallDelay(150);
                
                // Phase 3 OFF
                GPIO_WriteHigh(GPIOC, GPC_CH1N_PIN);
                GPIO_WriteLow(GPIOC, GPC_CH2_PIN);
            }
            else
            {
                SET_LED(Led3, 0);
            }
            */
            sysFlag_TmrTick = 0;
        }
    }
}


