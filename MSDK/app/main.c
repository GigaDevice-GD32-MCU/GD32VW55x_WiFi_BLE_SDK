/*!
    \file    main.c
    \brief   Main loop of GD32VW55x SDK.

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

#include <stdint.h>
#include "wifi_export.h"
#include "gd32vw55x_platform.h"
#include "uart.h"
#include "ble_init.h"
#include "gd32vw55x.h"
#include "wrapper_os.h"
#include "cmd_shell.h"
#include "atcmd.h"
#include "util.h"
#include "wlan_config.h"
#include "wifi_init.h"
#include "user_setting.h"
#include "version.h"
#include "_build_date.h"
#include "config_gdm32.h"
#include "wifi_sta.h"
#ifdef CONFIG_FATFS_SUPPORT
#include "fatfs.h"
#endif
#include "ble_app_config.h"
#include "app_cmd.h"
#ifdef CONFIG_AZURE_IOT_SUPPORT
#include "azure_entry.h"
#endif

/*!
    \brief      Init applications.
                This function is called to initialize all the applications.
    \param[in]  none.
    \param[out] none.
    \retval     none.
*/
static void application_init(void)
{
#if defined CONFIG_BASECMD || defined CONFIG_RF_TEST_SUPPORT || defined CONFIG_BLE_DTM_SUPPORT
    if (cmd_shell_init()) {
        dbg_print(ERR, "cmd shell init failed\r\n");
    }
#endif
#ifdef CONFIG_ATCMD
    if (atcmd_init()) {
        dbg_print(ERR, "atcmd init failed\r\n");
    }
#endif
    util_init();

    user_setting_init();

#ifdef CFG_BLE_SUPPORT
#ifdef CONFIG_BLE_ALWAYS_ENABLE
    ble_init(true);
#else
    ble_init(false);
#endif  // CONFIG_BLE_DEFAULT_INIT
#endif  // CFG_BLE_SUPPORT

#ifdef CFG_WLAN_SUPPORT
    if (wifi_init()) {
        dbg_print(ERR, "wifi init failed\r\n");
    } else {
        if (wifi_sta_start() != 0) {
            dbg_print(ERR, "wifi sta start failed\r\n");
        }
    }
#endif
#ifdef CONFIG_FATFS_SUPPORT
#ifndef CONFIG_ATCMD
    fatfs_mk_mount(NULL);
#endif
#endif
#ifdef CFG_MATTER
    MatterInit();
#endif

#ifdef CONFIG_AZURE_F527_DEMO_SUPPORT
    azure_task_start();
#endif
}

#ifdef PLATFORM_OS_RTTHREAD
/*!
    \brief      Start task.
                This function is called to initialize all the applications in thread context.
    \param[in]  param parameter passed to the task
    \param[out] none
    \retval     none.
*/
static void start_task(void *param)
{
    (void)param;

    application_init();

    sys_task_delete(NULL);
}
#endif

/*!
    \brief      Main entry point.
                This function is called right after the booting process has completed.
    \param[in]  none
    \param[out] none
    \retval     none.
*/
int main(void)
{
    sys_os_init();
    platform_init();

    dbg_print(NOTICE, "SDK Version: %s\n", WIFI_GIT_REVISION);
    dbg_print(NOTICE, "Build date: %s\n", SDK_BUILD_DATE);
    dbg_print(NOTICE, "Image Version: %s%x.%x.%x.%03x\n",
            RE_CUSTOMER_NAME,
            (RE_IMG_VERSION >> 28),
            (RE_IMG_VERSION >> 20) & 0xFF,
            (RE_IMG_VERSION >> 12) & 0xFF,
            RE_IMG_VERSION & 0xFFF);


#ifdef PLATFORM_OS_RTTHREAD
    if (sys_task_create_dynamic((const uint8_t *)"start_task",
            START_TASK_STACK_SIZE, OS_TASK_PRIORITY(START_TASK_PRIO), start_task, NULL) == NULL) {
        dbg_print(ERR, "Create start task failed\r\n");
    }
#else
    application_init();
#endif

    sys_os_start();
}
