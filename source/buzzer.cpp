
#include "global_def.h"
#include "buzzer.h"
#include "buzzer_private.h"
#include "pwm.h"

//=================================================================//
// Data types and definitions

// see buzzer_private.h


//=================================================================//
// Data


static eBuzState buzzerState = BZ_IDLE;
static buzzerData_t buzzerData;


//=================================================================//
// Externals

// Callback for LEDs management
void onBuzzerStateChanged(uint8_t isActive);


//=================================================================//
// Control interface


void Buzz_Init(eVolume volume)
{
    buzzerState = BZ_IDLE;
    buzzerData.volume = volume;
    buzzerData.queueWrCount = 0;
}


void Buzz_SetVolume(eVolume volume)
{
    buzzerData.volume = volume;
}


void Buzz_BeepContinuous(eTone tone)
{
    // This function does not require FSM to be called
    PWM_Beep(tone, buzzerData.volume);
    buzzerState = BZ_CONTINUOUS;
    // Clear queue
    buzzerData.queueWrCount = 0;
    onBuzzerStateChanged(1);
}


void Buzz_PutTone(eTone tone, uint16_t ms)
{
    // Ignored if buzzer is set for continuous beep
    // Queued beeps are processed by FSM
    if ((buzzerData.queueWrCount < BUZZER_QUEUE_SIZE) && (buzzerState != BZ_CONTINUOUS))
        buzzerData.queue[buzzerData.queueWrCount++] = (buzQueueElement_t){tone, ms};
}


void Buzz_Stop(void)
{
    PWM_Stop();
    buzzerState = BZ_IDLE;
    onBuzzerStateChanged(0);
}


uint8_t Buzz_IsActive(void)
{
    return (buzzerState != BZ_IDLE) || (buzzerData.queueWrCount > 0);
}


uint8_t Buzz_IsContinuousBeep(void)
{
    return (buzzerState == BZ_CONTINUOUS);
}


//=================================================================//
// FSM


void Buzz_Process(void)
{
    buzQueueElement_t elm;
    uint8_t exit = 0;
    while (!exit)
    {
        // Run once by default
        exit = 1;
        switch (buzzerState)
        {
            case BZ_IDLE:
                if (buzzerData.queueWrCount > 0)
                {
                    buzzerState = BZ_START_QUEUED_TONE;
                    onBuzzerStateChanged(1);
                    exit = 0;
                }
                break;

            case BZ_START_QUEUED_TONE:
                elm = buzzerData.queue[0];
                // Shift queue
                for (uint8_t i=0; i<buzzerData.queueWrCount; i++)
                {
                    // Nothing to shift in for full queue
                    if (i == BUZZER_QUEUE_SIZE-1)
                        continue;
                    buzzerData.queue[i] = buzzerData.queue[i+1];
                }
                buzzerData.queueWrCount--;
                PWM_Beep(elm.tone, buzzerData.volume);
                onBuzzerStateChanged(elm.tone != ToneSilence);
                buzzerData.timer = 0;
                buzzerData.toneDurationMs = elm.ms;
                buzzerState = BZ_PLAYING_QUEUED_TONE;
                break;

            case BZ_PLAYING_QUEUED_TONE:
                buzzerData.timer += BUZZER_FSM_CALL_PERIOD_MS;
                if (buzzerData.timer >= buzzerData.toneDurationMs)
                {
                    if (buzzerData.queueWrCount > 0)
                    {
                        buzzerState = BZ_START_QUEUED_TONE;
                        exit = 0;
                    }
                    else
                    {
                        PWM_Stop();
                        buzzerState = BZ_IDLE;
                        onBuzzerStateChanged(0);
                    }
                }
                break;

            default:
                break;
        }
    }
}




