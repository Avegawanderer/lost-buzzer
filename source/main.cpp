
#include "stm8s.h"


void delay(u16 time)
{
    while(time > 0)
    {
        time--;
    }
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
