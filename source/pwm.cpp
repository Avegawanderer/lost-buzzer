
#include "global_def.h"
#include "pwm.h"


/*
    Timers are fed by Fmaster
    Ftim1 = Fmaster / TIM1_CAPWM_FMASTER_DIV

    For H-Bridge PWM center-aligned mode is required
    For center-aligned mode, effective PWM signal period will be 2 * PWM_PERIOD
*/

#define TIM1_CAPWM_FMASTER_DIV          2       // TIM1 clk must be 2MHz, select accordingly to Fmaster

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

// Dead-time is specified in CK_PSC periods (before Timer1 prescaler) = Fmaster
// To specify dead-time in us, use the macro below:
#define DT2(x)      DT(2*(x))


// Timer setup for a tone, including frequecy (period) and dead-time for valirous volume levels
typedef struct {
    uint16_t pwm_period;
    uint8_t pwm_dt[VolumeCount];
} timCtrl_t;


// pwm_period = [us]
// pwm_dt = [us], 0 to 504
static timCtrl_t toneCtrl[ToneCount] =
{
    // PWM Period (must be even)        VolumeSilent    VolumeLow       VolumeMedium    VolumeHigh
    {.pwm_period = 100,     .pwm_dt = { 0xFF,           0xFF,           0xFF,           0xFF       } },        // ToneSilence
    {.pwm_period = 366,     .pwm_dt = { 0xFF,           DT2(356),       DT2(320),       DT2(180)   } },        // Tone1 - 2732Hz, 40mA @4.2V, 52mA @5V, 31 mA @3.3V
    {.pwm_period = 416,     .pwm_dt = { 0xFF,           DT2(400),       DT2(380),       DT2(250)   } },        // Tone2 - 2403Hz
    {.pwm_period = 480,     .pwm_dt = { 0xFF,           DT2(460),       DT2(420),       DT2(350)   } },        // Tone3 - 2083Hz
    {.pwm_period = 183,     .pwm_dt = { 0xFF,           DT(178),        DT(160),        DT(90)     } }         // Tone4 - 5464Hz
};



void PWM_Beep(eTone tone, eVolume volume)
{
    timCtrl_t *pTone = &toneCtrl[tone];
    uint16_t halfPeriod = pTone->pwm_period >> 1;

    // Select the Counter Mode
    TIM1->CR1 = 0;      // Timer disabled
    TIM1->CR2 = 0;      // CCx registers are not preloaded
    TIM1->BKR = 0;      // Outputs disabled

    // Set the Prescaler value
    TIM1->PSCRH = (uint8_t)0;
    TIM1->PSCRL = (uint8_t)(TIM1_CAPWM_FMASTER_DIV - 1);

    // Set the Autoreload value
    TIM1->ARRH = (uint8_t)(pTone->pwm_period >> 8);
    TIM1->ARRL = (uint8_t)(pTone->pwm_period);

    // Channel active: OC1REF = 1
    // PWM1: channel active when TIM1_CNT < TIM1_CCR1 (up) and TIM1_CNT <= TIM1_CCR1 (down)
    // PWM2: channel inactive when TIM1_CNT < TIM1_CCR1 and TIM1_CNT <= TIM1_CCR1 (down)
    // MOE: main channel output enable (TIM1_BKR[7]), when cleared, OC and OCN outputs are disabled or forced to idle state

    // OC1 / OC1N
    TIM1->CCER1 = (1 << 0)  |       // CC1E: Capture/compare 1 output enable
                  (0 << 1)  |       // CC1P: Capture/compare 1 output polarity (0: OC1 active high, 1: OC1 active low)
                  (1 << 2)  |       // CC1NE: Capture/compare 1 complementary output enable
                  (1 << 3)  |       // CC1NP: Capture/compare 1 complementary output polarity (0: OC1N active high,  1: OC1N active low)
                  (1 << 4)  |       // CC2E: Capture/compare 2 output enable
                  (0 << 5)  |       // CC2P: Capture/compare 2 output polarity (0: OC2 active high, 1: OC2 active low)
                  (1 << 6)  |       // CC2NE: Capture/compare 2 complementary output enable
                  (1 << 7)  |       // CC2NP: Capture/compare 2 complementary output polarity (0: OC2N active high,  1: OC2N active low)
                  0;

    TIM1->OISR =  (0 << 0)  |       // OIS1 : Output idle state 1 (OC1 output) (0: OC1=0 when MOE=0, 1: OC1=1 when MOE=0)
                  (1 << 1)  |       // OIS1N: Output idle state 1 (OC1N output) (OC1N = 0 when MOE = 0, 1: OC1N = 1 when MOE = 0)
                  (0 << 2)  |       // OIS2
                  (1 << 3)  |       // OIS2N
                  0;

    TIM1->CCMR1 = TIM1_OCMODE_PWM2;
    TIM1->CCMR2 = TIM1_OCMODE_PWM1;

    // Set compare level. Different pulse width is achieved with dead-time generation
    TIM1->CCR1H = (uint8_t)(halfPeriod >> 8);
    TIM1->CCR1L = (uint8_t)(halfPeriod);
    TIM1->CCR2H = TIM1->CCR1H;
    TIM1->CCR2L = TIM1->CCR1L;

    // Clear counter regs
    TIM1->CNTRH = 0;
    TIM1->CNTRL = 0;

    // Set dead-time, enable outputs and start timer
    TIM1->DTR = pTone->pwm_dt[volume];
#if ENA_PWM_OUTPUT == 1
    TIM1->BKR = TIM1_BKR_AOE;       // Outputs will be enabled automatically at the next UEV
                                    // This is used to prevent incorrect dead-time generation at the start of the signal
                                    // and thus remove undesired audible clicks
#endif
    TIM1->CR1 = TIM1_CR1_CEN | TIM1_COUNTERMODE_CENTERALIGNED1;      // Timer enabled, center-aligned PWM mode
}


void PWM_Stop(void)
{
    TIM1->CR1 = 0;
    TIM1->BKR = 0;
    TIM1->CNTRL = 0;
    TIM1->CNTRH = 0;
    TIM1->IER = 0;
}



