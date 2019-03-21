
#include "board.h"
#include "buzzer.h"
#include "buzzer_private.h"
#include "pwm.h"

static buzState_t buzState;
static buzData_t buzData;

// Callback for LEDs management
void onBuzzerStateChanged(uint8_t isActive);


void Buzz_Init(void)
{
    buzState = buzIdle;
    buzData.muteLevel = 0;
}


void Buzz_SetVolume(uint8_t muteLevel)
{
    buzData.muteLevel = muteLevel;
}


void Buzz_StartContinuous(void)
{
    PWM_Beep(buzData.muteLevel);
    buzState = buzContinuous;
    onBuzzerStateChanged(1);
}


void Buzz_StartMs(uint16_t timeMs)
{
    buzData.timeMs = timeMs;
    buzData.timer = 0;
    PWM_Beep(buzData.muteLevel);
    buzState = buzTimed;
    onBuzzerStateChanged(1);
}


void Buzz_Stop(void)
{
    PWM_Stop();
    buzState = buzIdle;
    onBuzzerStateChanged(0);
}


uint8_t Buzz_IsActive(void)
{
    return (buzState != buzIdle);
}


void Buzz_Process(void)
{
    switch (buzState)
    {
        case buzTimed:
            buzData.timer += BUZZER_FSM_CALL_PERIOD_MS;
            if (buzData.timer >= buzData.timeMs)
            {
                PWM_Stop();
                buzState = buzIdle;
                onBuzzerStateChanged(0);
            }
            else
            {
                buzData.timer--;
            }
            break;
        default:
            break;
    }
}




