/*!
    \file    gd32vw55x_platform.c
    \brief   WIFI reference platform initialization.

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

#include "app_cfg.h"
#ifdef CFG_WLAN_SUPPORT
#include "wlan_config.h"
#include "wifi_export.h"
#include "wifi_netlink.h"
#endif
#include "arch.h"
#include "gd32vw55x.h"
#include "systime.h"
#include "uart.h"
#include "rf_spi.h"
#include "hal_rf.h"
#include "rfi.h"
#include "dma.h"
#include "gd32vw55x_platform.h"
#include "wakelock.h"
#include "init_rom.h"
#include "nvds_flash.h"
#include "dbg_print.h"
#include "trng.h"
#include "trace_uart.h"
#include "log_uart.h"
#include "raw_flash_api.h"
#ifdef CFG_BLE_SUPPORT
#include "ble_export.h"
#endif

#ifdef CFG_BLE_HCI_MODE
#include "ble_uart.h"
#endif

// Indicate WiFi is existed or not (CFG_WLAN_SUPPORT)
uint8_t wifi_exist_flag = 0;

extern void system_clock_config(void);

__INLINE void hw_crypto_engine_enable(void)
{
    rcu_periph_clock_enable(RCU_PKCAU);
    rcu_periph_clock_enable(RCU_CAU);
    rcu_periph_clock_enable(RCU_HAU);
}

__INLINE void hw_crypto_engine_disable(void)
{
    rcu_periph_clock_disable(RCU_PKCAU);
    rcu_periph_clock_disable(RCU_CAU);
    rcu_periph_clock_disable(RCU_HAU);
}

/*!
    \brief      set lpds preconfig
    \param[in]  settle_time: time want to set
    \param[out] none
    \retval     none
*/
void wifi_lpds_preconfig(uint8_t settle_time)
{
    /* set HXTAL settle time */
    PMU_RFPAR  &= (~PMU_RFPAR_TIM1_PAR);
    PMU_RFPAR  |= (settle_time & PMU_RFPAR_TIM1_PAR);
}

void wifi_lpds_enter(void)
{
    PMU_CTL1 |= PMU_CTL1_WIFI_LPDS_ON;
}

void wifi_lpds_exit(void)
{
    PMU_CTL1 &= (~PMU_CTL1_WIFI_LPDS_ON);
    while (((PMU_RFCTL & PMU_RFCTL_RF_STATE) >> 16) != 0x08);
    // wait wifi pmu active
    while ((PMU_CS1 & PMU_CS1_WPS_ACTIVE) != PMU_CS1_WPS_ACTIVE);
    // wait wifi module pmc idle
    while (!(REG32(0x4002392C) & BIT(31)));
}

bool wifi_is_not_idle(void)
{
    return (REG32(0x4002392C) & BITS(28, 30)) != BITS(28, 30);
}

bool wifi_hw_is_sleep(void)
{
#ifdef CFG_WLAN_SUPPORT
    return (((PMU_RFCTL & PMU_RFCTL_RF_STATE) == 0x00) // RF state is idle
            && ((PMU_CS1 & PMU_CS1_WPS_SLEEP) == PMU_CS1_WPS_SLEEP) // wifi module is sleep
            );
#else
    return true;
#endif
}

/*!
    \brief      clear exti line
    \param[in]  line_no: EXTI line number, refer to exti_line_enum
                only one parameter can be selected which is shown as below:
      \arg        EXTI_x (x=0..28): EXTI line x
    \param[out] none
    \retval     none
*/
static void exti_line_clear(exti_line_enum line_no)
{
    /* disable interrupt mask */
    exti_interrupt_disable(line_no);

    /* clear pending interrupt bit */
    if (SET == exti_flag_get(line_no)) {
        exti_interrupt_flag_clear(line_no);
    }
}

/*!
    \brief      enter deep sleep mode
    \param[in]  sleep_time: sleep time(0x0000-0x7fff)
    \param[out] none
    \retval     none
*/
void deep_sleep_enter(uint16_t sleep_time)
{
    exti_init(LOG_USART_RX_PIN_EXTI_LINE, EXTI_INTERRUPT, EXTI_TRIG_RISING);

    // Wifi module enter exit by self
    // exti_init(WLAN_WAKEUP_EXTI_LINE, EXTI_INTERRUPT, EXTI_TRIG_RISING);

    exti_init(RTC_WAKEUP_EXTI_LINE, EXTI_INTERRUPT, EXTI_TRIG_RISING);

    rtc_flag_clear(RTC_STAT_WTF);
    rtc_wakeup_disable();

#if defined(RTC_CLOCK_SOURCE_IRC32K)
    rtc_wakeup_timer_set(sleep_time * 2);  /* unit: 500us */
#else
    rtc_wakeup_timer_set(sleep_time * 5); /* unit: 200us */
#endif

    rtc_wakeup_enable();

    hw_crypto_engine_disable();
    trng_close(0);

#ifdef LOG_UART
    /* wait usart transmition complete */
    while(RESET == usart_flag_get(LOG_UART, USART_FLAG_TC));
#endif

#ifdef TRACE_UART
    /* wait usart transmition complete */
    while(RESET == usart_flag_get(TRACE_UART, USART_FLAG_TC));
#endif

#ifdef HCI_UART
    /* wait usart transmition complete */
    while(RESET == usart_flag_get(HCI_UART, USART_FLAG_TC));
#endif

    pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_LOWDRIVER_ENABLE, WFI_CMD);
}

/*!
    \brief      exit deep sleep mode
    \param[in]  none
    \param[out] none
    \retval     none
*/
void deep_sleep_exit(void)
{
    GLOBAL_INT_STOP();
    if (rcu_system_clock_source_get() != RCU_SCSS_PLLDIG)
        system_clock_config();
    GLOBAL_INT_START();

    exti_line_clear(LOG_USART_RX_PIN_EXTI_LINE);
    // Wifi module clear exit by self
    // exti_line_clear(WLAN_WAKEUP_EXTI_LINE);
    exti_line_clear(RTC_WAKEUP_EXTI_LINE);

    #ifdef CFG_WLAN_SUPPORT
    if (wifi_netlink_status_get() == WIFI_RUNNING) {
        hw_crypto_engine_enable();
    }
    #endif

    if(RESET != rtc_flag_get(RTC_STAT_WTF)) {
        rtc_flag_clear(RTC_STAT_WTF);
    }
}

/*!
    \brief      enter deep sleep mode
    \param[in]  none
    \param[out] none
    \retval     none
*/
void wlan_exti_enter()
{
#ifdef CFG_LPS
    exti_init(WLAN_WAKEUP_EXTI_LINE, EXTI_INTERRUPT, EXTI_TRIG_RISING);
#endif
}

/*!
    \brief      exit deep sleep mode
    \param[in]  none
    \param[out] none
    \retval     none
*/
void wlan_exti_exit(void)
{
#ifdef CFG_LPS
    exti_line_clear(WLAN_WAKEUP_EXTI_LINE);
#endif
}

/*!
    \brief      ble enter deep sleep mode
*/
void ble_exti_enter(void)
{
    exti_init(BLE_WAKEUP_EXTI_LINE, EXTI_INTERRUPT, EXTI_TRIG_RISING);
}

/*!
    \brief      ble exit deep sleep mode
*/
void ble_exti_exit(void)
{
    exti_line_clear(BLE_WAKEUP_EXTI_LINE);
}

void ble_pmu_config(uint32_t enable)
{
    if (enable) {
        /* if ble is sleep, config power on */
        if (pmu_flag_get(PMU_FLAG_BLE_SLEEP) == SET)
            pmu_ble_control(PMU_BLE_WAKE);
        /* wait active */
        while (pmu_flag_get(PMU_FLAG_BLE_ACTIVE) != SET);
    } else {
        /* if ble is active, config power off */
        if (pmu_flag_get(PMU_FLAG_BLE_ACTIVE) == SET)
            pmu_ble_control(PMU_BLE_SLEEP);
        /* wait sleep */
        while (pmu_flag_get(PMU_FLAG_BLE_SLEEP) != SET);
    }
}

void ble_wait_sleep_exit(uint32_t timeout_ms)
{
#ifdef CFG_BLE_SUPPORT
    uint64_t start;

    if (timeout_ms > 0) {
        start = get_sys_local_time_us();
        while (ble_core_is_deep_sleep() && (get_sys_local_time_us() < start + timeout_ms * 1000));
    } else {
        while (ble_core_is_deep_sleep());
    }
#endif
}

void ble_rcc_config(void)
{
    /* ble enable */
    rcu_periph_clock_enable(RCU_BLE);

    /* ble reset */
    rcu_periph_reset_enable(RCU_BLERST);
    rcu_periph_reset_disable(RCU_BLERST);
}

void ble_power_status_en(void)
{
    pmu_interrupt_enable(PMU_INT_BLE_POWER_FALL);
    pmu_interrupt_enable(PMU_INT_BLE_POWER_RISE);
}

uint32_t ble_power_status_rise_status(void)
{
    return pmu_interrupt_flag_get(PMU_INT_FLAG_BLE_POWER_RISE) == SET;
}

void ble_power_status_rise_clear(void)
{
    pmu_interrupt_flag_clear(PMU_INT_FLAG_RESET_BLE_POWER_RISE);
}

uint32_t ble_power_status_fall_status(void)
{
    return pmu_interrupt_flag_get(PMU_INT_FLAG_BLE_POWER_FALL) == SET;
}

void ble_power_status_fall_clear(void)
{
    pmu_interrupt_flag_clear(PMU_INT_FLAG_RESET_BLE_POWER_FALL);
}

void ble_external_wakeup(void)
{
    pmu_ble_wakeup_request_enable();
}

void ble_external_wakeup_clear(void)
{
    pmu_ble_wakeup_request_disable();
}

uint32_t ble_sleep_measure_clock_get(void)
{
#ifdef CONFIG_PLATFORM_FPGA
    return rcu_clock_freq_get(CK_SYS);
#else
    return rcu_clock_freq_get(CK_APB1);
#endif
}

void ble_irq_enable(void)
{
    /* exti interrupt wake up the cpu from deep sleep state */
    eclic_irq_enable(BLE_WKUP_IRQn, 12, 0);

    /* ble ps requires low interrupt processing latency for pmu, the interrupt priority is higher than that of OS interrupt masking(configMAX_SYSCALL_INTERRUPT_PRIORITY) */
    eclic_irq_enable(BLE_POWER_STATUS_IRQn, 11, 0);

    eclic_irq_enable(BLE_SW_TRIG_IRQn, 8, 0);
    ECLIC_SetTrigIRQ(BLE_SW_TRIG_IRQn, ECLIC_POSTIVE_EDGE_TRIGGER);

    eclic_irq_enable(BLE_HALF_SLOT_IRQn, 8, 0);
    ECLIC_SetTrigIRQ(BLE_HALF_SLOT_IRQn, ECLIC_POSTIVE_EDGE_TRIGGER);

    eclic_irq_enable(BLE_SLEEP_MODE_IRQn, 8, 0);
    ECLIC_SetTrigIRQ(BLE_SLEEP_MODE_IRQn, ECLIC_POSTIVE_EDGE_TRIGGER);

    eclic_irq_enable(BLE_ENCRYPTION_ENGINE_IRQn, 8, 0);
    ECLIC_SetTrigIRQ(BLE_ENCRYPTION_ENGINE_IRQn, ECLIC_POSTIVE_EDGE_TRIGGER);

    eclic_irq_enable(BLE_FINE_TIMER_TARGET_IRQn, 8, 0);
    ECLIC_SetTrigIRQ(BLE_FINE_TIMER_TARGET_IRQn, ECLIC_POSTIVE_EDGE_TRIGGER);

    eclic_irq_enable(BLE_STAMP_TARGET1_IRQn, 8, 0);
    ECLIC_SetTrigIRQ(BLE_STAMP_TARGET1_IRQn, ECLIC_POSTIVE_EDGE_TRIGGER);

    eclic_irq_enable(BLE_STAMP_TARGET2_IRQn, 8, 0);
    ECLIC_SetTrigIRQ(BLE_STAMP_TARGET2_IRQn, ECLIC_POSTIVE_EDGE_TRIGGER);

    eclic_irq_enable(BLE_STAMP_TARGET3_IRQn, 8, 0);
    ECLIC_SetTrigIRQ(BLE_STAMP_TARGET3_IRQn, ECLIC_POSTIVE_EDGE_TRIGGER);

    eclic_irq_enable(BLE_FREQ_SELECT_IRQn, 8, 0);
    ECLIC_SetTrigIRQ(BLE_FREQ_SELECT_IRQn, ECLIC_POSTIVE_EDGE_TRIGGER);

    eclic_irq_enable(BLE_ERROR_IRQn, 8, 0);
    ECLIC_SetTrigIRQ(BLE_ERROR_IRQn, ECLIC_POSTIVE_EDGE_TRIGGER);

    eclic_irq_enable(BLE_FIFO_ACTIVITY_IRQn, 8, 0);
    ECLIC_SetTrigIRQ(BLE_FIFO_ACTIVITY_IRQn, ECLIC_POSTIVE_EDGE_TRIGGER);
}

void ble_irq_disable(void)
{
    eclic_irq_disable(BLE_WKUP_IRQn);

    eclic_irq_disable(BLE_POWER_STATUS_IRQn);

    eclic_irq_disable(BLE_SW_TRIG_IRQn);

    eclic_irq_disable(BLE_HALF_SLOT_IRQn);

    eclic_irq_disable(BLE_SLEEP_MODE_IRQn);

    eclic_irq_disable(BLE_ENCRYPTION_ENGINE_IRQn);

    eclic_irq_disable(BLE_FINE_TIMER_TARGET_IRQn);

    eclic_irq_disable(BLE_STAMP_TARGET1_IRQn);

    eclic_irq_disable(BLE_STAMP_TARGET2_IRQn);

    eclic_irq_disable(BLE_STAMP_TARGET3_IRQn);

    eclic_irq_disable(BLE_FREQ_SELECT_IRQn);

    eclic_irq_disable(BLE_ERROR_IRQn);

    eclic_irq_disable(BLE_FIFO_ACTIVITY_IRQn);
}

void ble_power_on(void)
{
    /* ble power on */
    ble_pmu_config(1);
    /* ble enable and reset */
    ble_rcc_config();
    /* ble ps enable, ble core sleep will generate power status interrupt */
    ble_power_status_en();
}

void ble_power_off(void)
{
    /* ble power off */
    ble_pmu_config(0);
    rcu_periph_clock_disable(RCU_BLE);
    ble_wakelock_release();
}

void hw_crc32_enable(void)
{
    rcu_periph_clock_enable(RCU_CRC);
    crc_data_register_reset();
}

uint32_t hw_crc32_single(uint32_t data)
{
    return crc_single_data_calculate(data);
}

void hw_crc32_disable(void)
{
    rcu_periph_clock_disable(RCU_CRC);
}

/*!
    \brief      acquire ble wakelock
    \param[in]  none
    \param[out] none
    \retval     none
*/
void ble_wakelock_acquire(void)
{
    sys_wakelock_acquire(LOCK_ID_BLE);
}

/*!
    \brief      release ble wakelock
    \param[in]  none
    \param[out] none
    \retval     none
*/
void ble_wakelock_release(void)
{
    sys_wakelock_release(LOCK_ID_BLE);
}

#ifdef CFG_WLAN_SUPPORT
/*!
    \brief      acquire wifi wakelock
    \param[in]  none
    \param[out] none
    \retval     none
*/
void wifi_wakelock_acquire(void)
{
    sys_wakelock_acquire(LOCK_ID_WLAN);
}

/*!
    \brief      release wifi wakelock
    \param[in]  none
    \param[out] none
    \retval     none
*/
void wifi_wakelock_release(void)
{
    sys_wakelock_release(LOCK_ID_WLAN);
}

#ifdef CONFIG_PLATFORM_ASIC
void wifi_pmu_config(void)
{
    /* PMU enable WLAN power */
    pmu_wifi_power_enable();
    /* wlan is in sleep state by default, set wlan_wake command to PMU to switch wlan power state */
    pmu_wifi_sram_control(PMU_WIFI_WAKE);

    /* polling PMU to confirm wlan is switched from sleep state */
    while (pmu_flag_get(PMU_FLAG_WIFI_ACTIVE) != SET) {
        pmu_wifi_sram_control(PMU_WIFI_WAKE);
        systick_udelay(50);
    }
}

void wifi_rcc_config(void)
{
    /* WIFI clock enable */
    rcu_periph_clock_enable(RCU_WIFI);

    /* WIFIRUN clock enable */
    rcu_periph_clock_enable(RCU_WIFIRUN);

    /* WIFI reset */
    rcu_periph_reset_enable(RCU_WIFIRST);
    rcu_periph_reset_disable(RCU_WIFIRST);
}

#else  /* CONFIG_PLATFORM_ASIC */

void wifi_pmu_config(void)
{
    /* PMU enable WLAN power */
    REG32(PMU + 0x08) |= BIT(1);
    /* wlan is in sleep state by default, set wlan_wake command to PMU to switch wlan power state */
    REG32(PMU + 0x08) |= BIT(3);

    /* polling PMU to confirm wlan is switched from sleep state */
    //while ((REG32(PMU + 0x0c) & BIT(2)) == 0) {
    //}
}

void wifi_rcc_config(void)
{
    /* wlan enable, wlan run enable */
    REG32(RCU + 0x30) |= 0x00006000;

    /* pwr */
    REG32(0x40007080) = 0x00040000;
}


void wifi_pll_reset(void)
{
    REG32(RCU + 0xa0) |= 0x00180000;
    REG32(RCU + 0xa0) &= (~0x00180000);
}

#endif  /* CONFIG_PLATFORM_ASIC */

void wifi_irq_enable(void)
{
    /* exti interrupt wake up the cpu from deep sleep state */
    eclic_irq_enable(WIFI_WKUP_IRQn, 12, 0);

    eclic_irq_enable(WIFI_INT_IRQn, 8, 0);
#ifdef GD32VW55X_WIFI_MUL_INTS
    eclic_irq_enable(WIFI_PROT_IRQn, 8, 0);
    eclic_irq_enable(WIFI_INTGEN_IRQn, 8, 0);
    eclic_irq_enable(WIFI_TX_IRQn, 8, 0);
    eclic_irq_enable(WIFI_RX_IRQn, 8, 0);
#endif
    eclic_irq_enable(LA_IRQn, 8, 0);

    // configure the interrupt controller
    intc_init();
}

void wifi_irq_disable(void)
{
    intc_deinit();

    eclic_irq_disable(WIFI_WKUP_IRQn);

    eclic_irq_disable(WIFI_INT_IRQn);
#ifdef GD32VW55X_WIFI_MUL_INTS
    eclic_irq_disable(WIFI_PROT_IRQn);
    eclic_irq_disable(WIFI_INTGEN_IRQn);
    eclic_irq_disable(WIFI_TX_IRQn);
    eclic_irq_disable(WIFI_RX_IRQn);
#endif
    eclic_irq_disable(LA_IRQn);
}

int wifi_power_on(void)
{
    // enable HardWare accelerate engine related RCU clock
    hw_crypto_engine_enable();

    // Enable wifi
    wifi_pmu_config();
    wifi_rcc_config();

    //wifi_irq_enable();

    return 0;
}

void wifi_power_off(void)
{
    /* disable wifi interrupts */
    //wifi_irq_disable();

    /* RCC disable wifi clock */
    rcu_periph_clock_disable(RCU_WIFI);
    rcu_periph_clock_disable(RCU_WIFIRUN);

    /* PMU set wifi to sleep state */
    pmu_wifi_sram_control(PMU_WIFI_SLEEP);
}

void wifi_led_config(void)
{
    /* enable the LED GPIO clock */
    rcu_periph_clock_enable(RCU_GPIOC);
    /* configure LED GPIO pin */
    // gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_RUN);
    // gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, LED_RUN);
    // gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_RX);
    // gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, LED_RX);
    // gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_SLEEP);
    // gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, LED_SLEEP);
    gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_WIFI_READY);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, LED_WIFI_READY);

    //LED_ON(LED_RUN);
}
#endif

#ifdef CONFIG_PLATFORM_ASIC
__INLINE void rf_rcc_config(void)
{
    /* polling RF DPLL ready */
    while ((RCU_CTL & RCU_CTL_PLLDIGSTB) == 0) {
    }

    /* RF clock enable */
    rcu_periph_clock_enable(RCU_RF);

    /* RF reset */
    rcu_periph_reset_enable(RCU_RFRST);
    rcu_periph_reset_disable(RCU_RFRST);

    /* RFI clock enable */
    rcu_periph_clock_enable(RCU_RFI);

    /* RFI reset */
    rcu_periph_reset_enable(RCU_RFIRST);
    rcu_periph_reset_disable(RCU_RFIRST);
}
#else /* CONFIG_PLATFORM_ASIC */
__INLINE void rf_rcc_config(void)
{
    /* RF PU & RF reset(bit29-30)*/
    REG32(RCU + 0x30) |= 0x60000000;

    // Wait RF PU stable
    systick_udelay(50);
}
#endif /* CONFIG_PLATFORM_ASIC */

void rf_pmu_par_config(void)
{
    /* set PMU_RFPAR_T1 to 0 so that RF XTAL up time is 1ms */
    PMU_RFPAR = PMU_RFPAR  & (~PMU_RFPAR_TIM1_PAR);
}

int rf_power_on(void)
{
#ifdef CONFIG_PLATFORM_FPGA
    // Enable SPI
    rf_spi_config();
#endif

    // Reset and enable RF PU
    rf_rcc_config();

    // rf reg adjuct for different crystal freq, rf pll enable is in system_clock_config().
    rf_enable_pll(HXTAL_VALUE / 1000000);

    rf_pmu_par_config();

    // init rfi
    rfi_init();

    // Add the initial configuration of the RF/ADC/DAC/etc.
    hal_init_rf();

    return 0;
}

void rf_power_off(void)
{
#ifdef CONFIG_PLATFORM_ASIC
    rcu_periph_clock_disable(RCU_RFI);
    rcu_periph_clock_disable(RCU_RF);
#endif /* CONFIG_PLATFORM_ASIC */
}

#ifdef CONFIG_PLATFORM_FPGA
#define RTC_CLOCK_SOURCE_HXTAL_DIV_RTCDIV
#else
#define RTC_CLOCK_SOURCE_IRC32K
#endif

__IO uint32_t prescaler_a = 0, prescaler_s = 0;

/*!
    \brief      configure RTC pre-configure
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void rtc_pre_config(void)
{
    #if defined(RTC_CLOCK_SOURCE_IRC32K)
        rcu_osci_on(RCU_IRC32K);
        rcu_osci_stab_wait(RCU_IRC32K);
        rcu_rtc_clock_config(RCU_RTCSRC_IRC32K);

        prescaler_s = 0x3E7;  // 0x13F;
        prescaler_a = 0x1F;  // 0x63;
    #elif defined(RTC_CLOCK_SOURCE_LXTAL)
        rcu_osci_on(RCU_LXTAL);
        rcu_osci_stab_wait(RCU_LXTAL);
        rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);

        prescaler_s = 0x3FF; // 1024
        prescaler_a = 0x1F;  // 32
    #elif defined(RTC_CLOCK_SOURCE_HXTAL_DIV_RTCDIV)
        rcu_osci_on(RCU_HXTAL);
        rcu_osci_stab_wait(RCU_HXTAL);
        prescaler_s = 0x3E7;  // 999;
        prescaler_a = 0x13;  // 19;
        /* HW fix pre-scaler 100*/
        /* rtc clock selection as (HXTAL/25)/100 = 20KHZ */
        rcu_rtc_clock_config(RCU_RTCSRC_HXTAL_DIV_RTCDIV);
        rcu_rtc_div_config(RCU_RTC_HXTAL_DIV25);
    #else
    #error RTC clock source should be defined.
    #endif  /* RTC_CLOCK_SOURCE_IRC32K */

    rcu_periph_clock_enable(RCU_RTC);
    rtc_register_sync_wait();
}

/*!
    \brief      get rtc 32k time
    \param[in]  cur_time: pointer to the input structure
                  tv_sec: second of current time
                  tv_msec: miliseconds of current time
     \param[in]  is_wakeup: wakeup state
    \param[out] none
    \retval     none
*/
void rtc_32k_time_get(struct time_rtc *cur_time, uint32_t is_wakeup)
{
    rtc_parameter_struct rtc_time;
    uint32_t second;
    uint32_t sub_second;

    if (is_wakeup) {
        rtc_register_sync_wait();
    }

    /* If directly call rtc_subsecond_get() function to get sub-second,
       it would unlock RTC_TIME and RTC_DATE by reading the RTC_DATE at the end.
       This could not protect the situation where we read the time just 59.999.
       as RTC_TIME may be updated to 00 at the next clock.
    */
    // Firstly we read RTC_SS to lock RTC_TIME and RTC_DATE,
    sub_second = RTC_SS;

    /* Then, we must call rtc_current_time_get here to read RTC_TIME && RTC_DATA
       and unlock them by the way
    */
    rtc_current_time_get(&rtc_time);

    second = ((rtc_time.second >> 4) * 10) + (rtc_time.second & 0x0f);

    cur_time->tv_sec = second;
    /* The sub-second(unit ms) calculated formula is as follow:
          sub_second = 1000*((float)(prescaler_s - RTC_SS)/(float)(prescaler_s + 1))
       We can use the simple formula if prescaler_s = 999.
    */
#ifdef RTC_CLOCK_SOURCE_LXTAL
    sub_second = 1000*((float)(prescaler_s - RTC_SS)/(float)(prescaler_s + 1));
#endif
    cur_time->tv_msec = prescaler_s - sub_second;
}

/*!
    \brief      trigger RF PLL calculation
    \param[in]  none
    \param[out] none
    \retval     none
*/
void rf_pll_cal_trigger(void)
{
    RCU_CFG1 &= (~RCU_CFG1_RFPLLCALEN);
    RCU_CFG1 |= RCU_CFG1_RFPLLCALEN;
}

/*!
    \brief      setup rtc
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void rtc_setup(void)
{
    rtc_parameter_struct rtc_initpara;

    /* setup RTC time value */
    uint32_t tmp_hh = 0, tmp_mm = 0, tmp_ss = 0;

    rtc_initpara.factor_asyn = prescaler_a;
    rtc_initpara.factor_syn = prescaler_s;
    rtc_initpara.year = 0x20;
    rtc_initpara.day_of_week = RTC_MONDAY;
    rtc_initpara.month = RTC_NOV;
    rtc_initpara.date = 0x2;
    rtc_initpara.display_format = RTC_24HOUR;
    rtc_initpara.am_pm = RTC_AM;
    rtc_initpara.hour = tmp_hh;
    rtc_initpara.minute = tmp_mm;
    rtc_initpara.second = tmp_ss;

    /* RTC current time configuration */
    if (ERROR == rtc_init(&rtc_initpara)) {
        printf("RTC time configuration failed!\r\n");
    } else {
        rtc_current_time_get(&rtc_initpara);
    }
}

/*!
    \brief      configure rtc 32k clock
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void rtc_32k_config(void)
{
    /* enable access to RTC registers in Backup domain */
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    rtc_pre_config();
    // /* 32K clock selection */
    // REG32(RCU + 0x70) = (REG32(RCU + 0x70) & 0xfffffcff) | 0x00000200;

    rtc_deinit();
    rtc_setup();

    /* clear wakeup timer occurs flag */
    rtc_flag_clear(RTC_STAT_WTF);
    /* RTC wakeup configuration */
    rtc_interrupt_enable(RTC_INT_WAKEUP);
#if defined(RTC_CLOCK_SOURCE_IRC32K)
    /* set wakeup clock as RTCCK_DIV16 */
    rtc_wakeup_clock_set(WAKEUP_RTCCK_DIV16);
#else
    /* set wakeup clock as WAKEUP_RTCCK_DIV4 = RTC CLOCK / 4 = 5KHZ*/
    rtc_wakeup_clock_set(WAKEUP_RTCCK_DIV4);
#endif
}

static void platform_uart_init()
{
    uart_driver_init();

#ifdef LOG_UART
    log_uart_init();
#endif

#ifdef CFG_BLE_HCI_MODE
    ble_uart_init();
#endif

#ifdef TRACE_UART
    trace_uart_init();
#endif
}

static void eclic_config(void)
{
    // disable all interrupts
    eclic_global_interrupt_disable();
    eclic_priority_group_set(ECLIC_PRIGROUP_LEVEL4_PRIO0);

    /* CLIC_INT_SFT,CLIC_INT_TMR set trigger mode */
    ECLIC_SetTrigIRQ(CLIC_INT_SFT, ECLIC_POSTIVE_EDGE_TRIGGER);
    ECLIC_SetTrigIRQ(CLIC_INT_TMR, ECLIC_POSTIVE_EDGE_TRIGGER);

    eclic_irq_enable(USART0_IRQn, 8, 0);

    eclic_irq_enable(UART1_IRQn, 8, 0);

#ifndef CFG_MATTER
    /* set trigger mode and interrupt level, same as arm priority. a larger interrupt priorities value in riscv indicates a higher priority */
    eclic_irq_enable(UART2_IRQn, 8, 0);
#endif

    /* exti interrupt wake up the cpu from deep sleep state, TODO: uart exti interrupts processing time is large, and the priority is lower */
    eclic_irq_enable(RTC_WKUP_IRQn, 12, 0);
    eclic_irq_enable(EXTI5_9_IRQn, 9, 0);

#ifdef TRACE_UART_DMA
    eclic_irq_enable(TRACE_DMA_IRQ_NUM, 8, 0);
#endif

#ifdef HCI_UART_RX_DMA
    eclic_irq_enable(HCI_DMA_IRQ_NUM, 8, 0);
#endif

}

void platform_init(void)
{
    eclic_config();
    systick_init();
    platform_uart_init();

#ifdef CONFIG_RF_TEST_SUPPORT
    printf("This firmware is for WiFi & BLE rf test.\r\n");
#endif
#ifdef CONFIG_SIGNALING_TEST_SUPPORT
    printf("This firmware is for WiFi signaling test.\r\n");
#endif
#ifdef CONFIG_BLE_DTM_SUPPORT
    printf("This firmware is for BLE DTM Signaling test\r\n");
#endif

    rcu_periph_clock_enable(RCU_PMU);
    rtc_32k_config();

    // initialize rom
    rom_init();

#ifdef CFG_WLAN_SUPPORT
#ifdef CONFIG_PLATFORM_FPGA
    // Before enable wifi, we need reset wifi pll to ensure wifi work on stable clock.
    wifi_pll_reset();
#endif
    wifi_led_config();
#ifdef CFG_DMA
    dma_config();
#endif
    sysctrl_init();
#endif

#ifdef CONFIG_FATFS_SUPPORT
    rcu_periph_clock_enable(RCU_CRC);
#endif

    rf_power_on();
    raw_flash_init();
    if (nvds_flash_internal_init())
        dbg_print(ERR, "nvds flash init failed\r\n");
}
