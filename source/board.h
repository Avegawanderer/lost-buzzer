#ifndef BOARD_H
#define BOARD_H

#include "stm8s.h"

void initGpio(void);
//void BRD_EnablePullupForButtonInput(void);
//void BRD_DisablePullupForButtonInput(void);
void BRD_SetLed1(uint8_t isOn);
void BRD_SetLed2(uint8_t isOn);
uint8_t BRD_IsMainSupplyPresent(void);
uint8_t BRD_IsButtonPressed(void);
uint8_t BRD_IsDirectControlInputActive(void);

extern uint8_t led1State;
extern uint8_t led2State;



#endif
