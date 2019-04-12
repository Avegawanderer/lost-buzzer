/**
    @brief UART IO module
    @author avegawanderer
*/

#include "global_def.h"
#include "stm8s_def.h"
#include "uart.h"



void UART_Init(void)
{
    UART1->CR1 =    (0 << 5) |          // UART enabled (no low power mode)
                    (0 << 4) |          // 8-n-ss, ss = 1 or 2 stop bits depending on CR3
                    (0 << 3) |          // Wakeup method
                    (0 << 2) |          // No parity control
                    (0 << 1) |          // Parity selection
                    (0 << 0);           // Parity interrupt disabled

    UART1->CR2 =    (0 << 7) |          // TIEN interrupt
                    (0 << 6) |          // TCIEN
                    (0 << 5) |          // RIEN
                    (0 << 4) |          // ILEN
                    (1 << 3) |          // Enable transmitter
                    (1 << 2) |          // Enable receiver
                    (0 << 1) |          // mute mode
                    (0 << 0);           // break char

    UART1->CR3 =    (0 << 6) |          // LIN mode disabled
                    (0 << 4) |          // 1 stop bit
                    (0 << 3) |          // SLK pin disabled
                    (0 << 2) |          // CPOL
                    (0 << 1) |          // CPHA
                    (0 << 0);           // LBCL

    UART1->CR4 =    0x00;               // LIN stuff

    UART1->CR5 =    (0 << 5) |          // Smartcard mode disabled
                    (0 << 4) |          // Smartcard nack
                    (1 << 3) |          // Half-duplex mode
                    (0 << 2) |          // IrDA low power
                    (0 << 1);           // IrDA disabled

    UART1->GTR =    0x00;               // Smartcard-related
    UART1->PSCR =   0;                  // Smartcard and IrDA-related

    // BRR for 9600 @ 2MHz = 208.(3) = 0xD0
    UART1->BRR2 = BRR2(208);
    UART1->BRR1 = BRR1(208);

}


void UART_Process(void)
{
    uint8_t data8;
    // Simple loop
    if (UART1->SR & (1 << 5))
    {
        data8 = UART1->DR;
    }
    data8++;
    UART1->DR = data8;
    while (!(UART1->SR & (1 << 6)));
    // Read out echo
    data8 = UART1->DR;
}







