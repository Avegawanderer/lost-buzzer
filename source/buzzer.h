#ifndef BUZZER_H
#define BUZZER_H

#include "board.h"

#define BUZZER_FSM_CALL_PERIOD_MS          10

void Buzz_Init(void);
void Buzz_SetVolume(uint8_t muteLevel);
void Buzz_StartContinuous(void);
void Buzz_StartMs(uint16_t timeMs);
void Buzz_Stop(void);
uint8_t Buzz_IsActive(void);
void Buzz_Process(void);




#endif
