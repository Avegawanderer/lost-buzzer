/********************************************************************
	Lost buzzer firmware
	
	Target STM8S003: 8kB FLASH, 1kB RAM, 128B EEPROM
	AFR0 and AFR7 must be set for TIM1 PWM outputs at PortC
********************************************************************/

#include "global_def.h"
#include "buzzer.h"
#include "ctrl_capture.h"
#include "uart.h"



//=================================================================//
// Data types and definitions

// Maximum value for timers used in FSM
#define MAX_TMR     65535



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
    uint8_t mainSupplyOk;
    uint8_t btnPressed;
} flags;


static struct {
    uint16_t tick;
    uint16_t state;
    uint16_t evt;
    uint16_t dly;
} timers;


volatile uint8_t sysFlag_TmrTick;
static bState_t state;
static uint8_t muteLevel;

// Global structure for storing settings
config_t cfg;


//=================================================================//
// AWU management (used as timebase for FSM)

#define AWU_1MS     ((3 << 8) | (20 << 0))
#define AWU_10MS    ((6 << 8) | (36 << 0))
#define AWU_100MS   ((9 << 8) | (36 << 0))      // FIXME


void setAwuPeriod(uint16_t period)
{
    // Set the TimeBase
    AWU->TBR &= (uint8_t)(~AWU_TBR_AWUTB);
    AWU->TBR |= (uint8_t)(period >> 8);

    // Set the APR divider
    AWU->APR &= (uint8_t)(~AWU_APR_APR);
    AWU->APR |= (uint8_t)(period & 0xFF);
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
    {.GPIO = GPIOA, .pin = GPA_LED1_PIN, .state = 0},
    {.GPIO = GPIOA, .pin = GPA_LED2_PIN, .state = 0},
    {.GPIO = GPIOC, .pin = GPC_LED3_PIN, .state = 0}
};



void initGpio(void)
{
    // LEDs
    GPIO_Init(GPIOA, (GPIO_Pin_TypeDef)(GPA_LED1_PIN | GPA_LED2_PIN), GPIO_MODE_OUT_PP_HIGH_SLOW);
    GPIO_Init(GPIOC, GPC_LED3_PIN, GPIO_MODE_OUT_PP_HIGH_SLOW);

    // Button
    GPIO_Init(GPIOA, GPA_VREF_SUPP_PIN, GPIO_MODE_OUT_PP_LOW_SLOW);
    GPIO_Init(GPIOD, GPD_BTN_PIN, GPIO_MODE_IN_FL_NO_IT);

    // VCC_SEN
    GPIO_Init(GPIOB, (GPIO_Pin_TypeDef)(GPB_VCCSEN_PIN), GPIO_MODE_IN_FL_IT);
    EXTI_SetExtIntSensitivity(EXTI_PORT_GPIOB, EXTI_SENSITIVITY_RISE_ONLY);

    // VBAT, VREF
    GPIO_Init(GPIOD, GPD_VBAT_PIN | GPD_VREF_PIN, GPIO_MODE_IN_FL_NO_IT);

    // SIG, UART
    GPIO_Init(GPIOD, GPD_UART_PIN, GPIO_MODE_OUT_OD_HIZ_SLOW);
    GPIO_Init(GPIOC, GPC_SIG_PIN, GPIO_MODE_IN_PU_NO_IT);

    // Unused pins to prevent floating
    // FIXME
    //GPIO_Init(GPIOD, GPD_SWIM_PIN, GPIO_MODE_IN_PU_NO_IT);
    GPIO_Init(GPIOB, GPIO_PIN_5, GPIO_MODE_OUT_OD_LOW_SLOW);

    // PWM outputs
    GPIO_Init(GPIOC, (GPIO_Pin_TypeDef)(GPIO_PIN_3 | GPIO_PIN_4), GPIO_MODE_OUT_PP_HIGH_FAST);
    GPIO_Init(GPIOC, (GPIO_Pin_TypeDef)(GPIO_PIN_6 | GPIO_PIN_7), GPIO_MODE_OUT_PP_LOW_FAST);
}


void setLed(eLeds led, uint8_t isOn)
{
    if (isOn)
        GPIO_WriteLow(ledCtrl[led].GPIO, ledCtrl[led].pin);
    else
        GPIO_WriteHigh(ledCtrl[led].GPIO, ledCtrl[led].pin);
    ledCtrl[led].state = isOn;
}


uint8_t isMainSupplyPresent(void)
{
    uint8_t pinState = (GPIOB->IDR & GPB_VCCSEN_PIN);
    return pinState;
}


uint8_t isButtonPressed(void)
{
    uint8_t pinState = (GPIOD->IDR & GPD_BTN_PIN);
    return !pinState;
}


uint8_t isDirectControlInputActive(void)
{
    uint8_t pinState = (GPIOC->IDR & GPC_SIG_PIN);
    return (cfg.io.directControlActiveHigh) ? pinState : !pinState;
}



//=================================================================//
// FSM and main loop

void smallDelay(uint16_t n)
{
    volatile uint16_t i;
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
    
    Buzz_Stop();
    //LED1 is turned off by buzzer callback
    setLed(Led2, 0);
    setLed(Led3, 0);
}


void incTimer(uint16_t *tmr)
{
    if (*tmr < MAX_TMR)
        (*tmr)++;
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
    Buzz_Init();
    initCapture();
    UART_Init();
    
    //-------------------------------------//
    // UART echo test
#if 1
    while(1)
    {
        UART_Process();
        smallDelay(50000);
    }
#endif  
    //-------------------------------------//

    //-------------------------------------//
    // Input capture test
#if 0
    enableInterrupts(); 
    
    static volatile uint16_t capTime;
    while(1)
    {
        startCapture(CapPosImpulse);
        while(isCaptureActive());
        
        capTime = getCapturedPulseUs();
    }
#endif  
    //-------------------------------------//

    

    //-------------------------------------//
    // GPIO PWM test for buzzer
#if 0
 	CLK_SYSCLKConfig(CLK_PRESCALER_HSIDIV1);    // Fmaster = 2MHz
    CLK_SYSCLKConfig(CLK_PRESCALER_CPUDIV1);    // Fcpu = 1MHz
    
    
    // System timer init
    TIM4_Cmd(DISABLE);
    TIM4_TimeBaseInit(TIM4_PRESCALER_32, 183);     // F = 500000 / TimeBase
    TIM4_ClearFlag(TIM4_FLAG_UPDATE);
    TIM4_ITConfig(TIM4_IT_UPDATE, ENABLE);
    TIM4_Cmd(ENABLE);
    
    CLK_LSICmd(DISABLE);
    stopAwu();
    
    // Enable pull-up for button
    GPIO_Init(GPIOA, GPA_VREF_SUPP_PIN, GPIO_MODE_OUT_PP_HIGH_SLOW);
    sysFlag_TmrTick = 0;
    enableInterrupts(); 
    
    while (1)
    {
        if (sysFlag_TmrTick)
        {
            sysFlag_TmrTick = 0;
            if (isButtonPressed())
            {
                setLed(Led1, 1);
                
                // Phase 1 ON
                GPIO_WriteLow(GPIOC, GPC_CH1N_PIN);
                GPIO_WriteHigh(GPIOC, GPC_CH2_PIN);
                
                smallDelay(100);
                
                // Phase 1 OFF
                GPIO_WriteHigh(GPIOC, GPC_CH1N_PIN);
                GPIO_WriteLow(GPIOC, GPC_CH2_PIN);
                
                // Phase 2 ON
                GPIO_WriteLow(GPIOC, GPC_CH2N_PIN);
                GPIO_WriteHigh(GPIOC, GPC_CH1_PIN);
                
                smallDelay(100);
                
                // Phase 2 OFF
                GPIO_WriteHigh(GPIOC, GPC_CH2N_PIN);
                GPIO_WriteLow(GPIOC, GPC_CH1_PIN);
                
                // Phase 3 On
                //GPIO_WriteLow(GPIOC, GPC_CH1N_PIN);
                //GPIO_WriteHigh(GPIOC, GPC_CH2_PIN);
                
                //smallDelay(20);
                
                // Phase 3 OFF
                //GPIO_WriteHigh(GPIOC, GPC_CH1N_PIN);
                //GPIO_WriteLow(GPIOC, GPC_CH2_PIN);
                
            }
            else
            {
                setLed(Led1, 0);
            }
        }
    }
#endif
    //-------------------------------------//

    
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
    
    // FIXME DEBUG
    //while(1) {
    //    asm("HALT");    // Active halt - AFU is enabled
    //}
    
    while(1)
    {
        // Wait for interrupt from AWU
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
        
        // Process state timer (common for all states)
        incTimer(&timers.state);
        
        // Update flags
        flags.mainSupplyOk = isMainSupplyPresent();
        flags.btnPressed = isButtonPressed();
        
        // Process FSM controller
        exit = 0;
        while (!exit)
        {
            exit = 1;       // Run FSM once by default
            switch (state)
            {
                case ST_WAKEUP:                         // [10ms]
                    // Provide some delay to ensure supply voltage is OK
                    if (timers.state == 1)
                    {
                        // First entry
                        // Enable external pull-up for button and voltage reference
                        GPIO_Init(GPIOA, GPA_VREF_SUPP_PIN, GPIO_MODE_OUT_PP_HIGH_SLOW);
                        setLed(Led2, 1);
                    }
                    if (timers.state >= 5)
                    {
                        // Check if starting with button pressed
                        // Set initial mute level
                        muteLevel = (flags.btnPressed) ? MuteFull : Mute2;
                        Buzz_SetMuteLevel(muteLevel);
                        setLed(Led2, 0);

                        // Detect cell count for power battery
                        // TODO

                        // Reset data for RUN state
                        alarms.directControl.dirCtrlState = 0;
                        // add more here ...

                        if (flags.mainSupplyOk)
                        {
                            // __enable_uart();         TODO
                            swState(ST_RUN);
                            setAwuPeriod(AWU_1MS);
                            exit = 0;
                        }
                        else
                        {
                            swState(ST_PREALARM);
                            exit = 0;
                        }
                    }
                break;
                
                case ST_RUN:                            // [1ms]
                    if (flags.mainSupplyOk)
                    {                      
                        // Process buzzer controller
                        if (++timers.tick >= 10)
                        {
                            timers.tick = 0;
                            Buzz_Process();
                        }

                        // Process initial mute level controller
                        if (_btn.pressed)
                        {
                            if (timers.state < 5000)
                            {
                                // Beep shortly once
                                Buzz_PutTone(Tone1, 100);
                                // _mute_level = _current_mute_level;
                            }
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
                    
                case ST_PREALARM:                       // [10ms]
                    if (flags.mainSupplyOk)
                    {
                        swState(ST_WAKEUP);
                    }
                    else
                    {
                        // Process buzzer controller
                        Buzz_Process();
                        
                        if (flags.btnPressed)
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
                                Buzz_PutTone(Tone1, 100);
                            }
                        }
                    }
                    break;
                    
                case ST_ALARM:                          // [10ms]
                    // Alarm is emitted until battery is drained, button is pressed or 
                    // main supply voltage is reapplied
                    if (flags.mainSupplyOk)
                    {
                        swState(ST_WAKEUP);
                    }
                    else
                    {
                        // Process buzzer controller
                        Buzz_Process();
                        
                        if (flags.btnPressed)
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

                    // Output low level for button to prevent floating and current leakage
                    GPIO_Init(GPIOA, GPA_VREF_SUPP_PIN, GPIO_MODE_OUT_PP_LOW_SLOW);

                    stopAwu();       		// No interrupts from AWU in sleep mode - only external irq
                    asm("HALT");            // Halt - AFU is disabled
                    // *** halted ***
                    // Woke up from halt by main supply IRQ - the only source of interrupts for this state
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
    setLed(Led1, isActive);
}




//=================================================================//
// Interrupt handlers


INTERRUPT_HANDLER(IRQ_Handler_TIM4, 23)
{
    sysFlag_TmrTick = 1;
    TIM4_ClearITPendingBit(TIM4_IT_UPDATE);
}

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


