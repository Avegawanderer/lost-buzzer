#ifndef __CTRL_CAPTURE_H__
#define __CTRL_CAPTURE_H__

#include "global_def.h"

typedef enum {
    CapPosImpulse = 0,
    CapNegImpulse = 1,
} eCapPolarity;


void initCapture(void);
void startCapture(eCapPolarity polarity);
void stopCapture(void);
uint8_t isCaptureActive(void);
uint16_t getCapturedPulseUs(void);





#endif  // __CTRL_CAPTURE_H__
