#ifndef __GLOBAL_DEF_H__
#define __GLOBAL_DEF_H__

#include "stm8s.h"


// Debug option
#define ENA_PWM_OUTPUT          1


// GPIOA
#define GPA_LED1_PIN        GPIO_PIN_2     // LED1 and LED2 are swapped on PCB
#define GPA_LED2_PIN        GPIO_PIN_1
#define GPA_LED3_PIN        GPIO_PIN_3
#define GPA_BTNUP_PIN       GPIO_PIN_1

// GPIOB
#define GPB_VCCSEN_PIN      GPIO_PIN_4
#define GPB_BTN_PIN         GPIO_PIN_5

// GPIOC
#define GPC_CH1_PIN         GPIO_PIN_6
#define GPC_CH1N_PIN        GPIO_PIN_3
#define GPC_CH2_PIN         GPIO_PIN_7
#define GPC_CH2N_PIN        GPIO_PIN_4
#define GPC_SIG_PIN         GPIO_PIN_5

// GPIOD
#define GPD_SWIM_PIN        GPIO_PIN_1
#define GPD_VREF_PIN        GPIO_PIN_2
#define GPD_VBAT_PIN        GPIO_PIN_3
#define GPD_VREF_SUPP_PIN   GPIO_PIN_4
#define GPD_UART_PIN        GPIO_PIN_5
#define GPD_RESERVED_PINS   GPIO_PIN_6




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
    VolumeSilent,       // No sound at all
    VolumeLow,          // Very quiet sound
    VolumeMedium,       // Something at middle
    VolumeHigh,         // Maximum sound
    VolumeCount
} eVolume;

// Settings
typedef struct {
    struct {
        uint8_t directControlActiveHigh : 1;
    } io;


} config_t;

// FSM logical states
typedef enum {
    ST_WAKEUP,
    ST_NOSUPPLY,        // Buzzer level selection
    ST_NOSUPPLY_EXIT,
    ST_WAITSUPPLY,
    ST_RUN,
    ST_RUN_SETUP_VOLUME,
    ST_RUN_SETUP_VOLUME_EXIT,
    ST_PREALARM,
    ST_ALARM,
    ST_SLEEP,
} bState_t;



#endif  //__GLOBAL_DEF_H__

