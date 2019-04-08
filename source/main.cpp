/********************************************************************
	Lost buzzer firmware
	
	Target STM8S003: 8kB FLASH, 1kB RAM, 128B EEPROM
	AFR0 and AFR7 must be set for TIM1 PWM outputs at PortC
********************************************************************/


#include "board.h"
#include "buzzer.h"




//=================================================================//
// Data types and definitions

// Maximum value for timers used in FSM
#define MAX_TMR     65535

// ADC channels
typedef enum {
    adcChBtn = ADC1_CHANNEL_6,
    adcChPowerBattery = ADC1_CHANNEL_4
} adcChannels_t;

// Logical states
typedef enum {
    ST_WAKEUP,
    ST_RUN,
    ST_PREALARM,
    ST_ALARM,
    ST_SLEEP,
} bState_t;




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

uint8_t led1State;
uint8_t led2State;

void initGpio(void)
{
    /* Configure LED1 and LED2 as output push-pull low (led switched on) */
    //GPIO_Init(GPIOB, (GPIO_Pin_TypeDef)(GPIO_PIN_4 | GPIO_PIN_5), GPIO_MODE_IN_PU_NO_IT);


    /* Configure LED1 and LED2 as output push-pull low (led switched on) */
    GPIO_Init(GPIOA, (GPIO_Pin_TypeDef)(GPIO_PIN_2 | GPIO_PIN_3), GPIO_MODE_OUT_PP_HIGH_FAST);

    // Configure as input with interrupt enabled
    GPIO_Init(GPIOD, (GPIO_Pin_TypeDef)(GPIO_PIN_2), GPIO_MODE_IN_FL_IT);
    // Enable interrupt for main supply rise
    EXTI_SetExtIntSensitivity(EXTI_PORT_GPIOD, EXTI_SENSITIVITY_RISE_ONLY);

    // Configure PB5 as output to prevent floating
    GPIO_Init(GPIOB, (GPIO_Pin_TypeDef)(GPIO_PIN_4 | GPIO_PIN_5), GPIO_MODE_OUT_OD_LOW_SLOW);

    /* Configure OCx outputs */
    GPIO_Init(GPIOC, (GPIO_Pin_TypeDef)(GPIO_PIN_3 | GPIO_PIN_4), GPIO_MODE_OUT_PP_HIGH_FAST);
    GPIO_Init(GPIOC, (GPIO_Pin_TypeDef)(GPIO_PIN_6 | GPIO_PIN_7), GPIO_MODE_OUT_PP_LOW_FAST);
}


void BRD_SetLed1(uint8_t isOn)
{
    if (isOn)
        GPIO_WriteLow(GPIOA, GPIO_PIN_2);
    else
        GPIO_WriteHigh(GPIOA, GPIO_PIN_2);
    led1State = isOn;
}


void BRD_SetLed2(uint8_t isOn)
{
    if (isOn)
        GPIO_WriteLow(GPIOA, GPIO_PIN_3);
    else
        GPIO_WriteHigh(GPIOA, GPIO_PIN_3);
    led2State = isOn;
}


uint8_t BRD_IsMainSupplyPresent(void)
{
    return GPIO_ReadInputData(GPIOD) & (1 << 2);
}


uint8_t BRD_IsButtonPressed(void)
{
    return !(GPIO_ReadInputData(GPIOD) & (1 << 6));     // Active low
}


uint8_t BRD_IsDirectControlInputActive(void)
{
    return !(GPIO_ReadInputData(GPIOD) & (1 << 4));     // Active low - TODO by config
}



//=================================================================//
// FSM and main loop

void smallDelay()
{
    volatile uint8_t i;
    for (i=0; i<20; i++);
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
    // __disable_leds();    TODO
}


void incTimer(uint16_t *tmr)
{
    if (*tmr < MAX_TMR)
        (*tmr)++;
}


int main()
{
    uint8_t tmp8u, i;
    uint8_t exit;
    
    CLK_SYSCLKConfig(CLK_PRESCALER_HSIDIV8);    // Fmaster = 2MHz
    CLK_SYSCLKConfig(CLK_PRESCALER_CPUDIV2);    // Fcpu = 1MHz

    initGpio();
    Buzz_Init();
    
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
        flags.mainSupplyOk = BRD_IsMainSupplyPresent();
        flags.btnPressed = BRD_IsButtonPressed();
        
        // Process FSM controller
        exit = 0;
        while (!exit)
        {
            exit = 1;       // Run FSM once by default
            switch (state)
            {
                case ST_WAKEUP:                         // [10ms]
                    // Provide some delay to ensure supply voltage is OK
                    if (timers.state == 4)
                    {
                        // Enable external pull-up for button input (check mute level)
                        GPIO_Init(GPIOA, GPIO_PIN_1, GPIO_MODE_OUT_PP_HIGH_FAST);
                    }
                    if (timers.state >= 5)
                    {

                        // Capture input parameters (volume level / mute / etc)
                        // Measure voltage level on BTN input. Depending on it, mute or disable buzzer
                        ADC1_Cmd(ENABLE);
                        ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, (ADC1_Channel_TypeDef)adcChBtn, ADC1_ALIGN_RIGHT);
                        ADC1_StartConversion();
                        while (ADC1_GetFlagStatus(ADC1_FLAG_EOC) == RESET);
                        uint16_t adcBtn = ADC1_GetConversionValue();
                        // Select battery channel to enable digital function on BTN pin
                        ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, (ADC1_Channel_TypeDef)adcChPowerBattery, ADC1_ALIGN_RIGHT);
                        // Enable pad pull-up for button
                        GPIO_Init(GPIOD, (GPIO_Pin_TypeDef)(GPIO_PIN_6), GPIO_MODE_IN_PU_NO_IT);
                        // Enable pad pull-up for control
                        GPIO_Init(GPIOD, (GPIO_Pin_TypeDef)(GPIO_PIN_4), GPIO_MODE_IN_PU_NO_IT);

                        // Determine mute level depending on ADC conversion result
                        muteLevel = (adcBtn > 970) ? 0 :
                                    ((adcBtn > 880) ? 1 :
                                    ((adcBtn > 810) ? 2 :
                                    ((adcBtn > 750) ? 4 : 4)));     // FIXME DEBUG
                        Buzz_SetVolume(muteLevel);

                        // Blink
                        setAwuPeriod(AWU_100MS);
                        if (muteLevel >= 4)
                        {
                            //BRD_SetLed2(1);
                            Buzz_StartContinuous();
                            for (i = 0; i<5; i++)
                            {
                                asm("WFI");
                            }
                            //BRD_SetLed2(0);
                            Buzz_Stop();
                        }
                        else
                        {
                            for (i = 0; i<muteLevel+1; i++)
                            {
                                //BRD_SetLed2(1);
                                Buzz_StartContinuous();
                                asm("WFI");
                                //BRD_SetLed2(0);
                                Buzz_Stop();
                                asm("WFI");
                            }
                        }
                        setAwuPeriod(AWU_10MS);
                        sysFlag_TmrTick = 0;

                        // Detect cell count for power battery
                        // TODO

                        // Reset data for RUN state
                        alarms.directControl.dirCtrlState = 0;
                        // add more here ...

                        // __enable_uart();        // And other peripherals - TODO
                        if (flags.mainSupplyOk)
                        {
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
                    
                        // Process direct buzzer control (level / pwm)
                        if (1)  // __cfg.dirCtrl == __LEVEL__
                        {
                            tmp8u = BRD_IsDirectControlInputActive();
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
                            Buzz_StartContinuous();
                        else if (alarms.directControl.stopContBeep)
                            Buzz_Stop();
                        
                        // Apply other alarms
                        if (alarms.powBat.doLowBatBeep && !Buzz_IsActive())
                            Buzz_StartMs(200);      // TODO
                        if (alarms.directControl.startTimeoutBeep && !Buzz_IsActive())
                            Buzz_StartMs(100);      // TODO
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
                                Buzz_StartMs(20);
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
                            BRD_SetLed1(0);
                            BRD_SetLed2(0);
                            exit = 0;
                            break;
                        }
                        if (timers.dly == 0)
                        {
                            // Beep long few times
                            Buzz_StartMs(250);
                        }
                        if (++timers.dly >= 250)
                        {
                            timers.dly = 0;
                        }
                    }
                    break;
                    
                    
                case ST_SLEEP:
                    //GPIO_DeInit(GPIOC);
                    //GPIO_DeInit(GPIOA);
                    //GPIO_DeInit(GPIOD);
                    //GPIO_Init(GPIOB, (GPIO_Pin_TypeDef)(GPIO_PIN_4 | GPIO_PIN_5), GPIO_MODE_OUT_PP_LOW_SLOW);
                    //smallDelay();

                // Enable external pull-up for button input (check mute level)
                GPIO_Init(GPIOA, GPIO_PIN_1, GPIO_MODE_OUT_PP_LOW_SLOW);
                // Enable pad pull-up for button
                GPIO_Init(GPIOD, (GPIO_Pin_TypeDef)(GPIO_PIN_6), GPIO_MODE_IN_FL_NO_IT);

                    stopAwu();       		// No interrupts from AWU in sleep mode - only external irq
                    asm("HALT");            // Halt - AFU is disabled
                    // *** halted ***
                    // Woke up from halt by main supply IRQ - the only source of interrupts for this state
                    swState(ST_WAKEUP);
                    startAwu(AWU_10MS);
                    // Configure PD2 as normal input
                    //GPIO_Init(GPIOD, (GPIO_Pin_TypeDef)(GPIO_PIN_2), GPIO_MODE_IN_FL_NO_IT);
                    //initGpio();
                    //GPIO_Init(GPIOB, (GPIO_Pin_TypeDef)(GPIO_PIN_4 | GPIO_PIN_5), GPIO_MODE_OUT_PP_HIGH_FAST);
                    break;

            }  // ~switch (state)
        } // ~while (!exit)
        
        
        alarms.directControl.startContBeep = 0;
        alarms.directControl.stopContBeep = 0;
        alarms.directControl.startTimeoutBeep = 0;
        alarms.powBat.doLowBatBeep = 0;
        alarms.directControl.startTimeoutBeep = 0;
        
        //BRD_SetLed1(!led1State);
        //BRD_SetLed2(!led2State);
    }
}


// Callback from buzzer FSM
void onBuzzerStateChanged(uint8_t isActive)
{
    BRD_SetLed1(isActive);
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

    // FIXME DEBUG
    //BRD_SetLed2(1);

    sysFlag_TmrTick = 1;
    reg = AWU->CSR;     // Reading AWU_CSR register clears the interrupt flag.
    
    // FIXME DEBUG
    //BRD_SetLed2(0);
}

INTERRUPT_HANDLER(IRQ_Handler_GPIOD, 6)
{
    // Do nothing. Interrupt handler is used to run main loop.
}


