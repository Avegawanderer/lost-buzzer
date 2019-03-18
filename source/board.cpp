
#include "stm8s.h"
#include "board.h"

uint8_t led1State;
uint8_t led2State;


void BRD_Init(void)
{
    CLK_SYSCLKConfig(CLK_PRESCALER_HSIDIV8);    // Fmaster = 2MHz
    CLK_SYSCLKConfig(CLK_PRESCALER_CPUDIV2);    // Fcpu = 1MHz
    
    /* GPIOD reset */
    GPIO_DeInit(GPIOB);

    /* Configure LED1 and LED2 as output push-pull low (led switched on) */
    GPIO_Init(GPIOB, (GPIO_Pin_TypeDef)(GPIO_PIN_4 | GPIO_PIN_5), GPIO_MODE_OUT_PP_HIGH_FAST);
    
    /* Configure PD2 as input with interrupt enabled */
    GPIO_Init(GPIOD, (GPIO_Pin_TypeDef)(GPIO_PIN_2), GPIO_MODE_IN_FL_IT);
    // Enable interrupt for main supply rise
    EXTI_SetExtIntSensitivity(EXTI_PORT_GPIOD, EXTI_SENSITIVITY_RISE_ONLY);
    
    /* Configure OCx outputs */
    GPIO_Init(GPIOC, (GPIO_Pin_TypeDef)(GPIO_PIN_3 | GPIO_PIN_4), GPIO_MODE_OUT_PP_HIGH_FAST);
    GPIO_Init(GPIOC, (GPIO_Pin_TypeDef)(GPIO_PIN_6 | GPIO_PIN_7), GPIO_MODE_OUT_PP_LOW_FAST);
}

void BRD_SetLed1(uint8_t isOn)
{
    if (isOn)
        GPIO_WriteLow(GPIOB, GPIO_PIN_4);
    else
        GPIO_WriteHigh(GPIOB, GPIO_PIN_4);
    led1State = isOn;
}


void BRD_SetLed2(uint8_t isOn)
{
    if (isOn)
        GPIO_WriteLow(GPIOB, GPIO_PIN_5);
    else
        GPIO_WriteHigh(GPIOB, GPIO_PIN_5);
    led2State = isOn;
}


uint8_t BRD_IsMainSupplyPresent(void)
{
    return GPIO_ReadInputData(GPIOD) & (1 << 2);
}


uint8_t BRD_IsButtonPressed(void)
{
    return GPIO_ReadInputData(GPIOD) & (1 << 6);
}


uint8_t BRD_IsDirectControlInputActive(void)
{
    return GPIO_ReadInputData(GPIOD) & (1 << 4);
}

