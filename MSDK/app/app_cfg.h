/*!
    \file    app_cfg.h
    \brief   application configuration for GD32VW55x SDK

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

#ifndef _APP_CFG_H_
#define _APP_CFG_H_

#include "platform_def.h"

#define CONFIG_DEBUG_PRINT_ENABLE

// #define PLATFORM_ASSERT_ENABLE

#ifdef PLATFORM_OS_RTTHREAD
#define START_TASK_STACK_SIZE       512
#define START_TASK_PRIO             4
#endif

#define CONFIG_OTA_DEMO_SUPPORT
#ifdef CONFIG_OTA_DEMO_SUPPORT
#define OTA_DEMO_STACK_SIZE         512
#define OTA_DEMO_TASK_PRIO          1
#endif

#define CONFIG_IPERF_TEST
// #define CONFIG_IPERF3_TEST

#ifdef CONFIG_IPERF3_TEST
#define IPERF_TASK_MAX              2
#endif

#define CONFIG_BASECMD

// #define CONFIG_SPI_I2S
// #define CONFIG_SOFTAP_PROVISIONING

// #define CONFIG_ATCMD
// #define CONFIG_ATCMD_HTTP_CLIENT
// #define CONFIG_ATCMD_SPI
// #define CONFIG_FLASH_NOT_BLOCK_UART_RX
#ifdef CONFIG_ATCMD_SPI
#ifndef CONFIG_ATCMD
#error "CONFIG_ATCMD must be defined"
#endif
#endif

// #define CONFIG_INTERNAL_DEBUG

// #define CONFIG_MQTT
// #define CONFIG_COAP
// #define CONFIG_NAPT

// #define CONFIG_WIFI_MESH_SMART
#ifdef CONFIG_WIFI_MESH_SMART
#undef CONFIG_NAPT
#undef CONFIG_SOFTAP_PROVISIONING

#define CONFIG_NAPT
#define CONFIG_SOFTAP_PROVISIONING
#endif

// #define CONFIG_ATCMD_OTA_DEMO
#ifdef CONFIG_ATCMD_OTA_DEMO
#undef CONFIG_ATCMD
#undef CONFIG_MQTT

#define CONFIG_ATCMD
#define CONFIG_MQTT
#endif

// #define CONFIG_FATFS_SUPPORT
#ifdef CONFIG_FATFS_SUPPORT
#if CONFIG_BOARD == PLATFORM_BOARD_32VW55X_EVAL
#define USE_QSPI_FLASH
#endif
#define FATFS_USE_WL
#endif

#define CONFIG_FAST_RECONNECT

// #define CONFIG_SSL_TEST

// #define CONFIG_LWIP_SOCKETS_TEST

// #define CONFIG_SNTP

// #define CONFIG_IPV6_SUPPORT

// #define CONFIG_TINY_WEBSOCKETS

#ifdef CFG_MATTER
    #undef CONFIG_BASECMD
    #undef CONFIG_ATCMD
    #ifndef CONFIG_IPV6_SUPPORT
        #define CONFIG_IPV6_SUPPORT
    #endif
#endif

#ifdef CFG_BLE_SUPPORT

#define BLE_LIB_MIN                          0       //only peripharal and server
#define BLE_LIB_MAX                          1       //add central and client usage

#define CONFIG_BLE_LIB                       BLE_LIB_MIN

/* If configured, BLE will be initialized when boot and stay enabled, otherwise BLE will not be
   initialized and will be enabled when ble courier is enabled by command and be disabled after
   ble courier wifi is disabled by command.
 */
#define CONFIG_BLE_ALWAYS_ENABLE

#define FEAT_SUPPORT_BLE_DATATRANS           0
#if FEAT_SUPPORT_BLE_DATATRANS
#define PURE_DATA_TRANSMIT_MODE              0           //transmit all data
#define MIXED_TRANSMIT_MODE                  1           //only transmit data which can not be recognized by cmd moudule

#define BLE_DATATRANS_MODE                   PURE_DATA_TRANSMIT_MODE
#endif // FEAT_SUPPORT_BLE_DATATRANS

#define FEAT_SUPPORT_SAVE_DEV_NAME           0

#define FEAT_SUPPORT_ADV_AFTER_REBOOT        0

#define FEAT_SUPPORT_BLE_OTA                 0

#define FEAT_SUPPORT_ADV_AFTER_DISCONN       0
#endif

#include "rftest_cfg.h"

#endif  /* _APP_CFG_H_ */
