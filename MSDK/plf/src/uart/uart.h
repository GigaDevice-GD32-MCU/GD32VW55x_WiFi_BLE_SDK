/*!
    \file    uart.h
    \brief   UART BSP for GD32VW55x SDK.

    \version 2023-07-20, V1.0.0, firmware for GD32VW55x
*/

/*
    Copyright (c) 2023, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#ifndef _UART_H
#define _UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include "platform_def.h"
#include "cyclic_buffer.h"

#ifdef CONFIG_PLATFORM_ASIC

#if CONFIG_BOARD == PLATFORM_BOARD_32VW55X_F527
#define USART0_TX_GPIO                  GPIOA
#define USART0_TX_PIN                   GPIO_PIN_8
#define USART0_TX_AF_NUM                GPIO_AF_2

#define USART0_RX_GPIO                  GPIOB
#define USART0_RX_PIN                   GPIO_PIN_15
#define USART0_RX_AF_NUM                GPIO_AF_8
#elif CONFIG_BOARD == PLATFORM_BOARD_32VW55X_LCKFB
#define USART0_TX_GPIO                  GPIOB
#define USART0_TX_PIN                   GPIO_PIN_15
#define USART0_TX_AF_NUM                GPIO_AF_8

#define USART0_RX_GPIO                  GPIOA
#define USART0_RX_PIN                   GPIO_PIN_8
#define USART0_RX_AF_NUM                GPIO_AF_2
#else /* PLATFORM_BOARD_32VW55X_F527 */
#define USART0_TX_GPIO                  GPIOA
#define USART0_TX_PIN                   GPIO_PIN_0
#define USART0_TX_AF_NUM                GPIO_AF_0

#define USART0_RX_GPIO                  GPIOA
#define USART0_RX_PIN                   GPIO_PIN_1
#define USART0_RX_AF_NUM                GPIO_AF_0
#endif /* PLATFORM_BOARD_32VW55X_F527 */
#define USART0_CTS_GPIO                 GPIOA
#define USART0_CTS_PIN                  GPIO_PIN_2
#define USART0_CTS_AF_NUM               GPIO_AF_0

#define USART0_RTS_GPIO                 GPIOA
#define USART0_RTS_PIN                  GPIO_PIN_3
#define USART0_RTS_AF_NUM               GPIO_AF_0

#define UART1_TX_GPIO                   GPIOB
#define UART1_TX_PIN                    GPIO_PIN_15
#define UART1_TX_AF_NUM                 GPIO_AF_7

#define UART1_RX_GPIO                   GPIOA
#define UART1_RX_PIN                    GPIO_PIN_8
#define UART1_RX_AF_NUM                 GPIO_AF_3

#define UART1_CTS_GPIO                  GPIOA
#define UART1_CTS_PIN                   GPIO_PIN_0
#define UART1_CTS_AF_NUM                GPIO_AF_7

#define UART1_RTS_GPIO                  GPIOA
#define UART1_RTS_PIN                   GPIO_PIN_1
#define UART1_RTS_AF_NUM                GPIO_AF_7

#define UART2_TX_GPIO                   GPIOA
#define UART2_TX_PIN                    GPIO_PIN_6
#define UART2_TX_AF_NUM                 GPIO_AF_10

#define UART2_RX_GPIO                   GPIOA
#define UART2_RX_PIN                    GPIO_PIN_7
#define UART2_RX_AF_NUM                 GPIO_AF_8

#define UART2_CTS_GPIO                  GPIOB
#define UART2_CTS_PIN                   GPIO_PIN_0
#define UART2_CTS_AF_NUM                GPIO_AF_10

#define UART2_RTS_GPIO                  GPIOB
#define UART2_RTS_PIN                   GPIO_PIN_1
#define UART2_RTS_AF_NUM                GPIO_AF_10
#else /* CONFIG_PLATFORM_ASIC */
#define USART0_TX_GPIO                  GPIOA
#define USART0_TX_PIN                   GPIO_PIN_9
#define USART0_TX_AF_NUM                GPIO_AF_7

#define USART0_RX_GPIO                  GPIOA
#define USART0_RX_PIN                   GPIO_PIN_10
#define USART0_RX_AF_NUM                GPIO_AF_7

#define USART0_CTS_GPIO
#define USART0_CTS_PIN
#define USART0_CTS_AF_NUM

#define USART0_RTS_GPIO
#define USART0_RTS_PIN
#define USART0_RTS_AF_NUM

#define UART1_TX_GPIO                   GPIOA
#define UART1_TX_PIN                    GPIO_PIN_4
#define UART1_TX_AF_NUM                 GPIO_AF_0

#define UART1_RX_GPIO                   GPIOA
#define UART1_RX_PIN                    GPIO_PIN_5
#define UART1_RX_AF_NUM                 GPIO_AF_0

#define UART1_CTS_GPIO
#define UART1_CTS_PIN
#define UART1_CTS_AF_NUM

#define UART1_RTS_GPIO
#define UART1_RTS_PIN
#define UART1_RTS_AF_NUM

#define UART2_TX_GPIO                   GPIOA
#define UART2_TX_PIN                    GPIO_PIN_6
#define UART2_TX_AF_NUM                 GPIO_AF_10

#define UART2_RX_GPIO                   GPIOA
#define UART2_RX_PIN                    GPIO_PIN_7
#define UART2_RX_AF_NUM                 GPIO_AF_8

#define UART2_CTS_GPIO                  GPIOB
#define UART2_CTS_PIN                   GPIO_PIN_0
#define UART2_CTS_AF_NUM                GPIO_AF_10

#define UART2_RTS_GPIO                  GPIOB
#define UART2_RTS_PIN                   GPIO_PIN_1
#define UART2_RTS_AF_NUM                GPIO_AF_10
#endif /* CONFIG_PLATFORM_ASIC */

#define BAUDRATE_9600           9600
#define BAUDRATE_19200          19200
#define BAUDRATE_38400          38400
#define BAUDRATE_57600          57600
#define BAUDRATE_115200         115200
#define BAUDRATE_921600         921600
#define BAUDRATE_2000000        2000000

#define DEFAULT_LOG_BAUDRATE    BAUDRATE_115200

#define UART_BUFFER_SIZE        128
#define MAX_ARGC                16

#define MAX_UART_NUM            3

typedef void (*uart_rx_irq_hdl_t)(uint32_t uart_port);

typedef struct _uart_config {
    uint32_t usart_periph;
    uint32_t baudrate;
    uint32_t databits;
    uint32_t stopbits;
    uint32_t parity;
    uint32_t flow_ctrl;
} uart_config_t;

typedef struct uart_cb_item {
    uint32_t uart_port;
    uart_rx_irq_hdl_t callback;
} uart_cb_item_t;

void uart_driver_init(void);
bool uart_irq_callback_register(uint32_t uart_port, uart_rx_irq_hdl_t callback);
bool uart_irq_callback_unregister(uint32_t uart_port);
void uart_dma_single_mode_config(uint32_t uart, uint32_t direction);
void uart_config(uint32_t usart_periph, uint32_t baudrate, bool flow_cntl, bool dma_rx, bool dma_tx);
void uart_put_data(uint32_t usart_periph, const uint8_t *d, int size);
void uart_putc_noint(uint32_t usart_periph, uint8_t c);
void uart_irq_hdl(uint32_t uart);
char uart_getc(uint32_t uart_id);

void uart_tx_idle_wait(uint32_t usart_periph);
int uart_getc_with_timeout(uint32_t usart_periph, char *ch, int timeout);
void uart_rx_flush(uint32_t usart_periph);

#ifdef __cplusplus
}
#endif

#endif // _UART_H
