
#include "stm8s.h"
#include "board.h"

uint8_t led1State;
uint8_t led2State;


void BRD_SetLed1(uint8_t isOn)
{
    if (isOn)
        GPIO_WriteHigh(GPIOB, GPIO_PIN_4);
    else
        GPIO_WriteLow(GPIOB, GPIO_PIN_4);
    led1State = isOn;
}


void BRD_SetLed2(uint8_t isOn)
{
    if (isOn)
        GPIO_WriteHigh(GPIOB, GPIO_PIN_5);
    else
        GPIO_WriteLow(GPIOB, GPIO_PIN_5);
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

