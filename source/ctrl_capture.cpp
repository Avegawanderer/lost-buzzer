/**
    @brief Control signal capture module
    @author avegawanderer
*/

#include "global_def.h"
#include "stm8s_def.h"
#include "ctrl_capture.h"


typedef enum {
    CAP_IDLE,
    CAP_WAIT_FIRST_EDGE,
    CAP_WAIT_SECOND_EDGE,
} eCapState;


static struct {
    volatile eCapState state;
    volatile uint16_t ccr1;
    volatile uint16_t ccr2;
} cap;



/*
    Timers are fed by Fmaster (2MHz)
    Fcpu = Fmaster / CPUDIV[2:0] (1MHz)
*/


/**
Initialize timer and start single impulse capture
*/
void startCapture(void)
{
    cap.state = CAP_WAIT_FIRST_EDGE;
    cap.ccr1 = cap.ccr2 = 0;

    // Make sure timer is not running
    TIM2->CR1 = 0;

    TIM2->IER = /*TIM2_IER_CC1IE |*/ TIM2_IER_UIE;
    TIM2->EGR = 0;
    TIM2->CCMR1 =   (4 << TIMx_CCMR1_IC1F_BPOS)     |       // digital filter, see reference manual
                    (0 << TIMx_CCMR1_IC1PSC_BPOS)   |       // 0: no input capture prescaler
                    (1 << 0);                               // 1: CC1 channel = input, IC1 mapped to TI1FP1

    TIM2->CCER1 = (0 << TIMx_CC1P_BPOS) |       // 0: Capture on rising TI1F of TI2F, 1: capture on falling edge
                  (1 << TIMx_CC1E_BPOS);        // 0: Capture disabled, 1: capture enabled
    TIM2->CNTRH = 0;
    TIM2->CNTRL = 0;
    TIM2->PSCR = 1;                             // Prescaler = 2, providing 1us timebase
    TIM2->ARRH = 0xFF;                          // Auto-reload value, set to maximum
    TIM2->ARRL = 0xFF;

    TIM2->CR1 = (0 << TIMx_CR1_ARPE_BPOS) |     // 0: ARR registers are not buffered
                (0 << TIMx_CR1_OPM_BPOS) |      // 0: counter is not stopped at update event (not implemented in TIM2/TIM3)
                (0 << TIMx_CR1_URS_BPOS) |      //
                (0 << TIMx_CR1_UDIS_BPOS) |
                (1 << TIMx_CR1_CEN_BPOS);
}


/**
Stop capture
*/
void stopCapture(void)
{
    // Stop timer
    TIM2->CR1 = 0;
    cap.state = CAP_IDLE;
}


/**
Get length of a single captured impulse
@return Length of an impulse in us.
    If capture did not triger, returns 0
*/
uint16_t getCapturedPwmUs(void)
{
    uint16_t time = 0;
    if ((ccr1 != 0) && (ccr2 != 0))
    {
        // Both first and second edges were detected
        time = ccr2 - ccr1;
    }
    return 0;
}


/**
ISR for TIM2 update/overflow
*/
INTERRUPT_HANDLER(isr_timer2_upd, 13)
{
    // No capture happened for timer update interval
    // Stop timer
    TIM2->CR1 = 0;
    cap.state = CAP_IDLE;
}


/**
ISR for TIM2 capture
*/
INTERRUPT_HANDLER(isr_timer2_cap, 14)
{
    switch (cap.state)
    {
        case CAP_WAIT_FIRST_EDGE:
            // Get captured value
            ccr1 = (uint16_t)TIM2->CCR1H;
            ccr1 = (ccr1 << 8) | TIM2->CCR1L;
            // Set opposite edge
            TIM2->CCER1 = (1 << TIMx_CC1P_BPOS) |       // 0: Capture on rising TI1F of TI2F, 1: capture on falling edge
                          (1 << TIMx_CC1E_BPOS);        // 0: Capture disabled, 1: capture enabled
            // Set next state
            cap.state = CAP_WAIT_SECOND_EDGE;
            break;

        case CAP_WAIT_SECOND_EDGE:
            // Get captured value
            ccr2 = (uint16_t)TIM2->CCR1H;
            ccr2 = (ccr2 << 8) | TIM2->CCR1L;
            // Stop timer
            TIM2->CR1 = 0;
            break;
    }
}


