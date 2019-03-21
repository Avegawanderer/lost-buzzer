#ifndef BUZZER_PRIVATE_H
#define BUZZER_PRIVATE_H

#include "board.h"

typedef enum {
    buzIdle,
    buzTimed,
    buzContinuous
} buzState_t;


typedef struct {
    uint16_t timer;
    uint16_t timeMs;
    uint8_t muteLevel;

} buzData_t;

#endif
