#ifndef __GLOBAL_DEF_H__
#define __GLOBAL_DEF_H__

#include "stm8s.h"


// GPIOA
#define GPA_LED1_PIN        GPIO_PIN_2
#define GPA_LED2_PIN        GPIO_PIN_3
#define GPA_BTNUP_PIN       GPIO_PIN_1

// GPIOB
#define GPB_VCCSEN_PIN      GPIO_PIN_4

// GPIOC
#define GPC_LED3_PIN        GPIO_PIN_5
#define GPC_CH1_PIN         GPIO_PIN_6
#define GPC_CH1N_PIN        GPIO_PIN_3
#define GPC_CH2_PIN         GPIO_PIN_7
#define GPC_CH2N_PIN        GPIO_PIN_4
#define GPC_SIG_PIN         GPIO_PIN_5

// GPIOD
#define GPD_SWIM_PIN        GPIO_PIN_1
#define GPD_VREF_PIN        GPIO_PIN_2
#define GPD_VBAT_PIN        GPIO_PIN_3
#define GPD_UART_PIN        GPIO_PIN_5
#define GPD_BTN_PIN         GPIO_PIN_6



// ADC channels
typedef enum {
    adcChBtn = ADC1_CHANNEL_6,
    adcChVbat = ADC1_CHANNEL_4,
    adcChVref = ADC1_CHANNEL_3,
} eAdcChannels;

// LEDs
typedef enum {
    Led1,
    Led2,
    Led3
} eLeds;

typedef struct {
    GPIO_TypeDef *GPIO;
    uint8_t pin;
    uint8_t state;
} ledCtrl_t;

// Buzzer
// Frequencies of the buzzer signals are limited to this set
typedef enum {
    ToneSilence,
    Tone1,
    Tone2,
    Tone3,
    Tone4,
    ToneCount
} eTone;

typedef enum {
    MuteNone,       // Maximum sound
    Mute1,          // Something at middle
    Mute2,          // Very quiet sound
    MuteFull,       // No sound at all
    MuteCount
} eMuteLevel;

// Settings
typedef struct {
    struct {
        uint8_t directControlActiveHigh : 1;
    } io;


} config_t;

// FSM logical states
typedef enum {
    ST_WAKEUP,
    ST_RUN,
    ST_PREALARM,
    ST_ALARM,
    ST_SLEEP,
} bState_t;



#endif  //__GLOBAL_DEF_H__

