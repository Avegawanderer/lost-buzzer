
#include "global_def.h"
#include "buzzer.h"
#include "buzzer_private.h"
#include "pwm.h"

//=================================================================//
// Data types and definitions

// see buzzer_private.h

/*
DTG[7:5]            DT
    0xx (0x00)            DTG[6:0]  * (1*t)     0 to 127, step 1
    10x (0x80)      (64 + DTG[5:0]) * (2*t)     128 to 254, step 2
    110 (0xC0)      (32 + DTG[4:0]) * (8*t)     256 to 504, step 8
    111 (0xE0)      (32 + DTG[4:0]) * (16*t)    512 to 1008, step 16

    t = TIM1 clock Fck_psc, before prescaler
    DT may vary from 0 (no dead time at all) to 1023 (output is kept idle)
*/

#define DT_0xx(x)   (x)
#define DT_10x(x)   ((x / 2) - 64 + 0x80)
#define DT_110(x)   ((x / 8) - 32 + 0xC0)
#define DT_111(x)   ((x / 16) - 32 + 0xE0)

#define DT(x)       ((x < 128) ? DT_0xx(x) : \
                    ((x < 256) ? DT_10x(x) : \
                    ((x < 512) ? DT_110(x) : DT_111(x))))


//=================================================================//
// Data


static eBuzState buzzerState = BZ_IDLE;
static buzzerData_t buzzerData;

static timCtrl_t toneCtrl[ToneCount] =
{
    // PWM Period (must be even)      MuteNone             Mute1               Mute2               MuteFull
    {.pwm_period = 100,     .pwm_dt = {0xFF,                0xFF,               0xFF,               0xFF} },        // ToneSilence
    {.pwm_period = 366,     .pwm_dt = {DT(180),             DT(320),            DT(356),            0xFF} },        // Tone1
    {.pwm_period = 416,     .pwm_dt = {0xFF,                0xFF,               0xFF,               0xFF} },        // Tone2
    {.pwm_period = 480,     .pwm_dt = {0xFF,                0xFF,               0xFF,               0xFF} },        // Tone3
    {.pwm_period = 500,     .pwm_dt = {0xFF,                0xFF,               0xFF,               0xFF} }         // Tone4
};


//=================================================================//
// Externals

// Callback for LEDs management
void onBuzzerStateChanged(uint8_t isActive);



//=================================================================//
// Control interface


void Buzz_Init(void)
{
    buzzerState = BZ_IDLE;
    buzzerData.muteLevel = MuteNone;
    buzzerData.queueWrCount = 0;
}


void Buzz_SetMuteLevel(eMuteLevel muteLevel)
{
    buzzerData.muteLevel = muteLevel;
}


void Buzz_BeepContinuous(eTone tone)
{
    // This function does not require FSM to be called
    PWM_Beep(toneCtrl[tone].pwm_period, toneCtrl[tone].pwm_dt[buzzerData.muteLevel]);
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
    return (buzzerState != BZ_IDLE);
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
                PWM_Beep(toneCtrl[elm.tone].pwm_period, toneCtrl[elm.tone].pwm_dt[buzzerData.muteLevel]);
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




