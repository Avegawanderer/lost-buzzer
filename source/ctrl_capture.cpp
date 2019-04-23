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
    uint8_t firstPol;
    uint8_t secPol;
} cap;


/*
    Timers are fed by Fmaster (2MHz)
    Fcpu = Fmaster / CPUDIV[2:0] (1MHz)
*/


/**
    Initialize common timer registers

*/
void initCapture(void)
{
    cap.state = CAP_IDLE;

    // Make sure timer is not running
    TIM2->CR1 = 0;
    TIM2->EGR = 0;
    TIM2->IER = 0;
    TIM2->CCMR1 =   (4 << TIMx_CCMR1_IC1F_BPOS)     |       // digital filter, see reference manual
                    (0 << TIMx_CCMR1_IC1PSC_BPOS)   |       // 0: no input capture prescaler
                    (1 << 0);                               // 1: CC1 channel = input, IC1 mapped to TI1FP1
    
    TIM2->PSCR = 1;                                         // Prescaler = 2, providing 1us timebase
    TIM2->ARRH = 0xFF;                                      // Auto-reload value, set to maximum
    TIM2->ARRL = 0xFF;

}


/**
    Initialize timer and start single impulse capture
    Timeout is 65536 us

*/
void startCapture(eCapPolarity polarity)
{
    cap.state = CAP_WAIT_FIRST_EDGE;
    cap.ccr1 = cap.ccr2 = 0;
    cap.firstPol = (polarity == CapPosImpulse) ? 0 : 1;
    cap.secPol = (polarity == CapPosImpulse) ? 1 : 0;

    TIM2->CR1 = 0;
    TIM2->IER = 0;
    
    TIM2->CCER1 = (cap.firstPol << TIMx_CC1P_BPOS) |        // 0: Capture on rising TI1F of TI2F, 1: capture on falling edge
                  (1 << TIMx_CC1E_BPOS);                    // 0: Capture disabled, 1: capture enabled

    // Timer prescaler requires UEV to load new value
    // Counter registers are also cleared
    TIM2->EGR = (1 << TIMx_EGR_UG_BPOS);                    // Generate UG event
    TIM2->SR1 = 0;                                          // Clear interrupt flags
    TIM2->SR2 = 0;                                          // Reset overcapture flags
    TIM2->IER = TIM2_IER_CC1IE | TIM2_IER_UIE;              // Enable interrupts
    
    // Enable counter
    TIM2->CR1 = (0 << TIMx_CR1_ARPE_BPOS) |                 // 0: ARR registers are not buffered
                (0 << TIMx_CR1_OPM_BPOS) |                  // 0: counter is not stopped at update event (not implemented in TIM2/TIM3)
                (0 << TIMx_CR1_URS_BPOS) |                  
                (0 << TIMx_CR1_UDIS_BPOS) |
                (1 << TIMx_CR1_CEN_BPOS);
}


/**
    Stop capture

*/
void stopCapture(void)
{
    // Stop timer
    TIM2->CR1 = 0;          // Stop timer
    TIM2->IER = 0;          // Disable interrupts
    cap.state = CAP_IDLE;
}


/**
    Get status of current capture

    @return 0 if capture is not active: second edge has been detected or counter is stopped by overflow
*/
uint8_t isCaptureActive(void)
{
    return (cap.state != CAP_IDLE);
}



/**
    Get length of a single captured impulse

    @return Length of an impulse in us. If capture result is not valid, returns 0
*/
uint16_t getCapturedPulseUs(void)
{
    uint16_t time = 0;
    if ((cap.ccr1 != 0) && (cap.ccr2 != 0))
    {
        // Both first and second edges were detected
        time = cap.ccr2 - cap.ccr1;
    }
    return time;
}


/**
    ISR for TIM2 update/overflow

*/
INTERRUPT_HANDLER(isr_timer2_upd, 13)
{
    // No capture happened for timer update interval
    TIM2->CR1 = 0;          // Stop timer
    TIM2->IER = 0;          // Disable interrupts
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
            cap.ccr1 = (uint16_t)TIM2->CCR1H;
            cap.ccr1 = (cap.ccr1 << 8) | TIM2->CCR1L;
            // Set opposite edge
            TIM2->CCER1 = (cap.secPol << TIMx_CC1P_BPOS) |  // 0: Capture on rising TI1F of TI2F, 1: capture on falling edge
                          (1 << TIMx_CC1E_BPOS);            // 0: Capture disabled, 1: capture enabled
            // Set next state
            cap.state = CAP_WAIT_SECOND_EDGE;
            break;

        case CAP_WAIT_SECOND_EDGE:
            // Get captured value
            cap.ccr2 = (uint16_t)TIM2->CCR1H;
            cap.ccr2 = (cap.ccr2 << 8) | TIM2->CCR1L;
            if (TIM2->SR2 & (1 << TIMx_SR2_CC1OF_BPOS))
                cap.ccr1 = cap.ccr2 = 0;                    // Overcapture, result is not valid
            TIM2->CR1 = 0;          // Stop timer
            TIM2->IER = 0;          // Disable interrupts
            cap.state = CAP_IDLE;
            break;
    }
}


