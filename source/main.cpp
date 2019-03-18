

#include "board.h"
//#include "buzzer.h"


void Buzz_Stop() {}
void Buzz_Init() {}
uint8_t Buzz_IsActive() {}
void Buzz_Process() {}
void Buzz_StartContinuous() {}
void Buzz_StartMs() {}



// STM8S003: 8kB FLASH, 1kB RAM, 128B EEPROM


typedef enum {
    ST_WAKEUP,
    ST_RUN,
    ST_PREALARM,
    ST_ALARM,
    ST_SLEEP,
} bState_t;



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


volatile uint8_t sysFlag_TmrTick;
struct {
    uint8_t mainSupplyOk;
    uint8_t btnPressed;
} flags;

// Maximum value for timers used in FSM
#define MAX_TMR     65535

struct {
    uint16_t tick;
    uint16_t state;
    uint16_t evt;
    uint16_t dly;
} timers;

bState_t state;


// AFR0 and AFR7 must be set for TIM1 PWM outputs at PortC


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
    // __disable_leds();
}

void incSatTimer(uint16_t *tmr)
{
    if (*tmr < MAX_TMR)
        (*tmr)++;
}

#define AWU_1MS     ((3 << 8) | (20 << 0))
#define AWU_10MS    ((6 << 8) | (36 << 0))

void setAwuPeriod(uint16_t value)
{
    // Set the TimeBase
    AWU->TBR &= (uint8_t)(~AWU_TBR_AWUTB);
    AWU->TBR |= (uint8_t)(value >> 8);

    // Set the APR divider
    AWU->APR &= (uint8_t)(~AWU_APR_APR);
    AWU->APR |= (uint8_t)(value & 0xFF);
}


int main()
{
    uint8_t tmp8u;
    uint8_t exit;
    
    BRD_Init();
    Buzz_Init();
    
#if 0
    // System timer init
    TIM4_Cmd(DISABLE);
    TIM4_TimeBaseInit(TIM4_PRESCALER_8, 124);     // 1 ms
    TIM4_ClearFlag(TIM4_FLAG_UPDATE);
    TIM4_ITConfig(TIM4_IT_UPDATE, ENABLE);
  
    TIM4_Cmd(ENABLE);
#endif
    
    // Enable LSI
    CLK_LSICmd(ENABLE);

    // Enable AWU
    AWU->CSR |= AWU_CSR_AWUEN;
    // AWU setup for WAKEUP timebase
    setAwuPeriod(AWU_1MS);
    
    
    // Use Active-halt with main voltage regulator (MVR) powered off 
    // (increased startup time of ~50us is acceptable)
    // Do not used fast clock wakeup since HSI is always used
    //CLK_SlowActiveHaltWakeUpCmd(ENABLE);
    //CLK_SlowActiveHaltWakeUpCmd(DISABLE);
    
    // Init FSM
    state = ST_WAKEUP;
    sysFlag_TmrTick = 0;
    
    // Start
    enableInterrupts();  
    
    while(1) {
        asm("HALT");    // Active halt - AFU is enabled
    }
    
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
        incSatTimer(&timers.state);
        
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
                case ST_WAKEUP:                         // [8ms]
                    // Provide some delay to ensure supply voltage is OK
                    break;
                    if (timers.state >= 5)
                    {
                        if (flags.mainSupplyOk)
                        {
                            // Capture input parameters (volume level / mute / etc)
                            // Measure voltage level on BTN input. Depending on it, mute or disable buzzer
                            // TODO
                            
                            // Detect cell count for power battery
                            // TODO
                            
                            // Reset data for RUN state
                            alarms.directControl.dirCtrlState = 0;
                            // add more here ...
                            
                            // __enable_uart();        // And other peripherals - TODO
                            
                            swState(ST_RUN);
                            AWU_Init(AWU_TIMEBASE_1MS);
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
                    if (flags.mainSupplyOk)
                    {                      
                        // Process buzzer controller
                        if (++timers.tick >= 8)
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
                        // Main supply is not OK
                        if (timers.state < 3000)
                        {
                            // __disable_uart();        // And other peripherals - TODO
                            swState(ST_SLEEP);
                            exit = 0;
                        }
                        else
                        {
                            // __disable_uart();        // And other peripherals - TODO
                            swState(ST_PREALARM);
                            AWU_Init(AWU_TIMEBASE_8MS);
                        }
                    }
                    break;
                    
                case ST_PREALARM:                       // [8ms]
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
                        if (++timers.dly >= 200)
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
                    
                case ST_ALARM:                          // [8ms]
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
                        }
                        if (timers.dly >= 500)
                        {
                            timers.dly = 0;
                            // Beep long few times
                            Buzz_StartMs(500);
                        }
                        else
                        {
                            timers.dly++;
                        }
                    }
                    break;
                    
                    
                case ST_SLEEP:
                    AWU_IdleModeEnable();       // Stop AWU
                    asm("HALT");                // Halt - AFU is disabled
                    // *** halted ***
                    // Woke up from halt by main supply IRQ - the only source of interrupts for this state
                    swState(ST_WAKEUP);
                    AWU_Init(AWU_TIMEBASE_8MS);
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


INTERRUPT_HANDLER(IRQ_Handler_TIM4, 23)
{
    sysFlag_TmrTick = 1;
    TIM4_ClearITPendingBit(TIM4_IT_UPDATE);
}

INTERRUPT_HANDLER(AWU_IRQHandler, 1)
{
    volatile unsigned char reg;
    BRD_SetLed2(1);
    
    //sysFlag_TmrTick = 1;
    //AWU_GetFlagStatus();   
    reg = AWU->CSR;     // Reading AWU_CSR register clears the interrupt flag.
    
    BRD_SetLed2(0);
}

INTERRUPT_HANDLER(IRQ_Handler_EXTIO3, 6)
{
    // Do nothing. Interrupt handler is used to run main loop.
}


