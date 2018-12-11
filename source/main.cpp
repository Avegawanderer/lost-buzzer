

#include "board.h"
#include "pwm.h"


typedef enum {
    ST_RUN,
    ST_PRE_ALARM,
    ST_ALARM,
    ST_SLEEP,
} bState_t;

typedef enum {
    ALM_NO_MAIN_SUPPLY,
    ALM_CTRL_TIMEOUT
} alarmReason_t;

volatile uint8_t sysFlag_TmrTick;
uint8_t sysFlag_MainSupplyOk;
uint8_t sysEvt_ButtonPressed;
uint8_t preAlarmReason;

bState_t state;
alarmReason_t almReason;


int main()
{
    /* Fmaster = 1MHz */
    CLK_HSIPrescalerConfig(CLK_PRESCALER_HSIDIV16);
    
    /* GPIOD reset */
    GPIO_DeInit(GPIOD);

    /* Configure PD0 (LED1) as output push-pull low (led switched on) */
    GPIO_Init(GPIOB, (GPIO_Pin_TypeDef)(GPIO_PIN_4 | GPIO_PIN_5), GPIO_MODE_OUT_PP_LOW_FAST);
  
    // System timer init
    TIM4_Cmd(DISABLE);
    TIM4_TimeBaseInit(TIM4_PRESCALER_8, 124);     // 1 ms
    TIM4_ClearFlag(TIM4_FLAG_UPDATE);
    TIM4_ITConfig(TIM4_IT_UPDATE, ENABLE);
  
    // Init FSM
    state = ST_RUN;
    sysFlag_TmrTick = 0;
    // TODO: reset timers
        
    // Start
    enableInterrupts();
    
    while(1)
    {
        asm("WFI");
        while (!sysFlag_TmrTick);
        sysFlag_TmrTick = 0;
   
        // Do the job
        switch (state)
        {
            sysFlag_MainSupplyOk = BRD_IsMainSupplyPresent();
            sysEvt_ButtonPressed = BRD_IsButtonPressed();       // TODO - get event
            
            case ST_RUN:
                if (!sysFlag_MainSupplyOk)
                {
                    state = ST_PRE_ALARM;
                    almReason = ALM_NO_MAIN_SUPPLY;
                    //PWM_Stop();       // TODO
                }
                else if (0) //__sysFlag_CtrlTimeout     TODO
                {
                    state = ST_PRE_ALARM;
                    almReason = ALM_CTRL_TIMEOUT;
                }
                break;
                
            case ST_PRE_ALARM:
                // beep_shortly();      TODO
                if (sysFlag_MainSupplyOk)
                {
                    if ((almReason = ALM_NO_MAIN_SUPPLY) || (sysEvt_ButtonPressed))
                    {
                        state = ST_RUN;
                        // TODO: reset timers
                    }
                    else if ((almReason = ALM_CTRL_TIMEOUT) && (0/*__sysEvt_CtrlChanged*/))    //  - TODO
                    {
                        state = ST_RUN;
                        // TODO: reset timers
                    }
                }
                else
                {
                    if (sysEvt_ButtonPressed)
                    {
                        state = ST_SLEEP;   // TODO - sleep
                    }
                    else if (0)  // timeout - TODO
                    {
                        state = ST_ALARM;
                    }
                }
                break;
                
            case ST_ALARM:
                break;
                
            case ST_SLEEP:
                break;
        }
        
        
        BRD_SetLed2(!led2State);
    }
}


INTERRUPT_HANDLER(IRQ_Handler_TIM4, 23)
{
    sysFlag_TmrTick = 1;
    TIM4_ClearITPendingBit(TIM4_IT_UPDATE);
}
