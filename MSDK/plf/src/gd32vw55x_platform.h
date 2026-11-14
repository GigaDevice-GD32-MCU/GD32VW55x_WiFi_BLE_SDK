/*!
    \file    gd32vw55x_platform.h
    \brief   Header file for WIFI reference platform initialization.

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

#ifndef _GD32VW55X_PLATFORM_H_
#define _GD32VW55X_PLATFORM_H_

#include <stdbool.h>

/*============================ MACRO FUNCTIONS ===============================*/

/*============================ TYPES =========================================*/
#define RFAD_SPI SPI0

// GPIO definitions
// LED indicating the CPU is sleeping
#define LED_SLEEP         GPIO_PIN_1 //GPIO_PIN_13
// LED indicating the CPU is running
#define LED_RUN           GPIO_PIN_0 //GPIO_PIN_11
// LED indicating a reception
#define LED_RX            GPIO_PIN_2 //GPIO_PIN_12
// LED indicating WiFi is ready
#define LED_WIFI_READY    GPIO_PIN_13
// Macro to output a value on the GPIOs
//#define GPIO_OUT(val)     (val ? gpio_bit_set(GPIOB, GPIO_PIN_13) : gpio_bit_reset(GPIOB, GPIO_PIN_13))
// Macro to enable LED
#define LED_ON(led)       gpio_bit_set(GPIOC, led)
// Macro to disable LED
#define LED_OFF(led)      gpio_bit_reset(GPIOC, led)

#ifdef CFG_LPS
    #define WLAN_WAKEUP_EXTI_LINE           EXTI_19
#elif  defined (CFG_PS_HW_WAKE)
    #define WLAN_WAKEUP_EXTI_LINE           EXTI_25
#endif

#define RTC_WAKEUP_EXTI_LINE                EXTI_21
#define LOG_USART_RX_PIN_EXTI_LINE          EXTI_7
#define BLE_WAKEUP_EXTI_LINE                EXTI_24

struct time_rtc {
    uint32_t tv_sec;         /* seconds */
    uint32_t tv_msec;        /* and miliseconds */
};

void ble_exti_enter(void);
void ble_exti_exit(void);
uint32_t ble_power_status_rise_status(void);
void ble_power_status_rise_clear(void);
uint32_t ble_power_status_fall_status(void);
void ble_power_status_fall_clear(void);
void ble_pmu_config(uint32_t enable);
void ble_wait_sleep_exit(uint32_t timeout_ms);
void ble_wakelock_acquire(void);
void ble_wakelock_release(void);
void ble_rcc_config(void);
void ble_power_status_en(void);
void ble_external_wakeup(void);
void ble_external_wakeup_clear(void);
void ble_power_on(void);
void ble_power_off(void);
void ble_irq_enable(void);
void ble_irq_disable(void);

void wlan_exti_enter(void);
void wlan_exti_exit(void);

void wifi_irq_enable(void);
void wifi_irq_disable(void);
void wifi_pmu_config(void);
void wifi_rcc_config(void);
void wifi_pll_reset(void);
int wifi_power_on(void);
void wifi_power_off(void);
void wifi_led_config(void);
bool wifi_is_not_idle(void);
bool wifi_hw_is_sleep(void);
void wifi_wakelock_acquire(void);
void wifi_wakelock_release(void);
void wifi_lpds_enter(void);
void wifi_lpds_exit(void);
void wifi_lpds_preconfig(uint8_t settle_time);

void hw_crc32_enable(void);
uint32_t hw_crc32_single(uint32_t data);
void hw_crc32_disable(void);

void deep_sleep_enter(uint16_t sleep_time);
void deep_sleep_exit(void);
void rtc_32k_time_get(struct time_rtc *cur_time, uint32_t is_wakeup);

int rf_power_on(void);
void rf_power_off(void);

void rf_pll_cal_trigger(void);


/**
 ****************************************************************************************
 * @brief Initialize WIFI reference platform.
 *
 * It initializes all platform specific drivers (IPC, CRM, DMA, ...)
 ****************************************************************************************
 */
void platform_init(void);

#endif  //_GD32VW55X_PLATFORM_H_
