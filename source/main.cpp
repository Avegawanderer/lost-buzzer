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

#define DFLT_VOLUME             VolumeSilent      //VolumeSilent VolumeHigh


//=================================================================//
// Data

//static struct {

//    // Alarm for power battery monitor
//    struct {
//        uint8_t doLowBatBeep;
//    } powBat;

//    // Alarm for direct control (PWM or level)
//    struct {
//        uint8_t dirCtrlState;
//        uint8_t startContBeep;
//        uint8_t stopContBeep;
//        uint8_t startTimeoutBeep;
//    } directControl;
//} alarms;



static struct {
    uint16_t tick;
    uint16_t state;
    uint16_t evt;
    uint16_t dly;
} timers;


static bState_t state;
static uint8_t buzzerVolume = VolumeSilent;

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
#define SET_LED(led, state) {(state) ? GPIO_WriteLow(ledCtrl[led].GPIO, (GPIO_Pin_TypeDef)ledCtrl[led].pin) : \
                                       GPIO_WriteHigh(ledCtrl[led].GPIO, (GPIO_Pin_TypeDef)ledCtrl[led].pin);}


void initGpio(void)
{
    uint8_t i;
    
    // LEDs
    for (i=0; i<sizeof(ledCtrl) / sizeof(ledCtrl_t); i++)
    {
        GPIO_Init(ledCtrl[i].GPIO, (GPIO_Pin_TypeDef)ledCtrl[i].pin, GPIO_MODE_OUT_PP_HIGH_SLOW);
        GPIO_WriteHigh(ledCtrl[i].GPIO, (GPIO_Pin_TypeDef)ledCtrl[i].pin);
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
    
    Buzz_Stop();
    SET_LED(Led1, 0);
    SET_LED(Led2, 0);
    SET_LED(Led3, 0);
}




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

Low-power:
     1MHz CPU, HSI 16MHz:
     WFI - 600uA
     HALT (active) - 220uA
*/



int main()
{   
    CLK_SYSCLKConfig(CLK_PRESCALER_HSIDIV4);    // Fmaster = 4MHz
    CLK_SYSCLKConfig(CLK_PRESCALER_CPUDIV1);    // Fcpu = 4MHz
    
    initGpio();
    Buzz_Init((eVolume)buzzerVolume);
    
    // GPIO PWM test for buzzer
    //testPwm();
    SET_LED(Led1, 1);
      
    // Enable LSI
    CLK_LSICmd(ENABLE);

    // Setup ADC
    //ADC1_PrescalerConfig(ADC1_PRESSEL_FCPU_D4);

    // System timer init (used in ST_RUN)
    TIM4_DeInit();
    TIM4_TimeBaseInit(TIM4_PRESCALER_16, 249);     // F = (Fmaster / Prescaler) / Period
    TIM4_ITConfig(TIM4_IT_UPDATE, DISABLE);

    // AWU setup for WAKEUP timebase
    startAwu(AWU_10MS);
    
    // Use Active-halt with main voltage regulator (MVR) powered off 
    // (increased startup time of ~50us is acceptable)
    // Do not used fast clock wakeup since HSI is always used
    //CLK_SlowActiveHaltWakeUpCmd(ENABLE);
    //CLK_SlowActiveHaltWakeUpCmd(DISABLE);
    
    // Init FSM
    swState(ST_WAKEUP);
    
    // Start
    enableInterrupts();  

    while(1)
    {          
        // Process FSM controller
        switch (state)
        {
            case ST_WAKEUP:
                if (isMainSupplyPresent())
                {
                    // Wait for supply to stabilize for about 20ms
                    asm("HALT");            // Active halt - wait for interrupt from AFU
                    asm("HALT");            // Active halt - wait for interrupt from AFU

                    // Check once again
                    if (isMainSupplyPresent())
                    {
                        SET_LED(Led1, 1);
                        SET_LED(Led2, 1);
                        SET_LED(Led3, 1);
                        startAwu(AWU_100MS);
                        asm("WFI");     // Wait for interrupts - peripherals are active
                        swState(ST_RUN);
                    }
                    else
                    {
                        // Power glitch
                        swState(ST_SLEEP);
                    }
                }
                else
                {
                    // Check BTN state
                    ProcessButtons();
                    if (buttons.action_down & BTN)
                    {
                        swState(ST_NOSUPPLY);
                    }
                    else
                    {
                        // Unexpected wake-up
                        swState(ST_SLEEP);
                    }
                }
                break;

            case ST_NOSUPPLY:
                startAwu(AWU_10MS);
                while (1)
                {
                    if (timers.state == 0)
                    {
                        // First entry or button has been pressed
                        // Show current buzzer level
                        SET_LED(Led1, volumeLedIndication[buzzerVolume].led1);
                        SET_LED(Led2, volumeLedIndication[buzzerVolume].led2);
                        SET_LED(Led3, volumeLedIndication[buzzerVolume].led3);
                    }

                    asm("WFI");     // Wait for interrupts - peripherals are active

                    if (isMainSupplyPresent())
                    {
                        // Start normal startup procedure
                        swState(ST_WAKEUP);
                        break;
                    }

                    // Check BTN state
                    ProcessButtons();

                    // Process state timer
                    timers.state++;

                    // If button has not been pressed for about 1 second and there is no supply, sleep again
                    if (timers.state >= 100)
                    {
                        // Beep at selected level
                        Buzz_PutTone(Tone1, 100);
                        while (Buzz_IsActive())
                        {
                            asm("WFI");     // Wait for interrupts - peripherals are active
                            Buzz_Process();
                        }
                        swState(ST_SLEEP);
                        break;
                    }

                    if (buttons.action_down & BTN)
                    {
                        buzzerVolume = (buzzerVolume < (VolumeCount - 1)) ? buzzerVolume + 1 : 0;
                        Buzz_SetVolume((eVolume)buzzerVolume);
                        timers.state = 0;
                    }
                }
                break;

            case ST_RUN:
                // Using Tim4 as timebase source for better accuracy
                stopAwu();
                TIM4_Cmd(ENABLE);
                TIM4_ITConfig(TIM4_IT_UPDATE, ENABLE);
                // TODO: Detect cell count for power battery
                // TODO: Enable other peripherals
                while (1)
                {
                    asm("WFI");     // Wait for interrupts - peripherals are active

                    if (!isMainSupplyPresent())
                    {
                        swState(ST_PREALARM);
                        break;
                    }

                    // Check BTN state
                    ProcessButtons();

                    // Process buzzer controller
                    if (++timers.tick >= 10)
                    {
                        timers.tick = 0;
                        Buzz_Process();
                    }

                    // Check if button is pressed to select volume level
                    if (buttons.action_down & BTN)
                    {
                        swState(ST_RUN_SETUP_VOLUME);
                        break;
                    }

                    // Simple direct control
                    if (isDirectControlInputActive() && (!Buzz_IsContinuousBeep()))
                        Buzz_BeepContinuous(Tone1);
                    else if (!isDirectControlInputActive() && Buzz_IsContinuousBeep())
                        Buzz_Stop();

                }
                TIM4_Cmd(DISABLE);
                TIM4_ITConfig(TIM4_IT_UPDATE, DISABLE);
                // TODO: Disable peripherals
                break;

            case ST_RUN_SETUP_VOLUME:
                startAwu(AWU_10MS);
                while (1)
                {
                    if (timers.state == 0)
                    {
                        // First entry or button has been pressed
                        // Show current buzzer level
                        SET_LED(Led1, volumeLedIndication[buzzerVolume].led1);
                        SET_LED(Led2, volumeLedIndication[buzzerVolume].led2);
                        SET_LED(Led3, volumeLedIndication[buzzerVolume].led3);
                    }

                    asm("WFI");     // Wait for interrupts - peripherals are active

                    // Check BTN state
                    ProcessButtons();

                    // Process state timer
                    timers.state++;

                    // If button has not been pressed for about 1 second and there is no supply, sleep again
                    if (timers.state >= 100)
                    {
                        // Beep at selected level
                        Buzz_PutTone(Tone1, 100);
                        while (Buzz_IsActive())
                        {
                            asm("WFI");     // Wait for interrupts - peripherals are active
                            Buzz_Process();
                        }
                        swState(ST_RUN);
                        break;
                    }

                    if (buttons.action_down & BTN)
                    {
                        buzzerVolume = (buzzerVolume < (VolumeCount - 1)) ? buzzerVolume + 1 : 0;
                        Buzz_SetVolume((eVolume)buzzerVolume);
                        timers.state = 0;
                    }
                }
                break;

            case ST_PREALARM:
                startAwu(AWU_10MS);
                while(1)
                {
                    if (Buzz_IsActive())
                        asm("WFI");             // Wait for interrupts - peripherals are active
                    else
                        asm("HALT");            // Active halt - wait for interrupt from AFU

                    if (isMainSupplyPresent())
                    {
                        swState(ST_WAKEUP);
                        break;
                    }

                    // Check BTN state
                    ProcessButtons();

                    // Process buzzer controller
                    Buzz_Process();

                    if (buttons.action_down & BTN)
                    {
                        // User wants to disable buzzer
                        swState(ST_SLEEP);
                        break;
                    }

                    if (++timers.dly >= 100)
                    {
                        timers.dly = 0;
                        if (++timers.evt == 10)
                        {
                            swState(ST_ALARM);
                            break;
                        }
                        else
                        {
                            // Beep shortly few times
                            Buzz_PutTone(Tone1, 10);
                        }
                    }
                }
                break;

            case ST_ALARM:
                startAwu(AWU_10MS);
                while(1)
                {
                    if (Buzz_IsActive())
                        asm("WFI");             // Wait for interrupts - peripherals are active
                    else
                        asm("HALT");            // Active halt - wait for interrupt from AFU

                    // Alarm is emitted until battery is drained, button is pressed or
                    // main supply voltage is reapplied
                    if (isMainSupplyPresent())
                    {
                        swState(ST_WAKEUP);
                        break;
                    }

                    // Check BTN state
                    ProcessButtons();

                    // Process buzzer controller
                    Buzz_Process();

                    if (buttons.action_down & BTN)
                    {
                        // User wants to disable buzzer
                        swState(ST_SLEEP);
                        break;
                    }
                    if (timers.dly == 0)
                    {
                        // Beep long few times
                        Buzz_PutTone(Tone1, 200);
                        Buzz_PutTone(ToneSilence, 100);
                        Buzz_PutTone(Tone1, 30);
                        Buzz_PutTone(ToneSilence, 30);
                        Buzz_PutTone(Tone1, 30);
                        Buzz_PutTone(ToneSilence, 30);
                        Buzz_PutTone(Tone1, 30);
                        Buzz_PutTone(ToneSilence, 30);
                        Buzz_PutTone(Tone1, 30);
                    }
                    if (++timers.dly >= 300)
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
    }
}


// Callback from buzzer FSM
void onBuzzerStateChanged(uint8_t isActive)
{
    SET_LED(Led3, isActive);
}




//=================================================================//
// Interrupt handlers


INTERRUPT_HANDLER(IRQ_Handler_TIM4, 23)
{
    TIM4_ClearITPendingBit(TIM4_IT_UPDATE);
    // Do nothing. Interrupt handler is used to run main loop.
}


INTERRUPT_HANDLER(IRQ_Handler_AWU, 1)
{
    volatile unsigned char reg;
    // Reading AWU_CSR register clears the interrupt flag.
    reg = AWU->CSR;
    // Do nothing. Interrupt handler is used to run main loop.
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
    
    enableInterrupts(); 
    TIM4_Cmd(ENABLE);
    
    while (1)
    {
        asm("WFI");             // Wait for interrupts - peripherals are active
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
    }
}



void testAdc(void)
{
    // FIXME - test only
    // Measure voltage on VREF pin
    ADC1_Cmd(ENABLE);
    ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, (ADC1_Channel_TypeDef)adcChVref, ADC1_ALIGN_RIGHT);
    ADC1_StartConversion();
    while (ADC1_GetFlagStatus(ADC1_FLAG_EOC) == RESET);
    volatile uint16_t adcVref = ADC1_GetConversionValue();

    // Measure voltage on VREF pin
    ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, (ADC1_Channel_TypeDef)adcChVbat, ADC1_ALIGN_RIGHT);
    ADC1_StartConversion();
    while (ADC1_GetFlagStatus(ADC1_FLAG_EOC) == RESET);
    volatile uint16_t adcVbat = ADC1_GetConversionValue();

    // Capture input parameters (volume level / mute / etc)
    // Measure voltage level on BTN pin. Depending on it, mute or disable buzzer
    ADC1_Cmd(ENABLE);
    ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, (ADC1_Channel_TypeDef)adcChBtn, ADC1_ALIGN_RIGHT);
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

