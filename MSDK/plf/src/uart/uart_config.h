/*!
    \file    uart_config.h
    \brief   UART CONFIG for GD32VW55x SDK.

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

#ifndef _UART_CONFIG_H
#define _UART_CONFIG_H
#ifdef __cplusplus
extern "C" {
#endif

#include "util_config.h"
#include "platform_def.h"
#include "gd32vw55x.h"

#ifdef CFG_GD_TRACE_EXT
#ifdef CONFIG_PLATFORM_ASIC
#ifdef CFG_BLE_HCI_MODE
#define TRACE_UART              UART2
#else
#define TRACE_UART              USART0
#endif
#else
#define TRACE_UART              UART1
#endif          // CONFIG_PLATFORM_ASIC end
#endif /* CFG_GD_TRACE_EXT */

// UART port
#ifdef CFG_BLE_HCI_MODE
#ifdef CONFIG_PLATFORM_ASIC
#ifndef CFG_GD_TRACE_EXT
#if defined(CONFIG_BOARD) && (CONFIG_BOARD == PLATFORM_BOARD_32VW55X_EVAL || CONFIG_BOARD == PLATFORM_BOARD_32VW55X_SONIC)
#define LOG_UART                UART1
#else
#define LOG_UART                UART2
#endif /* CONFIG_BOARD */
#endif
#define HCI_UART                USART0
#else
//#define HCI_UART_RX_DMA
#ifdef HCI_UART_RX_DMA
#define HCI_DMA_CHNL            DMA_CH5
#define HCI_DMA_IRQ_NUM         DMA_Channel5_IRQn
#endif  // HCI_UART_RX_DMA
#define LOG_UART                UART1
#define HCI_UART                UART2
#endif          // CONFIG_PLATFORM_ASIC end
#else /* CFG_BLE_HCI_MODE */
#if defined(CONFIG_BOARD) && (CONFIG_BOARD == PLATFORM_BOARD_32VW55X_EVAL || CONFIG_BOARD == PLATFORM_BOARD_32VW55X_SONIC)
#define LOG_UART                UART1
#else
#define LOG_UART                USART0
#endif /* CONFIG_BOARD */
#endif /* CFG_BLE_HCI_MODE */

#ifdef TRACE_UART
#define TRACE_UART_DMA
#ifdef CONFIG_PLATFORM_ASIC
#ifdef CFG_BLE_HCI_MODE
#define TRACE_DMA_CHNL          DMA_CH6
#define TRACE_DMA_IRQ_NUM       DMA_Channel6_IRQn
#else
#define TRACE_DMA_CHNL          DMA_CH7
#define TRACE_DMA_IRQ_NUM       DMA_Channel7_IRQn
#endif
#else
#define TRACE_DMA_CHNL          DMA_CH1
#define TRACE_DMA_IRQ_NUM       DMA_Channel1_IRQn
#endif
#endif /* TRACE_UART */

#if defined(CONFIG_BOARD) && (CONFIG_BOARD == PLATFORM_BOARD_32VW55X_EVAL || CONFIG_BOARD == PLATFORM_BOARD_32VW55X_SONIC)
#define AT_UART                 UART2
#elif defined(CONFIG_BOARD) && (CONFIG_BOARD == PLATFORM_BOARD_32VW55X_F527)
#define AT_UART                 USART0
#else
#define AT_UART                 UART1
#endif

#ifdef __cplusplus
}
#endif

#endif // _UART_CONFIG_H
