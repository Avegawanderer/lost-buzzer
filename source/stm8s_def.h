#ifndef __STM8S_DEF_H__
#define __STM8S_DEF_H__

/**
    @brief Some bit filds description to extend stm8s.h
    @author avegawanderer
*/

#include "stm8s.h"



//==================================================================//
//                          Timers                                  //
//                                                                  //
//==================================================================//

/**
    TIMx_CR1
*/
#define TIMx_CR1_ARPE_BPOS      7
#define TIMx_CR1_OPM_BPOS       3
#define TIMx_CR1_URS_BPOS       2
#define TIMx_CR1_UDIS_BPOS      1
#define TIMx_CR1_CEN_BPOS       0


/**
    TIMx_CCER1
*/
#define TIMx_CC2P_BPOS          5
#define TIMx_CC2E_BPOS          4
#define TIMx_CC1P_BPOS          1
#define TIMx_CC1E_BPOS          0

/**
    TIMx_CCER2
*/
#define TIMx_CC3P_BPOS          1
#define TIMx_CC3E_BPOS          0

/**
    TIMx_CCMR1
*/
// Capture mode
#define TIMx_CCMR1_IC1F_BPOS    4
#define TIMx_CCMR1_IC1PSC_BPOS  2
// Common
#define TIMx_CCMR1_CC1S_BPOS    0


/**
    TIMx_SR1
*/
#define TIMx_SR1_UIF_BPOS       0


/**
    TIMx_SR2
*/
#define TIMx_SR2_CC1OF_BPOS       0


/**
    TIMx_EGR
*/
#define TIMx_EGR_UG_BPOS       0


//==================================================================//
//                           UART                                   //
//                                                                  //
//==================================================================//
/**
    Baudrate macros
*/

#define BRR2(x)     ((((x) & 0x0F) | ((x) >> (12 - 4))) & 0xFF)
#define BRR1(x)     (((x) >> 4) & 0xFF)







#endif // __STM8S_DEF_H__
