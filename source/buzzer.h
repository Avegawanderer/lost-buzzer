#ifndef BUZZER_H
#define BUZZER_H

#include "global_def.h"

#define BUZZER_FSM_CALL_PERIOD_MS          10


void Buzz_Init(eVolume volume);
void Buzz_SetVolume(eVolume volume);
void Buzz_BeepContinuous(eTone tone);
void Buzz_PutTone(eTone tone, uint16_t ms);
void Buzz_Stop(void);
uint8_t Buzz_IsActive(void);
void Buzz_Process(void);




#endif
