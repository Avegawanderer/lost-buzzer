
#include "board.h"


void PWM_Beep(uint8_t muteLevel)
{
    TIM1_DeInit();
    TIM1_TimeBaseInit(0, TIM1_COUNTERMODE_CENTERALIGNED1, 366, 0);
    
    // Channel active: OC1REF = 1
    // PWM1: channel active when TIM1_CNT < TIM1_CCR1 (up) and TIM1_CNT <= TIM1_CCR1 (down)
    // PWM2: channel inactive when TIM1_CNT < TIM1_CCR1 and TIM1_CNT <= TIM1_CCR1 (down)
    // MOE: main channel output enable (TIM1_BKR[7]), when cleared, OC and OCN outputs are disabled or forced to idle state
    
    
    TIM1_OC1Init( TIM1_OCMODE_PWM2, 
                  TIM1_OUTPUTSTATE_ENABLE, 
                  TIM1_OUTPUTNSTATE_ENABLE,
                  183, 
                  TIM1_OCPOLARITY_HIGH, 
                  TIM1_OCNPOLARITY_LOW, 
                  TIM1_OCIDLESTATE_RESET,
                  TIM1_OCNIDLESTATE_SET
    ); 
    
    TIM1_OC2Init( TIM1_OCMODE_PWM1, 
                  TIM1_OUTPUTSTATE_ENABLE, 
                  TIM1_OUTPUTNSTATE_ENABLE, 
                  183,
                  TIM1_OCPOLARITY_HIGH, 
                  TIM1_OCNPOLARITY_LOW, 
                  TIM1_OCIDLESTATE_RESET, 
                  TIM1_OCNIDLESTATE_SET
    );


    TIM1_Cmd(ENABLE);
    TIM1_CtrlPWMOutputs(ENABLE);
    
    switch (muteLevel)
    {
        case 0:
            TIM1->DTR = (0x3 << 5) | 0x1F;
            break;
        case 1:
            TIM1->DTR = (0x2 << 5) | 0x1F;      // FIXME
            break;
        case 2:
            TIM1->DTR = (0x1 << 5) | 0x1F;      // FIXME
            break;
        default:
            TIM1_Cmd(DISABLE);
            TIM1_CtrlPWMOutputs(DISABLE);
            break;
    }
}


void PWM_Stop(void)
{
    TIM1_CtrlPWMOutputs(DISABLE);
    TIM1_Cmd(DISABLE);
}



