#ifndef BUZZER_PRIVATE_H
#define BUZZER_PRIVATE_H

#include "global_def.h"

// Tone queue size
#define BUZZER_QUEUE_SIZE       20



// Queue element
typedef struct {
    eTone tone;
    uint16_t ms;
} buzQueueElement_t;


// Buzzer FSM states
typedef enum {
    BZ_IDLE,
    BZ_START_QUEUED_TONE,
    BZ_PLAYING_QUEUED_TONE,
    BZ_CONTINUOUS
} eBuzState;


// Buzzer data
typedef struct {
    uint16_t timer;
    uint16_t toneDurationMs;
    eVolume volume;
    buzQueueElement_t queue[BUZZER_QUEUE_SIZE];
    uint8_t queueWrCount;


} buzzerData_t;

#endif
