
#include "stm8s.h"


void delay(u16 time)
{
    while(time > 0)
    {
        time--;
    }
}



void setup_pwm(void)
{
    TIM1_DeInit();
    TIM1_TimeBaseInit(0, TIM1_COUNTERMODE_CENTERALIGNED1, 4095, 0);
    
    TIM1_OC1Init( TIM1_OCMODE_PWM2, 
                  TIM1_OUTPUTSTATE_ENABLE, 
                  TIM1_OUTPUTNSTATE_ENABLE,
                  1500, 
                  TIM1_OCPOLARITY_LOW, 
                  TIM1_OCNPOLARITY_HIGH, 
                  TIM1_OCIDLESTATE_SET,
                  TIM1_OCNIDLESTATE_RESET
    ); 
    
    TIM1_OC2Init( TIM1_OCMODE_PWM2, 
                  TIM1_OUTPUTSTATE_ENABLE, 
                  TIM1_OUTPUTNSTATE_ENABLE, 
                  1000,
                  TIM1_OCPOLARITY_LOW, 
                  TIM1_OCNPOLARITY_HIGH, 
                  TIM1_OCIDLESTATE_SET, 
                  TIM1_OCNIDLESTATE_RESET
    );
  

    TIM1_Cmd(ENABLE);
    TIM1_CtrlPWMOutputs(ENABLE);
}


int main()
{
    /* Fmaster = 16MHz */
    CLK_HSIPrescalerConfig(CLK_PRESCALER_HSIDIV1);
    
    
    /* GPIOD reset */
    GPIO_DeInit(GPIOD);

    /* Configure PD0 (LED1) as output push-pull low (led switched on) */
    GPIO_Init(GPIOB, GPIO_PIN_4 | GPIO_PIN_5, GPIO_MODE_OUT_PP_LOW_FAST);
  
  
    while(1)
    {
        GPIO_WriteHigh(GPIOB, GPIO_PIN_4);
        delay(10000);
        GPIO_WriteLow(GPIOB, GPIO_PIN_4);
        GPIO_WriteHigh(GPIOB, GPIO_PIN_5);
        delay(10000);
        GPIO_WriteLow(GPIOB, GPIO_PIN_5);
    }
  
    return 0;
}
