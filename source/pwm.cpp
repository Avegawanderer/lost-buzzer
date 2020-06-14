
#include "global_def.h"
#include "pwm.h"


/*
    Timers are fed by Fmaster
    Fcpu = Fmaster / CPUDIV[2:0]

    For H-Bridge PWM center-aligned mode is required
    For center-aligned mode, effective PWM signal period will be 2 * PWM_PERIOD
*/

#define TIM1_FMASTER_DIV      2     // TIM1 clk must be 2MHz, select accordingly to Fmaster


void PWM_Beep(uint16_t pwm_period, uint8_t pwm_deadtime)
{
    TIM1_Cmd(DISABLE);
    TIM1_CtrlPWMOutputs(DISABLE);
    TIM1->CNTRL = 0;
    TIM1->CNTRH = 0;
    TIM1_TimeBaseInit(TIM1_FMASTER_DIV - 1, TIM1_COUNTERMODE_CENTERALIGNED1, pwm_period, 0);
    
    // Channel active: OC1REF = 1
    // PWM1: channel active when TIM1_CNT < TIM1_CCR1 (up) and TIM1_CNT <= TIM1_CCR1 (down)
    // PWM2: channel inactive when TIM1_CNT < TIM1_CCR1 and TIM1_CNT <= TIM1_CCR1 (down)
    // MOE: main channel output enable (TIM1_BKR[7]), when cleared, OC and OCN outputs are disabled or forced to idle state

    TIM1_OC1Init( TIM1_OCMODE_PWM2, 
                  TIM1_OUTPUTSTATE_ENABLE, 
                  TIM1_OUTPUTNSTATE_ENABLE,
                  pwm_period >> 1,
                  TIM1_OCPOLARITY_HIGH, 
                  TIM1_OCNPOLARITY_LOW, 
                  TIM1_OCIDLESTATE_RESET,
                  TIM1_OCNIDLESTATE_SET
    ); 
    
    TIM1_OC2Init( TIM1_OCMODE_PWM1, 
                  TIM1_OUTPUTSTATE_ENABLE, 
                  TIM1_OUTPUTNSTATE_ENABLE, 
                  pwm_period >> 1,
                  TIM1_OCPOLARITY_HIGH, 
                  TIM1_OCNPOLARITY_LOW, 
                  TIM1_OCIDLESTATE_RESET, 
                  TIM1_OCNIDLESTATE_SET
    );

    TIM1_Cmd(ENABLE);
    TIM1->DTR = pwm_deadtime;
#if ENA_PWM_OUTPUT == 1
    TIM1_CtrlPWMOutputs(ENABLE);
#endif
}


void PWM_Stop(void)
{
#if ENA_PWM_OUTPUT == 1
    TIM1_CtrlPWMOutputs(DISABLE);
#endif
    TIM1_Cmd(DISABLE);
    TIM1->CNTRL = 0;
    TIM1->CNTRH = 0;
}



