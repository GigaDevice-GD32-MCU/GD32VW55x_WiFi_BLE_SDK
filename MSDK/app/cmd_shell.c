/*!
    \file    cmd_shell.c
    \brief   Command shell for GD32VW55x SDK.

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

#include "config_gdm32.h"
#include <app_cfg.h>
#include "wlan_config.h"
#include "build_config.h"
#include "gd32vw55x.h"
#include "lwip/igmp.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "wifi_vif.h"
#include "wifi_net_ip.h"
#include "wifi_management.h"
#include "wifi_export.h"
#include "wifi_init.h"
#include "cmd_shell.h"
#include "dbg_print.h"
#include "uart.h"
#include "uart_config.h"
#include "ping.h"
#include "net_iperf.h"
#include "version.h"
#include "_build_date.h"
#include "log_uart.h"
#include "wakelock.h"
#include "trace_uart.h"
#include "rom_export.h"
#ifdef CONFIG_OTA_DEMO_SUPPORT
#include "ota_demo.h"
#endif
#ifdef CONFIG_MQTT
#include "mqtt_cmd.h"
#endif

#ifdef CFG_TRACE
#include "trace_ext.h"
#endif

#if defined(CONFIG_RF_TEST_SUPPORT) || defined(CONFIG_INTERNAL_DEBUG)
#include "cmd_rftest.h"
#endif

#if NVDS_FLASH_SUPPORT
#include "nvds_flash.h"
#endif

#ifdef CONFIG_FATFS_SUPPORT
#include "fatfs.h"
#endif

#ifdef CONFIG_COAP
#include "cmd_coap.c"
#endif

#if defined(CONFIG_INTERNAL_DEBUG)
#include "cmd_inner.c"
#endif

#ifdef CONFIG_IPERF3_TEST
#include "iperf3_main.c"
#endif

#ifdef CFG_8021x_EAP_TLS
#include "802_1x_EAP_TLS_certs.c"
#endif

#ifdef CONFIG_SOFTAP_PROVISIONING
#include "wifi_softap_provisioning.h"
#endif

#ifdef CONFIG_WIFI_MESH_SMART
#include "wifi_mesh_smart.h"
#endif

// CLI task message queue size
#define CLI_QUEUE_SIZE 3

struct cmd_module_info {
    enum cmd_mode_type cmd_mode;
    struct cmd_module_reg_info cmd_reg_infos[CMD_MODULE_MAX];
};

static cyclic_buf_t uart_cyc_buf;
static char uart_buf[UART_BUFFER_SIZE];
static uint32_t uart_index = 0;
static os_queue_t cmd_queue;
static struct cmd_module_info cmd_info;
static const struct cmd_entry cmd_table[];
static cmd_unkwn_handle_cb unkwn_cmd_handler = NULL;

int cmd_info_send(int id, void *msg_data, uint16_t len);
int cli_parse_ip4(char *str, uint32_t *ip, uint32_t *mask);

#ifdef CONFIG_IPERF_TEST
extern void cmd_iperf(int argc, char **argv);
#endif /* CONFIG_IPERF_TEST */

#ifdef CONFIG_IPERF3_TEST
extern void cmd_iperf3(int argc, char **argv);
#endif

#ifdef CONFIG_SSL_TEST
extern void cmd_ssl_client(int argc, char **argv);
#endif

#ifdef CONFIG_LWIP_SOCKETS_TEST
extern void cmd_lwip_sockets_client(int argc, char **argv);
extern void cmd_lwip_sockets_server(int argc, char **argv);
extern void cmd_lwip_sockets_close(int argc, char **argv);
extern void cmd_lwip_sockets_get_status(int argc, char **argv);
#endif

// scan callback function
static void cb_scan_done(void *eloop_data, void *user_ctx);
static void cb_scan_fail(void *eloop_data, void *user_ctx);
// connect callback function
static void cb_connect_success(void *eloop_data, void *user_ctx);
static void cb_connect_fail(void *eloop_data, void *user_ctx);

static void uart_cmd_rx_indicate(void)
{
    if (cmd_info_send(0x23, (void *)(&uart_cyc_buf), uart_index + 1) == 0) {
        if (cyclic_buf_write(&uart_cyc_buf, (uint8_t *)uart_buf, uart_index + 1) == false) {
            dbg_print(ERR, "uart cyclic buffer full\r\n");
        }
    } else {
        /* queue was full */
        dbg_print(ERR, "queue full\r\n");
        /* TODO: report 'message ignored' status */
    }

    uart_index = 0;
}

#undef isprint
#define in_range(c, lo, up)  ((uint8_t)c >= lo && (uint8_t)c <= up)
#define isprint(c)           in_range(c, 0x20, 0xff)
static void log_uart_rx_irq_hdl(uint32_t uart_port)
{
    uint8_t ch;
    usart_interrupt_disable(uart_port, USART_INT_RBNE);
    while (1) {
        // We should have chance to check overflow error
        // Otherwise it may cause dead loop handle rx interrupt
        if (RESET != usart_flag_get(uart_port, USART_FLAG_ORERR)) {
            usart_flag_clear(uart_port, USART_FLAG_ORERR);
        }

        if ((RESET != usart_flag_get(uart_port, USART_FLAG_RBNE))) {
            ch = usart_data_receive(uart_port);
        } else {
            break;
        }

        if (ch == '\0') {
            break;
        }

        if (isprint(ch)) {
            uart_buf[uart_index++] = ch;
            if (uart_index >= UART_BUFFER_SIZE) {
                uart_index = 0;
            }
            log_uart_putc_noint(ch);
        } else if (ch == '\r') { /* putty doesn't transmit '\n' */
            uart_buf[uart_index] = '\0';

            log_uart_putc_noint('\r');
            log_uart_putc_noint('\n');

            if (uart_index > 0) {
                uart_cmd_rx_indicate();
            } else {
                log_uart_putc_noint('#');
                log_uart_putc_noint(' ');
            }
            sys_wakelock_release(LOCK_ID_USART);
        } else if (ch == '\b') { /* non-destructive backspace */
            if (uart_index > 0) {
                uart_buf[--uart_index] = '\0';
            }
        }
    }

    usart_interrupt_enable(uart_port, USART_INT_RBNE);
}

void log_uart_rx_init(void)
{
    memset(uart_buf, 0, UART_BUFFER_SIZE);
    uart_index = 0;
    cyclic_buf_init(&uart_cyc_buf, 4 * UART_BUFFER_SIZE);
    uart_irq_callback_register(LOG_UART, log_uart_rx_irq_hdl);
}

static void uart_cmd_rx_handle_done(cyclic_buf_t *uart_cyc_buf, uint8_t *buf, uint16_t *len)
{
    if (*len > cyclic_buf_count(uart_cyc_buf)) {
        *len = cyclic_buf_count(uart_cyc_buf);
    }

    if (buf == NULL) {
        cyclic_buf_drop(uart_cyc_buf, *len);
    } else {
        cyclic_buf_read(uart_cyc_buf, buf, *len);
    }
}

/**
 ****************************************************************************************
 * @brief Convert string containing ip address
 *
 * The string may should be of the form a.b.c.d/e (/e being optional)
 *
 * @param[in]  str   String to parse
 * @param[out] ip    Updated with the numerical value of the ip address
 * @param[out] mask  Updated with the numerical value of the network mask
 *                   (or 32 if not present)
 * @return 0 if string contained what looks like a valid ip address and -1 otherwise
 ****************************************************************************************
 */
int cli_parse_ip4(char *str, uint32_t *ip, uint32_t *mask)
{
    char *token;
    uint32_t a, i, j;

    #define check_is_num(_str)  for (j = 0; j < strlen(_str); j++) \
        {                                                          \
            if (_str[j] < '0' || _str[j] > '9')                    \
            return -1;                                             \
        }

    // Check if mask is present
    token = strchr(str, '/');
    if (token && mask) {
        *token++ = '\0';
        check_is_num(token);
        a = atoi(token);
        if ((a == 0) || (a > 32))
            return -1;
        *mask = (1 << a) - 1;
    } else if (mask) {
        *mask = 0xffffffff;
    }

    // parse the ip part
    *ip = 0;
    for (i = 0; i < 4; i++) {
        if (i < 3) {
            token = strchr(str, '.');
            if (!token)
                return -1;
            *token++ = '\0';
        }
        check_is_num(str);
        a = atoi(str);
        if (a > 255)
            return -1;
        str = token;
        *ip += (a << (i * 8));
    }

    return 0;
}

/**
 ****************************************************************************************
 * @brief Convert string mac address
 *
 * The string may should be of the form xx:xx:xx:xx:xx:xx
 *
 * @param[in]  str   String to parse
 * @param[out] bssid BSSID
 * @return 0 if string contained what looks like a valid ip address and -1 otherwise
 ****************************************************************************************
 */
int cli_parse_macaddr(char *str, uint8_t *bssid)
{
    char *token;
    char *endptr = NULL;
    uint32_t a, i, j;

    #define check_is_hex(_str)  for (j = 0; j < 2; j++)            \
        {                                                          \
            if (!((_str[j] >= '0' && _str[j] <= '9')               \
                || (_str[j] >= 'a' && _str[j] <= 'f')              \
                || (_str[j] >= 'A' && _str[j] <= 'F'))) {          \
                return -1;                                         \
            }                                                      \
        }

    // parse the mac address
    for (i = 0; i < 6; i++) {
        if (i < 5) {
            token = strchr(str, ':');
            if (!token)
                return -1;
            *token++ = '\0';
        }
        check_is_hex(str);
        a = (uint32_t)strtoul(str, &endptr, 16);
        if (a > 255)
            return -1;

        bssid[i] = a;
        str = token;
    }

    return 0;
}

/**
 ****************************************************************************************
 * @brief Process function for 'list_cmd' command
 *
 * Simply list the commands sending a print message.
 *
 * @param[in] params Not used
 *
 * @return 0 on success and !=0 if error occurred
 ****************************************************************************************
 */
static void cmd_help(int argc, char **argv)
{
    uint8_t i;

    for (i = 0; cmd_table[i].function != NULL; i++) {
        app_print("%s\n", cmd_table[i].command);
    }

    return;
}

static void cmd_reboot(int argc, char **argv)
{
    printf("\r\n#");
    SysTimer_SoftwareReset();
}

static void cmd_version(int argc, char **argv)
{
    app_print("SDK Version: %s\n", WIFI_GIT_REVISION);
    app_print("SDK Build date: %s\n", SDK_BUILD_DATE);
    app_print("Image Version: %s%x.%x.%x.%03x\n",
            RE_CUSTOMER_NAME,
            (RE_IMG_VERSION >> 28),
            (RE_IMG_VERSION >> 20) & 0xFF,
            (RE_IMG_VERSION >> 12) & 0xFF,
            RE_IMG_VERSION & 0xFFF);
}

static void cmd_task_list(int argc, char **argv)
{
    app_print("TaskName\t\tState\tPri\tStack\tID\tStackBase\r\n");
    app_print("--------------------------------------------------\r\n");
    sys_task_list(NULL);
}

/**
 ****************************************************************************************
 * @brief Process function for 'free' command
 *
 * Provides information about memory usage.
 *
 * @param[in] params Not used
 * @return 0 on success and !=0 if error occurred
 ****************************************************************************************
 */
static void cmd_free(int argc, char **argv)
{
    extern void dump_mem_block_list(void);

    int total, used, free, max_used;

    sys_heap_info(&total, &free, &max_used);
    used = total - free;
    max_used = total - max_used;

    app_print("RTOS HEAP: free=%d used=%d max_used=%d/%d\n",
                free, used, max_used, total);

    dump_mem_block_list();

    return;
}

static void cmd_sys_ps(int argc, char **argv)
{
    uint8_t ps_mode;

    if (argc == 2) {
        ps_mode = atoi(argv[1]);
        if (ps_mode == 1) {
            sys_ps_set(SYS_PS_DEEP_SLEEP);
        } else if (ps_mode == 0) {
            sys_ps_set(SYS_PS_OFF);
        } else {
            goto Usage;
        }
        // info wifi system power mode change
#ifdef CFG_WLAN_SUPPORT
        wifi_core_task_resume(false);
#endif
        return;
    }
    ps_mode = sys_ps_get();

    app_print("Current power save mode: %d\n\r", ps_mode);

Usage:
    app_print("Usage: sys_ps [mode]\n\r");
    app_print("\tmode: 0: None, 1: CPU Deep Sleep\r\n");
}

static void cmd_ps_stats(int argc, char **argv)
{
    uint32_t cpu_sleep_time, cpu_stats_time;
    uint32_t sleep_int, sleep_frac;
    uint32_t stats_time, doze_time;
    uint32_t doze_int, doze_frac;

    if (argc != 1) {
        goto Usage;
    } else {
        sys_cpu_sleep_time_get(&cpu_stats_time, &cpu_sleep_time);
        app_print("cpu_sleep_time: %u\r\n", cpu_sleep_time);
        app_print("cpu_stats_time: %u\r\n", cpu_stats_time);
        sleep_int = ((cpu_sleep_time * 100) / cpu_stats_time);
        sleep_frac = (((cpu_sleep_time * 100) % cpu_stats_time) * 10) / cpu_stats_time;
        app_print("cpu sleep: %u.%u\r\n", sleep_int, sleep_frac);
#ifdef CFG_WLAN_SUPPORT
        wifi_netlink_sys_stats_get(&doze_time, &stats_time);
        app_print("doze_time: %u\r\n", doze_time);
        app_print("stats_time: %u\r\n", stats_time);
        doze_int = ((doze_time * 100) / stats_time);
        doze_frac = (((doze_time * 100) % stats_time) * 10) / stats_time;
        app_print("wifi doze: %u.%u\r\n", doze_int, doze_frac);
#endif
    }
    return;

Usage:
    app_print("Usage: ps_stats\n\r");
}

static void cmd_xmodem(int argc, char **argv)
{
    int ret;
    uint8_t xmodem_flag = 1;

    ret = rom_sys_status_set(SYS_XMODEM_FLAG, 1, &xmodem_flag);
    if (ret < 0) {
        app_print("rom sys status set fail\r\n");
        return;
    }
    printf("\r\n#");
    SysTimer_SoftwareReset();
}

/**
 ****************************************************************************************
 * @brief Process function for 'cpu_stats' command
 *
 * @param[in] params Not used
 * @return 0 on success and !=0 if error occurred
 ****************************************************************************************
 */
static void cmd_cpu_stats(int argc, char **argv)
{
    sys_cpu_stats();
}

static void cmd_read_memory(int argc, char **argv)
{
    char *endptr = NULL;
    uint32_t addr = 0;
    uint32_t count = 4;
    uint32_t width = 4;

    if (argc > 1) {
        addr = (uint32_t)strtoul((const char *)argv[1], &endptr, 16);
        if (*endptr != '\0') {
            app_print("rmem: invalid address\r\n");
            goto exit;
        }
    }
    if (argc > 2) {
        count = (uint32_t)strtoul((const char *)argv[2], &endptr, 0);
        if (*endptr != '\0') {
            app_print("rmem: invalid count\r\n");
            goto exit;
        }
    }
    if (argc > 3) {
        width = (uint32_t)strtoul((const char *)argv[3], &endptr, 0);
        if (*endptr != '\0') {
            app_print("rmem: invalid width\r\n");
            goto exit;
        }
    }

    print_buffer(addr, (void *)addr, width, count, 0);
    return;
exit:
    app_print("Usage: rmem <addr> [count] [width]\r\n");
}

#if LWIP_STATS && LWIP_STATS_DISPLAY
static void cmd_lwip_stats(int argc, char **argv)
{
    stats_display();
}
#endif

static void cmd_group_join(int argc, char **argv)
{
    if (argc == 2) {
        int vif_idx = WIFI_VIF_INDEX_DEFAULT;
        ip4_addr_t group_ip;
        struct netif *net_if = NULL;

        if (inet_aton((char *)argv[1], (struct in_addr*)&group_ip) == 0) {
            app_print("\rCan not join group because of group IP error\r\n");
            goto usage;
        }

        if (!ip4_addr_ismulticast(&group_ip)) {
            app_print("ip is not a multicast ip\r\n");
            goto usage;
        }

        net_if = (struct netif *)vif_idx_to_net_if(vif_idx);
        if (!net_if) {
            app_print("no netif found for interface:%d", vif_idx);
            return;
        }
        if (net_dhcp_address_obtained(net_if) || net_if_is_static_ip()) {
            igmp_joingroup((const ip4_addr_t *)&net_if->ip_addr, (const ip4_addr_t *)&group_ip);
        } else {
            app_print("Can not join group because IP not got\r\n");
        }
        return;
    }
usage:
    app_print("Usage: join_group <group ip eg:224.0.0.5>\r\n");
}

static void cmd_wifi_debug(int argc, char **argv)
{
    char *endptr = NULL;
    uint32_t enable = 0;

    if (argc == 2) {
        enable = (uint32_t)strtoul((const char *)argv[1], &endptr, 0);
        if (*endptr != '\0') {
            goto usage;
        }
        if (enable != 0 && enable != 1)
            goto usage;
        if (enable)
            wifi_netlink_dbg_open();
        else
            wifi_netlink_dbg_close();
    } else {
        goto usage;
    }
    return;

usage:
    app_print("Usage: wifi_debug <0 or 1>\r\n");
}

static void cmd_wifi_open(int argc, char **argv)
{
    wifi_netlink_wifi_open();
}

static void cmd_wifi_close(int argc, char **argv)
{
    wifi_netlink_wifi_close();
}

static void cmd_wifi_mac_addr(int argc, char **argv)
{
    uint8_t *addr;
    uint8_t user_addr[WIFI_ALEN];

    if(argc == 1) {
        addr = wifi_vif_mac_addr_get(WIFI_VIF_INDEX_DEFAULT);
        app_print("Wi-Fi MAC Address: "MAC_FMT"\r\n", MAC_ARG_UINT8(addr));
        goto Usage;
    } else if(argc == 2) {
        if (cli_parse_macaddr(argv[1], user_addr)) {
            app_print("MAC address is not valid.\r\n");
            goto Usage;
        }

        app_print("User MAC: "MAC_FMT"\r\n", MAC_ARG_UINT8(user_addr));
        if (user_addr[0] & 0x01) {
            app_print("The LSB of the first byte of the MAC must be 0.\r\n" );
            return;
        }
        wifi_vif_user_addr_set(user_addr);
        app_print("Please enter wifi_close and wifi_open to take effect.\r\n");
    } else {
        goto Usage;
    }
    return;
Usage:
    app_print("\rUsage: wifi_mac_addr [xx:xx:xx:xx:xx:xx]\r\n");
}

#ifdef CFG_WIFI_CONCURRENT
static void cmd_wifi_concurrent(int argc, char **argv)
{
    char *endptr = NULL;
    uint32_t enable = 0;

    if (argc == 2) {
        enable = (uint32_t)strtoul((const char *)argv[1], &endptr, 0);
        if (*endptr != '\0') {
            goto usage;
        }
        if (enable != 0 && enable != 1) {
            goto usage;
        }
        wifi_management_concurrent_set(enable);
    } else if (argc == 1) {
        app_print("wifi concurrent mode %d\r\n", wifi_management_concurrent_get());
    } else {
        goto usage;
    }
    return;

usage:
    app_print("Usage: wifi_concurrent [0 or 1]\r\n");
}
#endif /* CFG_WIFI_CONCURRENT */

static void cb_scan_done(void *eloop_data, void *user_ctx)
{
    app_print("WIFI_SCAN: done\r\n");
    wifi_netlink_scan_results_print(WIFI_VIF_INDEX_DEFAULT, wifi_netlink_scan_result_print);
    eloop_event_unregister(WIFI_MGMT_EVENT_SCAN_DONE, cb_scan_done);
    eloop_event_unregister(WIFI_MGMT_EVENT_SCAN_FAIL, cb_scan_fail);
}

static void cb_scan_fail(void *eloop_data, void *user_ctx)
{
    app_print("WIFI_SCAN: failed\r\n");
    eloop_event_unregister(WIFI_MGMT_EVENT_SCAN_DONE, cb_scan_done);
    eloop_event_unregister(WIFI_MGMT_EVENT_SCAN_FAIL, cb_scan_fail);
}

static void cmd_wifi_scan(int argc, char **argv)
{
#if 1
    eloop_event_register(WIFI_MGMT_EVENT_SCAN_DONE, cb_scan_done, NULL, NULL);
    eloop_event_register(WIFI_MGMT_EVENT_SCAN_FAIL, cb_scan_fail, NULL, NULL);

    if (wifi_management_scan(false, NULL)) {
        eloop_event_unregister(WIFI_MGMT_EVENT_SCAN_DONE, cb_scan_done);
        eloop_event_unregister(WIFI_MGMT_EVENT_SCAN_FAIL, cb_scan_fail);
        app_print("Wifi scan failed\r\n");
    }
#else
    if (wifi_management_scan(true, NULL) != 0) {
        app_print("start wifi_scan failed\r\n");
    } else {
        wifi_netlink_scan_results_print(WIFI_VIF_INDEX_DEFAULT, wifi_netlink_scan_result_print);
    }
#endif
}

/**
 ****************************************************************************************
 * @brief Process function for 'connect' command
 *
 * Start connection to an AP in a separated task.
 * @verbatim
   wifi_connect <SSID> [password]
   @endverbatim
 *
 * @param[in] params <ssid> [password]
 * @return
 ****************************************************************************************
 */
static void cb_connect_success(void *eloop_data, void *user_ctx)
{
    app_print("WIFI_CONNECT: success\r\n");

    eloop_event_unregister(WIFI_MGMT_EVENT_CONNECT_SUCCESS, cb_connect_success);
    eloop_event_unregister(WIFI_MGMT_EVENT_CONNECT_FAIL, cb_connect_fail);
}

static void cb_connect_fail(void *eloop_data, void *user_ctx)
{
    app_print("WIFI_CONNECT: fail\r\n");

    eloop_event_unregister(WIFI_MGMT_EVENT_CONNECT_SUCCESS, cb_connect_success);
    eloop_event_unregister(WIFI_MGMT_EVENT_CONNECT_FAIL, cb_connect_fail);
}

static void cmd_wifi_connect(int argc, char **argv)
{
    int status = 0;
    char *ssid;
    char *password;

    if(argc == 2) {
        ssid = argv[1];
        password = NULL;
    } else if(argc == 3) {
        ssid = argv[1];
        password = argv[2];
    } else {
        app_print("\rUsage: wifi_connect <SSID> [PASSWORD]\r\n");
        return;
    }
#if 1
    status = wifi_management_connect(ssid, password, true);
#else
    eloop_event_register(WIFI_MGMT_EVENT_CONNECT_SUCCESS, cb_connect_success, NULL, NULL);
    eloop_event_register(WIFI_MGMT_EVENT_CONNECT_FAIL, cb_connect_fail, NULL, NULL);

    status = wifi_management_connect(ssid, password, false);
#endif

    if (status != 0) {
        app_print("start wifi_connect failed %d\r\n", status);
    }
}

static void cmd_wifi_connect_bssid(int argc, char **argv)
{
    int status = 0;
    char *string_bssid;
    uint8_t bssid[WIFI_ALEN];
    char *password;

    if (argc == 2) {
        string_bssid = argv[1];
        password = NULL;
    } else if (argc == 3) {
        string_bssid = argv[1];
        password = argv[2];
    } else {
        goto Usage;
    }
    if (cli_parse_macaddr(string_bssid, bssid)) {
        app_print("BSSID is not valid.\r\n");
        goto Usage;
    }

    app_print("bssid: "MAC_FMT"\r\n", MAC_ARG_UINT8(bssid));
    if (bssid[0] & 0x01) {
        app_print("The LSB of the first byte of the BSSID must be 0.\r\n" );
        return;
    }
    status = wifi_management_connect_with_bssid(bssid, password, true);

    if (status != 0) {
        app_print("start wifi_connect_bssid failed %d\r\n", status);
    }
    return;

Usage:
    app_print("\rUsage: wifi_connect_bssid <BSSID> [PASSWORD]\r\n");
}

#ifdef CFG_8021x_EAP_TLS
static void cmd_wifi_connect_eap_tls(int argc, char **argv)
{
    int status = 0;
    char *ssid;

    if (argc == 2) {
        ssid = argv[1];
    } else {
        app_print("\rUsage: wifi_connect_eap_tls <SSID>\r\n");
        return;
    }

    status = wifi_management_connect_with_eap_tls(ssid, identity, ca_cert,
                                                client_key, client_cert,
                                                client_key_password, phase1, true);

    if (status != 0) {
        app_print("start wifi_connect_eap_tls failed %d\r\n", status);
    }
}
#endif

static void cmd_wifi_disconnect(int argc, char **argv)
{
    wifi_management_disconnect();
    return;
}

/**
 ****************************************************************************************
 * @brief Process function for 'wifi_status' command
 *
 * @param[in] params not used
 *
 * @return
 ****************************************************************************************
 */
static void cmd_wifi_status(int argc, char **argv)
{
    wifi_netlink_status_print();
}

/**
 ****************************************************************************************
 * @brief Process function for 'wifi_set_ip' command
 *
   @verbatim
      wifi_set_ip dhcp
      wifi_set_ip <ip> <gw>
   @endverbatim
 *
 * @param[in] params 'dhcp' | <ip> <gw>
 * @return
 ****************************************************************************************
 */
static void cmd_wifi_ip_set(int argc, char **argv)
{
    int vif_idx = WIFI_VIF_INDEX_DEFAULT;
    struct wifi_vif_tag *wvif = vif_idx_to_wvif(vif_idx);
    struct wifi_ip_addr_cfg ip_cfg;

    if (argc == 2) {
        if (strcmp(argv[1], "dhcp") == 0) {
            /* Only STA mode */
            if (wvif->wvif_type != WVIF_STA) {
                app_print("wifi_set_ip: only for STA mode\r\n");
                goto usage;
            }
            /* if ip has been get before, clear it and get a new one */
            net_if_use_static_ip(false);
            ip_cfg.mode = IP_ADDR_NONE;
            wifi_set_vif_ip(vif_idx, &ip_cfg);

            ip_cfg.mode = IP_ADDR_DHCP_CLIENT;
            ip_cfg.default_output = true;
            ip_cfg.dhcp.to_ms = VIF_DHCP_TIMEOUT;

            if (wifi_set_vif_ip(vif_idx, &ip_cfg)) {
                app_print("wifi_set_ip: dhcpc failed\n");
            }
        } else {
            goto usage;
        }
    } else if (argc == 3) {
        /* Only STA mode */
        if (wvif->wvif_type != WVIF_STA) {
            app_print("wifi_set_ip: only for STA mode\r\n");
            goto usage;
        }
        app_print("wifi_set_ip: set ip addr:%s, gate_way:%s\r\n", (char *)argv[1], (char *)argv[2]);
        ip_cfg.mode = IP_ADDR_STATIC_IPV4;
        ip_cfg.ipv4.dns = 0;
        net_if_use_static_ip(true);

        if (cli_parse_ip4((char *)argv[1], &ip_cfg.ipv4.addr, &ip_cfg.ipv4.mask) != 0)
            goto usage;

        if (cli_parse_ip4((char *)argv[2], &ip_cfg.ipv4.gw, NULL))
            goto usage;

        if (wifi_set_vif_ip(vif_idx, &ip_cfg)) {
            app_print("wifi_set_ip: failed to set ip\r\n");
        }
    } else if (argc == 4) {
        if (strcmp(argv[1], "dhcpd") == 0) {
            /* Only SoftAP mode */
            if (wvif->wvif_type != WVIF_AP) {
                app_print("wifi_set_ip: only for AP mode\r\n");
                goto usage;
            }

            ip_cfg.mode = IP_ADDR_DHCP_SERVER;
            if (cli_parse_ip4((char *)argv[2], &ip_cfg.ipv4.addr, &ip_cfg.ipv4.mask) != 0)
                goto usage;

            if (cli_parse_ip4((char *)argv[3], &ip_cfg.ipv4.gw, NULL))
                goto usage;

            if (wifi_set_vif_ip(vif_idx, &ip_cfg)) {
                app_print("wifi_set_ip: failed to set dhcpd\r\n");
            }
        } else {
            goto err_input;
        }
    } else {
        goto err_input;
    }
    return;

err_input:
    app_print("wifi_set_ip: invalid input\r\n");
usage:
    app_print("Usage: wifi_set_ip dhcp | <ip_addr/mask_bits> <gate_way> | dhcpd <ip_addr/mask_bits> <gate_way>\r\n");
    app_print("\tdhcp: get ip by start dhcp, only for STA mode\r\n");
    app_print("\tip_addr: ipv4 addr to set.\r\n");
    app_print("\tgate_way: gate way to set.\r\n");
    app_print("\tdhcpd: use new ip addr to restart dhcp server, only for SoftAP mode\r\n");
    app_print("Example: wifi_set_ip 192.168.0.123/24 192.168.0.1\r\n");
    app_print("         wifi_set_ip dhcp\r\n");
    app_print("         wifi_set_ip dhcpd 192.168.0.1/24 192.168.0.1\r\n");
    return;
}

/**
 ****************************************************************************************
 * @brief Process function for 'wifi_auto_conn' command
 *
   @verbatim
      wifi_auto_conn 1/0 or wifi_auto_conn
   @endverbatim
 *
 * @param[in] [auto_conn]
 * @return
 ****************************************************************************************
 */
static void cmd_wifi_auto_conn(int argc, char **argv)
{
    char *endptr = NULL;
    uint32_t enable = 0;

    if (argc == 2) {
        enable = (uint32_t)strtoul((const char *)argv[1], &endptr, 0);
        if (*endptr != '\0') {
            goto usage;
        }
        if (enable != 0 && enable != 1) {
            goto usage;
        }
        wifi_netlink_auto_conn_set(enable);
    } else if (argc == 1) {
        app_print("Current wifi auto conn %d\r\n", wifi_netlink_auto_conn_get());
        return;
    } else {
        goto usage;
    }
    return;

usage:
    app_print("Usage: wifi_auto_conn [0 or 1]\r\n");
}

/**
 ****************************************************************************************
 * @brief Process function for 'wifi_wireless_mode' command
 *
   @verbatim
      wifi_wireless_mode bg/bgn/bgn_ax or wifi_wireless_mode
   @endverbatim
 *
 * @param[in] [mode]
 * @return
 ****************************************************************************************
 */
static void cmd_wifi_wireless_mode(int argc, char **argv)
{
    uint32_t wireless_mode = WIRELESS_MODE_UNKNOWN;
    uint32_t vif_idx = WIFI_VIF_INDEX_DEFAULT;
    struct wifi_vif_tag *wvif = vif_idx_to_wvif(vif_idx);

    if (wvif->wvif_type != WVIF_STA) {
        app_print("Only for STA mode.\r\n");
        return;
    }

    if (argc == 2) {
        if (strncmp(argv[1], "bgnax", 5) == 0) {
            wireless_mode = WIRELESS_MODE_11BGN_AX;
        } else if (strncmp(argv[1], "bgn", 3) == 0) {
            wireless_mode = WIRELESS_MODE_11BGN;
        } else if (strncmp(argv[1], "bg", 2) == 0) {
            wireless_mode = WIRELESS_MODE_11BG;
        }
        if (wireless_mode == WIRELESS_MODE_UNKNOWN) {
            app_print("Input wireless mode error.\r\n");
            goto Usage;
        }
        macif_vif_wireless_mode_set(wireless_mode);
        app_print("Please enter wifi_close and wifi_open to take effect.\r\n");
    } else if (argc == 1) {
        wireless_mode = macif_vif_wireless_mode_get(vif_idx);
        app_print("Current wireless mode: ");
        wifi_netlink_wireless_mode_print(wireless_mode);
        return;
    } else {
        goto Usage;
    }
    return;

Usage:
    app_print("Usage: wifi_wireless_mode [bg or bgn or bgnax]\r\n");
}

/**
 ****************************************************************************************
 * @brief Process function for 'wifi_roaming' command
 *
   @verbatim
      wifi_roaming 1/0 rss_threshold or wifi_roaming
   @endverbatim
 *
 * @param[in] [enable] [rssi_threshold]
 * @return
 ****************************************************************************************
 */
static void cmd_wifi_roaming(int argc, char **argv)
{
    uint32_t vif_idx = WIFI_VIF_INDEX_DEFAULT;
    struct wifi_vif_tag *wvif = vif_idx_to_wvif(vif_idx);
    uint8_t enable = 0;
    int8_t rssi_th = 0;

    if (wvif->wvif_type != WVIF_STA) {
        app_print("Only for STA mode.\r\n");
        return;
    }

    if (argc >= 3) {
        rssi_th = atoi(argv[2]);
        if (rssi_th >= 0) {
            app_print("RSSI threshold must be less than 0.\r\n");
            return;
        }
    }

    if (argc >= 2) {
        enable = atoi(argv[1]) ? 1 : 0;
        wifi_management_roaming_set(enable, rssi_th);
    } else if (argc == 1) {
        enable = wifi_management_roaming_get(&rssi_th);
        if (enable)
            app_print("wifi roaming enable: 1, rssi th %d\r\n", rssi_th);
        else
            app_print("wifi roaming enable: 0\r\n");
        goto Usage;
    }
    return;

Usage:
    app_print("Usage: wifi_roaming [enable] [rssi_threshold]\r\n");
    app_print("Example: wifi_roaming 1 -70\r\n");
}

/**
 ****************************************************************************************
 * @brief Process function for 'wifi_ps' command
 *
 * Enable/disable power save mode.
 * @verbatim
   wifi_ps <mode>
   @endverbatim
 *
 * @param[in] params  PS parameters
 * @return none
 ****************************************************************************************
 */
static void cmd_wifi_ps(int argc, char **argv)
{
    uint32_t mode_set;
    uint32_t vif_idx = WIFI_VIF_INDEX_DEFAULT;
    struct wifi_vif_tag *wvif = vif_idx_to_wvif(vif_idx);

    if (wvif->wvif_type != WVIF_STA) {
        app_print("Not STA mode, do nothing.\r\n");
        return;
    }

    do {
        if (argc < 2)
            break;

        mode_set = atoi(argv[1]);
        if (wifi_netlink_ps_mode_set(vif_idx, mode_set))
            app_print("wifi_ps: set failed\r\n");
        return;

    } while(0);

    app_print("Current ps mode: %u\r\n\r\n", wvif->sta.psmode);

    app_print("Usage: wifi_ps [mode]\n\r");
    app_print("\tmode: 0: off, 1: always on, 2: based on traffic detection\r\n");
}

/**
 ****************************************************************************************
 * @brief Process function for 'wifi_listen_interval' command
 *
 * set listen beacon interval in power save mode
 * @verbatim
   wifi_listen_interval [interval]
   @endverbatim
 *
 * @param[in] params  listen interval
 * @return none
 ****************************************************************************************
 */
static void cmd_wifi_listen_interval_set(int argc, char **argv)
{
    uint32_t listen_interval;

    do {
        if (argc < 2)
            break;

        listen_interval = atoi(argv[1]);
        if (listen_interval > 10) {
            app_print("listen interval is too large\r\n");
            break;
        }

        if (wifi_netlink_listen_interval_set(listen_interval))
            app_print("wifi_listen_interval: set failed\r\n");

        return;
    } while(0);

    app_print("Usage: wifi_listen_interval [interval]\n\r");
    app_print("\tinterval: 0: listen beacon by dtim, 1 - 10 , the interval of listen beacon\r\n");
}

/**
 ****************************************************************************************
 * @brief Process function for 'wifi_ap' command
 *
 * Start an AP
 * @verbatim
   wifi_ap <ssid> <password> <channel> [-a <akm>[,<akm 2>]] [-hide <hide_ap>]
   @endverbatim
 * @param[in] params <ssid> <password> <channel> [-a <akm>[,<akm 2>]] [-hide <hide_ap>]
 * @return
 ****************************************************************************************
 */
#ifdef CFG_SOFTAP
static void cmd_wifi_ap(int argc, char **argv)
{
    char *ssid = NULL;
    char *password = NULL;
    uint32_t passwd_len = 0;
    uint8_t channel = 1, is_hidden = 0;
    wifi_ap_auth_mode_t auth_mode = AUTH_MODE_UNKNOWN;
    char *option, *akm_str = NULL;
    int arg_idx = 4;

    if ((argc < 4) || (argc & 1) == 1) {
        goto usage;
    }
    ssid = argv[1];
    password = argv[2];
    channel = atoi(argv[3]);

    while (arg_idx < argc) {
        option = argv[arg_idx];
        if (!strcmp("-a", option)) {
            akm_str = argv[arg_idx + 1];
        } else if (!strcmp("-hide", option)) {
            is_hidden = atoi(argv[arg_idx + 1]) > 0 ? 1 : 0;
        }
        arg_idx += 2;
    }

    passwd_len = strlen(password);
    if ((strlen(ssid) > WIFI_SSID_MAX_LEN)
        || (passwd_len < WPA_MIN_PSK_LEN && strcmp("NULL", password))
        || (passwd_len > WPA_MAX_PSK_LEN))
        goto usage;

    if (channel > 13 || channel < 1)
        goto usage;

    if (akm_str) {
        if (!strcmp(akm_str, "open"))
            auth_mode = AUTH_MODE_OPEN;
        else if (!strcmp(akm_str, "wpa2"))
            auth_mode = AUTH_MODE_WPA2;
        else if (!strcmp(akm_str, "wpa3"))
            auth_mode = AUTH_MODE_WPA3;
        else if (!strcmp(akm_str, "wpa2,wpa3") || !strcmp(akm_str, "wpa3,wpa2"))
            auth_mode = AUTH_MODE_WPA2_WPA3;
        else
            goto usage;
    }

    if (!strcmp(password, "NULL")) {
        auth_mode = AUTH_MODE_OPEN;
        password = NULL;
    } else if (auth_mode == AUTH_MODE_UNKNOWN || auth_mode == AUTH_MODE_OPEN) {
        // default use wpa2 mode;
        // or password has been entered, but a '-a open' followed, ignore -a option.
        auth_mode = AUTH_MODE_WPA2;
    }

    if (wifi_management_ap_start(ssid, password, channel, auth_mode, is_hidden)) {
        app_print("Failed to start AP, check your configuration.\r\n");
        return;
    }

    app_print("SoftAP successfully started!\r\n");
    return;
usage:
    app_print("Usage: wifi_ap <ssid> <password> <channel> [-a <akm>[,<akm 2>]] [-hide <hide_ap>]\r\n");
    app_print("<ssid>: The length should be between 1 and 32.\r\n");
    app_print("<password>: The length should be between 8 and 63, but can be \"NULL\" indicates open ap.\r\n");
    app_print("<channel>: 1~13.\r\n");
    app_print("[-a <akm>[,<akm 2>]]: only support following 5 AKM units: open; wpa2; wpa3; wpa2,wpa3 or wpa3,wpa2.\r\n");
    app_print("[-hide <hide_ap>]: 0 or 1, default 0.\r\n");
    app_print("For example:\r\n");
    app_print("    wifi_ap test_ap 12345678 1 -a wpa3 -hide 0, means a wpa3 ap in channel 1 and can broadcast ssid.\r\n");
    app_print("    wifi_ap test_ap NULL 5, means an open ap in channel 5.\r\n");
    app_print("    wifi_ap test_ap 12345678 11, means a wpa2 ap in channel 11, default wpa2.\r\n");
}

extern uint32_t dhcpd_find_ipaddr_by_macaddr(uint8_t *mac_addr);
static void cmd_wifi_ap_client_delete(int argc, char **argv)
{
    char *string_addr;
    uint8_t client_mac_addr[WIFI_ALEN];
    int vif_idx = WIFI_VIF_INDEX_DEFAULT;
    int ret = 0;

    if (argc == 2) {
        string_addr = argv[1];
    } else {
        goto Usage;
    }

    if (cli_parse_macaddr(string_addr, client_mac_addr)) {
        app_print("Client mac addr is not valid.\r\n");
        goto Usage;
    }
    app_print("Client mac addr: "MAC_FMT"\r\n", MAC_ARG_UINT8(client_mac_addr));

#ifdef CFG_WIFI_CONCURRENT
    if (wifi_management_concurrent_get()) {
        vif_idx = WIFI_VIF_INDEX_SOFTAP_MODE;
    }
#endif
    if (!wifi_vif_is_softap(vif_idx)) {
        app_print("No SoftAP is started.\r\n");
        return;
    }
    if (dhcpd_find_ipaddr_by_macaddr(client_mac_addr) == 0) {
        app_print("Client is not connected to our SoftAP.\r\n");
        return;
    }

    ret = wifi_management_ap_delete_client(client_mac_addr);
    if (ret) {
        app_print("SoftAP disconnect to client "MAC_FMT" failed.\r\n", MAC_ARG_UINT8(client_mac_addr));
    }

    return;
Usage:
    app_print("\rUsage: wifi_ap_client_delete <client mac addr>\r\n");
}

static void cmd_wifi_ap_stop(int argc, char **argv)
{
    wifi_management_ap_stop();
}
#endif // CFG_SOFTAP

#ifdef CONFIG_SOFTAP_PROVISIONING
/*!
    \brief      stop wifi ap mode
    \param[in]  argc: number of parameters
    \param[in]  argv: the pointer to the array that holds the parameters
    \param[out] none
    \retval     none
*/
static void cmd_wifi_ap_provisioning(int argc, char **argv)
{
    int start;
    do {
        if (argc == 2) {
            start = atoi(argv[1]);
            if (start == 1) {
                wifi_softap_provisioning_start();
            } else if (start == 0) {
                wifi_softap_provisioning_stop();
            } else {
                break;
            }
            return;
        }
    } while (0);

    app_print("Usage: wifi_ap_provisioning [start]\r\n");
    app_print("\tstart: 1: start provisioning, 0: stop provisioning \r\n");
}
#endif

#ifdef CFG_TWT
static void cmd_wifi_twt_setup(int argc, char **argv)
{
    struct macif_twt_setup_t param;
    char *endptr = NULL;

    do {
        if (argc < 6)
            break;

        if (argc >= 7) {
            param.wake_dur_unit = (uint32_t)strtoul((const char *)argv[6], &endptr, 10);
            if ((*endptr != '\0'))
                break;
        } else {
            param.wake_dur_unit = 0;
        }

        param.setup_type = (uint32_t)strtoul((const char *)argv[1], &endptr, 10);
        if ((*endptr != '\0') || (param.setup_type > 2))
            break;

        param.flow_type = (uint32_t)strtoul((const char *)argv[2], &endptr, 10);
        if ((*endptr != '\0') || (param.flow_type > 1))
            break;

        param.wake_int_exp = (uint32_t)strtoul((const char *)argv[3], &endptr, 10);

        if ((*endptr != '\0') || (param.wake_int_exp > 31))
            break;

        param.wake_int_mantissa = (uint32_t)strtoul((const char *)argv[4], &endptr, 10);

        if ((*endptr != '\0') || (param.wake_int_mantissa == 0) || (param.wake_int_mantissa > 0xFFFF))
            break;

        param.min_twt_wake_dur = (uint32_t)strtoul((const char *)argv[5], &endptr, 10);
        if ((*endptr != '\0') || (param.min_twt_wake_dur > 255))
            break;

        wifi_netlink_twt_setup(WIFI_VIF_INDEX_DEFAULT, &param);

        return;
    } while(0);

    app_print("Invaild parameters!!\r\n");

    app_print("Usage: wifi_setup_twt <setup type> <flow> <wake interval exp>  <wake interval mantissa> <mini wake> [wake unit]\n\r");
    app_print("\tsetup type: 0: Request, 1: Suggest, 2: Demand\r\n");
    app_print("\tflow: 0: Announced, 1: Unannounced\r\n");
    app_print("\twake interval exp: TWT Wake Interval Exponent , 0 - 31\r\n");
    app_print("\twake interval mantissa: TWT Wake Interval mantissa, 1 - 0xFFFF\r\n");
    app_print("\t\tTWT Wake Interval = (wake interval mantissa) * 2^(wake interval exp) us\r\n");
    app_print("\tmini wake: max 255, Minimum TWT Wake Duration = (mini wake) * (wake unit)\r\n");
    app_print("\twake unit: 0:256us, 1:tu(1024us), default wake unit 0\r\n");

    return;
}

static void cmd_wifi_twt_teardown(int argc, char **argv)
{
    char *endptr = NULL;
    uint8_t id = 0;
    uint8_t neg_type = 0;

    if (argc < 2) {
        goto Usage;
    }

    if (argc >= 2) {
        id = (uint32_t)strtoul((const char *)argv[1], &endptr, 10);
        if (*endptr != '\0') {
            app_print("cmd_wifi_twt_teardown: invalid id\r\n");
            return;
        }
    }

    if (argc == 3) {
        neg_type = (uint32_t)strtoul((const char *)argv[2], &endptr, 10);
        if (*endptr != '\0') {
            app_print("cmd_wifi_twt_teardown: invalid negotiation type\r\n");
            return;
        }
    }

    wifi_netlink_twt_teardown(WIFI_VIF_INDEX_DEFAULT, id, neg_type);
    return;

Usage:
    app_print("Invaild parameters!!\r\n");
    app_print("Usage: wifi_teardown_twt <flow id> [negotiation type]\r\n");
    app_print("\tnegotiation type: default 0\r\n");
}
#endif /* CFG_TWT */
/**
 ****************************************************************************************
 * @brief Process function for 'wifi_monitor' command
 *
 * monitor command can be used to start the monitor mode
 *
   @verbatim
     wifi_monitor start <channel>
     wifi_monitor stop
   @endverbatim
 *
 * @param[in] params monitor_start/stop commands above
 ****************************************************************************************
 */
static void cmd_wifi_monitor(int argc, char **argv)
{
    char *option;
    uint8_t channel;

    if (argc == 3) {
        option = argv[1];
        if (!strcmp("start", option)) {
            channel = atoi(argv[2]);
            if (channel < 1 || channel > 14) {
                goto usage;
            }
            wifi_management_monitor_start(channel, NULL);
            return;
        }
    } else if (argc == 2) {
        option = argv[1];
        if (!strcmp("stop", option)) {
            struct wifi_vif_tag *wvif = &wifi_vif_tab[WIFI_VIF_INDEX_DEFAULT];

            if (wvif->wvif_type != WVIF_MONITOR) {
                app_print("not monitor mode, do nothing.\r\n");
                return;
            }

            /* stop monitor first, if the monitor is already started */
            wifi_management_sta_start();
            return;
        }
    }

usage:
    app_print("Usage: wifi_monitor stop | start <channel>\r\n");
    app_print("start: start the monitor mode.\r\n");
    app_print("<channel>: 1~14.\r\n");
    app_print("stop: stop the monitor mode.\r\n");
}

#ifdef CFG_WPS
/**
 ****************************************************************************************
 * @brief Process function for 'wps' command
 *
 * wps command can be used to connect with the AP using PBC mode or PIN mode
 *
   @verbatim
     wps pbc : connect with AP using WPS PBC mode
     wps pin pin_code : connect with AP using WPS PIN code.
   @endverbatim
 *
 * @param[in] params pbc/pin commands above
 ****************************************************************************************
 */
static void cmd_wifi_wps(int argc, char **argv)
{
    bool is_pbc = true;
    char *pin = NULL;
    int i, ret;

    if (argc == 1) {
        goto Usage;
    }
    if (argc >= 2) {
        if (strncmp(argv[1], "pbc", 3) == 0) {
            is_pbc = true;
        } else if (strncmp(argv[1], "pin", 3) == 0) {
            is_pbc = false;
        } else {
            app_print("WPS parameter error.\r\n");
            goto Usage;
        }
    }

    if (argc >= 3){
        pin = argv[2];
        if (strlen(pin) != 8) {
            app_print("WPS PIN code length is not 8.\r\n");
            goto Usage;
        }
        for (i = 0; i < strlen(pin); i++) {
            if (!('0' <= pin[i] && pin[i] <= '9')) {
                app_print("WPS PIN code must be all digit numbers.\r\n");
                goto Usage;
            }
        }
    }

    ret = wifi_management_wps_start(is_pbc, pin, true);
    if (ret) {
        app_print("WPS failed and return %d\r\n", ret);
    } else {
        app_print("WPS succeeded.\r\n");
    }
    return;
Usage:
    app_print("Usage: wifi_wps pbc | pin <pin code>\r\n");
    app_print("Example:\r\n");
    app_print("     : wifi_wps pbc\r\n");
    app_print("     : wifi_wps pin 43022618\r\n");
}
#endif /* CFG_WPS */

#ifdef CONFIG_OTA_DEMO_SUPPORT
static void cmd_ota_demo(int argc, char **argv)
{
    char *srv_addr;
    char *image_url;

    if (argc != 3)
        goto usage;

    srv_addr = argv[1];
    image_url = argv[2];

    if (strlen(srv_addr) == 0 || strlen(image_url) == 0)
        goto usage;

    /* HTTP OTA */
    if (ota_demo_cfg_init(srv_addr, image_url, 0)) {
        app_print("OTA config init failed\r\n");
        return;
    }

    /* Start OTA task */
    if (ota_demo_start() != 0) {
        app_print("OTA demo start failed\r\n");
        return;
    }

    return;

usage:
    app_print("Usage: ota_demo <srvaddr> <imageurl>\r\n");
    app_print("<srvaddr>: IPv4 address of remote OTA server.\r\n");
    app_print("<imageurl>: OTA firmware filename or path.\r\n");
    app_print("Example:\r\n");
    app_print("    ota_demo 192.168.3.89 image-ota.bin\r\n");
}
#endif

#if NVDS_FLASH_SUPPORT
static int nvds_hex_para(char *argv, char *para, uint16_t *para_len, uint16_t max_len)
{
    uint16_t offset, hex_len;
    char str[3];

    hex_len = (strlen(argv) - 3) / 2 + 1;
    if (hex_len > max_len)
        return -1;

    offset = hex_len - 1;
    for (int i = strlen(argv) - 1; i >= 2; i = i - 2) {
        if (i <= 2) {
            str[0] = argv[i];
            str[1] = '\0';
            para[offset--] = strtoul(str, NULL, 16);
        } else {
            str[0] = argv[i - 1];
            str[1] = argv[i];
            str[2] = '\0';
            para[offset--] = strtoul(str, NULL, 16);
        }
    }

    para[hex_len] = '\0';
    *para_len = hex_len;

    #if 0
    for(int i = 0; i < hex_len; i++)
        app_print("0x%02x ", para[i]);
    app_print("\r\n");
    #endif
    return 0;
}

/**
 ****************************************************************************************
 * @brief Process function for 'nvds' command
 *
 * nvds command can be used to clean/add/del/show nvds flash data
 *
   @verbatim
     nvds clean : Erase internal nvds flash
     nvds add namespace key value : Save data to nvds flash.
     nvds del namespace key : Delete data in nvds flash.
     nvds dump : Show all valid data stored in nvds flash.
     nvds dump verbose : Show all data include invalid stored in nvds flash.
     nvds dump namespace : Show all data in the specified namespace.
     nvds dump namespace key : Show data by specified namespace and key.
   @endverbatim
 *
 * @param[in] params clean/add/del/show commands above
 ****************************************************************************************
 */
static void cmd_nvds_handle(int argc, char **argv)
{
    char *option, *ns;
    int ret;
    char key[16], value[256];
    uint16_t key_len;
    uint16_t value_len;
    uint32_t length;
    uint8_t *buffer;

    if (argc < 2)
        goto usage;

    option = argv[1];
    if (!strcmp("clean", option)) {
        ret = nvds_clean(NULL);
        if (ret)
            app_print("NVDS flash erase failed, error code:%d\r\n", ret);
        else
            app_print("NVDS flash erase ok.\r\n");
    } else if (!strcmp("add", option)) {
        if (argc < 5)
            goto usage;

        // namespace
        ns = argv[2];
        if (strlen(ns) > 15) {
            app_print("Illeagl namespace, Maximum length is 15 characters.\r\n");
            goto usage;
        }

        // key
        if (!memcmp("0x", argv[3], 2)) {
            ret = nvds_hex_para(argv[3], key, &key_len, 15);
            if (ret) {
                app_print("Illeagl key, Maximum length is 15 characters.\r\n");
                goto usage;
            }
        } else {
            key_len = strlen(argv[3]) + 1;
            if (strlen(argv[3]) > 15) {
                app_print("Illeagl key, Maximum length is 15 characters.\r\n");
                goto usage;
            }
            memcpy(key, argv[3], key_len);
        }

        // value
        if (!memcmp("0x", argv[4], 2)) {
            ret = nvds_hex_para(argv[4], value, &value_len, 255);
            if (ret) {
                app_print("Illeagl value, Maximum length is 255 characters.\r\n");
                goto usage;
            }
        } else {
            value_len = strlen(argv[4]) + 1;
            if (strlen(argv[4]) > 255) {
                app_print("Illeagl value, Maximum length is 255 characters.\r\n");
                goto usage;
            }
            memcpy(value, argv[4], value_len);
        }

        ret = nvds_data_put(NULL, ns, key, (uint8_t*)value, value_len);
        if (ret)
            app_print("NVDS flash add key failed, error code:%d\r\n", ret);
        else
            app_print("NVDS flash add key ok\r\n");
    } else if (!strcmp("del", option)) {
        if (argc < 3)
            goto usage;

        // namespace
        ns = argv[2];
        if (strlen(ns) > 15) {
            app_print("Illeagl namespace, Maximum length is 15 characters.\r\n");
            goto usage;
        }

        if (argc == 3) {
            ret = nvds_del_keys_by_namespace(NULL, ns);
        } else {
            // key
            if (!memcmp("0x", argv[3], 2)) {
                ret = nvds_hex_para(argv[3], key, &key_len, 15);
                if (ret) {
                    app_print("Illeagl key, Maximum length is 15 characters.\r\n");
                    goto usage;
                }
            } else {
                key_len = strlen(argv[3]) + 1;
                if (strlen(argv[3]) > 15) {
                    app_print("Illeagl key, Maximum length is 15 characters.\r\n");
                    goto usage;
                }
                memcpy(key, argv[3], key_len);
            }

            ret = nvds_data_del(NULL, ns, key);
        }

        if (ret)
            app_print("NVDS flash delete key failed, error code:%d\r\n", ret);
        else
            app_print("NVDS flash delete key ok\r\n");
    } else if (!strcmp("dump", option)) {
        if (argc == 2) {
            nvds_dump(NULL, 0, NULL);
        } else {
            if (!strcmp("verbose", argv[2])) {
                nvds_dump(NULL, 1, NULL);
            } else {
                // namespace
                ns = argv[2];
                if (strlen(ns) > 15) {
                    app_print("Illeagl namespace, Maximum length is 15 characters.\r\n");
                    goto usage;
                }
                if (argc == 3) {
                    nvds_dump(NULL, 0, ns);
                }

                if (argc > 3) {
                    // key
                    if (!memcmp("0x", argv[3], 2)) {
                        ret = nvds_hex_para(argv[3], key, &key_len, 15);
                        if (ret) {
                            app_print("Illeagl key, Maximum length is 15 characters.\r\n");
                            goto usage;
                        }
                    } else {
                        key_len = strlen(argv[3]) + 1;
                        if (strlen(argv[3]) > 15) {
                            app_print("Illeagl key, Maximum length is 15 characters.\r\n");
                            goto usage;
                        }
                        memcpy(key, argv[3], key_len);
                    }

                    length = 0;
                    ret = nvds_data_get(NULL, ns, key, NULL, &length);
                    if (ret) {
                        app_print("NVDS flash get length failed, error code:%d\r\n", ret);
                        goto usage;
                    }
                    buffer = sys_malloc(length  + 1);
                    ret = nvds_data_get(NULL, ns, key, buffer, &length);
                    if (ret) {
                        app_print("NVDS flash get key value failed, error code:%d\r\n", ret);
                        goto usage;
                    } else {
                        app_print("NVDS flash get key: %s, value(str):%s, value(hex):", key, (char*)buffer);
                        for(int i = 0; i < length; i++) {
                            app_print("%02x ", buffer[i]);
                        }
                        app_print("\r\n");
                    }
                    sys_mfree(buffer);
                }
            }
        }
    } else {
        goto usage;
    }
    return;
usage:
    app_print("Usage: nvds clean | add | del | dump [options]\r\n");
    app_print("     : nvds clean : Erase internal nvds flash.\r\n");
    app_print("     : nvds add <namespace> <key> <value> : Save data to nvds flash.\r\n");
    app_print("     : nvds del <namespace> <key> : Delete data in nvds flash.\r\n");
    app_print("     : nvds del <namespace> : Delete all the data in the specified namespace.\r\n");
    app_print("     : nvds dump : Show all valid data stored in nvds flash.\r\n");
    app_print("     : nvds dump verbose : Show all data include invalid stored in nvds flash.\r\n");
    app_print("     : nvds dump <namespace> : Show all data in the specified namespace.\r\n");
    app_print("     : nvds dump <namespace> <key> : Show data by specified namespace and key.\r\n");
    app_print("     : Hexadecimals parmeter starts with 0x, else string.\r\n");
    app_print("Example:\r\n");
    app_print("     : nvds add wifi ip 0xc0a80064\r\n");
    app_print("     : nvds add wifi ssid gigadevice\r\n");
}
#endif /* NVDS_FLASH_SUPPORT */

#ifdef CONFIG_FATFS_SUPPORT
/*!
    \brief      fatfs related operation
    \param[in]  argc: number of parameters
    \param[in]  argv: the pointer to the array that holds the parameters
    \param[out] none
    \retval     none
*/
void cmd_fatfs(int argc, char **argv)
{

    if(argc >= 2 && argc <= 5){
        if (cmd_fatfs_exec(argc, argv) == 0)
            return;
    }
    app_print("\r\nUsage:\r\n");
    app_print("    fatfs create <path | path/filename>(path should end with \\ or /)\r\n");
    app_print("    fatfs append <path/filename> <string>\r\n");
    app_print("    fatfs read   <path/filename> [length] [offset]\r\n");
    app_print("    fatfs rename <path/filename> <[path/]new filename>\r\n");
    app_print("    fatfs delete <path | path/filename>\r\n");
    app_print("    fatfs show   [dir]\r\n");
    app_print("    Example: fatfs create a/b/c/d/ | fatfs create a/b/c/d.txt\r\n");
}
#endif

#ifdef USE_QSPI_FLASH
extern void qspi_flash_chip_erase(void);
static void cmd_qspi_flash_chip_erase(int argc, char **argv)
{
    qspi_flash_chip_erase();
    app_print("cmd_qspi_flash_chip_erase done! \r\n");
}
#endif

static void cmd_image_switch(int argc, char **argv)
{
    uint8_t running_idx = 0;
    uint32_t res = 0;

    res = rom_sys_status_get(SYS_RUNNING_IMG, LEN_SYS_RUNNING_IMG, &running_idx);

    res = rom_sys_set_img_flag(running_idx, (IMG_FLAG_IA_MASK | IMG_FLAG_NEWER_MASK), (IMG_FLAG_IA_OK | IMG_FLAG_OLDER));
    res |= rom_sys_set_img_flag(!running_idx, (IMG_FLAG_IA_MASK | IMG_FLAG_VERIFY_MASK | IMG_FLAG_NEWER_MASK), 0);
    res |= rom_sys_set_img_flag(!running_idx, IMG_FLAG_NEWER_MASK, IMG_FLAG_NEWER);

    if (res != 0) {
        app_print("Set sys image status failed(res = %d), switch image failed!\r\n", res);
        return;
    }

    app_print("Switch image successful, please reboot now!\r\n");
    return;
}

// Array of supported CLI command
static const struct cmd_entry cmd_table[] =
{
    {"help", cmd_help},
    {"reboot", cmd_reboot},
    {"version", cmd_version},
#ifdef USE_QSPI_FLASH
    {"qspi_flash_chip_erase", cmd_qspi_flash_chip_erase},
#endif

#ifdef CONFIG_FATFS_SUPPORT
    {"fatfs", cmd_fatfs},
#endif

    {"rmem", cmd_read_memory},
#ifdef CONFIG_BASECMD
    {"tasks", cmd_task_list},
    {"free", cmd_free},
    {"sys_ps", cmd_sys_ps},
    {"cpu_stats", cmd_cpu_stats},
    {"ps_stats", cmd_ps_stats},
    {"xmodem", cmd_xmodem},
#ifdef CFG_WLAN_SUPPORT
    {"ping", cmd_ping},
    {"join_group", cmd_group_join},
#ifdef CONFIG_SSL_TEST
    {"ssl_client", cmd_ssl_client},
#endif
#ifdef CONFIG_IPERF_TEST
    {"iperf", cmd_iperf},
#endif
#ifdef CONFIG_IPERF3_TEST
    {"iperf3", cmd_iperf3},
#endif
#ifdef CONFIG_OTA_DEMO_SUPPORT
    {"ota_demo", cmd_ota_demo},
#endif
#if LWIP_STATS && LWIP_STATS_DISPLAY
    {"lwip_stats", cmd_lwip_stats},
#endif
    {"wifi_debug", cmd_wifi_debug},
    {"wifi_open", cmd_wifi_open},
    {"wifi_close", cmd_wifi_close},
    {"wifi_mac_addr", cmd_wifi_mac_addr},
#ifdef CFG_WIFI_CONCURRENT
    {"wifi_concurrent", cmd_wifi_concurrent},
#endif
    {"wifi_auto_conn", cmd_wifi_auto_conn},
    {"wifi_wireless_mode", cmd_wifi_wireless_mode},
    {"wifi_roaming", cmd_wifi_roaming},
    {"wifi_scan", cmd_wifi_scan},
    {"wifi_connect", cmd_wifi_connect},
    {"wifi_connect_bssid", cmd_wifi_connect_bssid},
#ifdef CFG_8021x_EAP_TLS
    {"wifi_connect_eap_tls", cmd_wifi_connect_eap_tls},
#endif /* CFG_8021x_EAP_TLS */
    {"wifi_disconnect", cmd_wifi_disconnect},
    {"wifi_status", cmd_wifi_status},
    {"wifi_set_ip", cmd_wifi_ip_set},
#ifdef CFG_LPS
    {"wifi_ps", cmd_wifi_ps},
    {"wifi_listen_interval", cmd_wifi_listen_interval_set},
#endif

#ifdef CFG_TWT
    {"wifi_setup_twt", cmd_wifi_twt_setup},
    {"wifi_teardown_twt", cmd_wifi_twt_teardown},
#endif

    {"wifi_monitor", cmd_wifi_monitor},
#ifdef CFG_SOFTAP
    {"wifi_ap", cmd_wifi_ap},
    {"wifi_ap_client_delete", cmd_wifi_ap_client_delete},
    {"wifi_stop_ap", cmd_wifi_ap_stop},
#endif /* CFG_SOFTAP */
#ifdef CONFIG_SOFTAP_PROVISIONING
    {"wifi_ap_provisioning", cmd_wifi_ap_provisioning},
#endif

#ifdef CFG_WPS
    {"wifi_wps", cmd_wifi_wps},
#endif /* CFG_WPS */
#ifdef CONFIG_MQTT
    {"mqtt", cmd_mqtt},
#endif /*CONFIG_MQTT*/
#ifdef CONFIG_COAP
    {"coap_client", cmd_coap_client},
    {"coap_server", cmd_coap_server},
#endif
#ifdef CONFIG_LWIP_SOCKETS_TEST
    {"socket_client", cmd_lwip_sockets_client},
    {"socket_server", cmd_lwip_sockets_server},
    {"socket_close", cmd_lwip_sockets_close},
    {"socket_get_status", cmd_lwip_sockets_get_status},
#endif
#endif /* CFG_WLAN_SUPPORT */
#if NVDS_FLASH_SUPPORT
    {"nvds", cmd_nvds_handle},
#endif /* NVDS_FLASH_SUPPORT */
    {"image_switch", cmd_image_switch},
#endif /* CONFIG_BASECMD */

    {"", NULL}
};

#if 0//def CONFIG_ATCMD
static void at_cmd_exec(struct cmd_msg *msg)
{
    int argc;
    char *argv[MAX_ARGC];
    char *command;
    cmd_handle_cb handle_cb = NULL;
    struct cmd_module_reg_info * atcmd_tab_info = &(cmd_info.cmd_reg_infos[CMD_MODULE_ATCMD]);

    command = sys_malloc(msg->len);
    uart_cmd_rx_handle_done((cyclic_buf_t *)msg->data, (uint8_t *)command, &msg->len);

    if (command == NULL)
    {
        app_print("No buffer alloc for at cmd ! \r\n");
        return;
    }

    if ((atcmd_tab_info->parse_cb == NULL) ||
        (argc = atcmd_tab_info->parse_cb(command, argv)) <= 0) {
        at_rsp_error();
        goto done;
    }

    if (atcmd_tab_info->get_handle_cb != NULL &&
        (atcmd_tab_info->prefix == NULL ||
        !memcmp(command, atcmd_tab_info->prefix, strlen(atcmd_tab_info->prefix)))) {
        if (atcmd_tab_info->get_handle_cb(command, (void **)&handle_cb) == CLI_SUCCESS) {
            handle_cb(argc, argv);
        }
    }

done:
    sys_mfree(command);
    app_print("# ");
}
#endif

/**
 ****************************************************************************************
 * @brief Extract parameter from list
 *
 * Extract the parameter of the string. Parameters are separatd with space unless
 * it starts with " (or ') in which case it extract the parameter until " (or ') is reached.
 * " (or ') are then removed from the parameter.
 *
 * @param[in, out] params Pointer to parameters string to parse. Updated with remaining
 *                        parameters to parse.
 * @return pointer on first parameter
 ****************************************************************************************
 */
static char *get_next_param(char **params)
{
    char *ptr = *params, *next;
    char sep = ' ';

    if (!ptr)
        return NULL;

    if ((ptr[0] == '"') || (ptr[0] == '\'')) {
        sep = ptr[0];
        ptr++;
    }

    next = strchr(ptr, sep);
    if (next) {
        *next++ = '\0';
        while (*next == ' ')
            next++;
        if (*next == '\0')
            next = NULL;
    }
    *params = next;
    return ptr;
}

static int parse_cmd(char *buf, char **argv)
{
    int argc = 0;
    char *param;

    if (buf == NULL)
        return 0;

    param = get_next_param(&buf);
    while (param && (argc < MAX_ARGC)) {
        argv[argc] = param;
        argc++;
        param = get_next_param(&buf);
    }

    return argc;
}

static void cmd_common_help(void)
{
#ifdef CONFIG_BASECMD
    uint8_t i;
    for (i = 0; cmd_table[i].function != NULL; i++) {
        app_print("\t%s\n", cmd_table[i].command);
    }
#endif

#if defined(CONFIG_RF_TEST_SUPPORT) || defined(CONFIG_INTERNAL_DEBUG)
    app_print("==============================\r\n");
    wifi_rftest_cmd_help();
#endif

#if defined(CONFIG_INTERNAL_DEBUG)
    app_print("==============================\r\n");
    wifi_inner_cmd_help();
#endif
    return;
}

static uint8_t cmd_common_handle(void *data, void **cmd)
{
    const struct cmd_entry *w_cmd = cmd_table;

    while (w_cmd->function) {
        if (!strcmp((char *)data, w_cmd->command)) {
            *cmd = w_cmd->function;
            break;
        }
        w_cmd++;
    }

#if defined(CONFIG_RF_TEST_SUPPORT) || defined(CONFIG_INTERNAL_DEBUG)
    if (w_cmd->function == NULL) {
        w_cmd = wifi_rftest_get_handle_cb(data, cmd);
    }
#endif

#if defined(CONFIG_INTERNAL_DEBUG)
    if (w_cmd->function == NULL) {
        w_cmd = wifi_inner_get_handle_cb(data, cmd);
    }
#endif

    return w_cmd->function == NULL ? CLI_UNKWN_CMD : CLI_SUCCESS;
}

/**
 ****************************************************************************************
 * @brief separate the param and cmd from the msg separated by symbol of ' '
 *
 * @param[in] command          point to msg
 * @param[in] command_len      length of msg
 * @param[out] param           point to the param of cmd
 ****************************************************************************************
 */
static char* cmd_param_separate(char *command, uint16_t command_len)
{
    char *param = strchr(command, ' ');

    if (param) {
        *param++ = '\0';
        while (*param == ' ') {
            param++;
        }
    } else {
         command[command_len - 1] = '\0'; //be sure to have \0 in command
    }
    return param;
}

static void cmn_cmd_exec(struct cmd_msg *msg)
{
    char *command, *param;
    uint32_t res = CLI_UNKWN_CMD;
    cmd_handle_cb handle_cb = NULL;
    cmd_parse_cb parse_cb = parse_cmd; // default parse function.
    char *argv[MAX_ARGC];
    uint32_t argc;
    uint8_t idx;

    command = sys_malloc(msg->len);
    uart_cmd_rx_handle_done((cyclic_buf_t *)msg->data, (uint8_t *)command, &msg->len);

    if (command == NULL)
    {
        app_print("No buffer alloc for cmd ! \r\n");
        return;
    }

    param = cmd_param_separate(command, msg->len);

    if (!strcmp(command, "help")) {
        for (idx = 0; idx < CMD_MODULE_MAX; idx++) {
            if (cmd_info.cmd_reg_infos[idx].help_cb != NULL) {
                app_print("==============================\r\n");
                cmd_info.cmd_reg_infos[idx].help_cb();
            }
        }
        goto symbol_print;
    }

    for (idx = 0; idx < CMD_MODULE_MAX; idx++) {
        if (cmd_info.cmd_reg_infos[idx].get_handle_cb != NULL &&
            (cmd_info.cmd_reg_infos[idx].prefix == NULL ||
            !memcmp(command, cmd_info.cmd_reg_infos[idx].prefix, strlen(cmd_info.cmd_reg_infos[idx].prefix)))) {
            res = cmd_info.cmd_reg_infos[idx].get_handle_cb(command, (void **)&handle_cb);
            if (res == CLI_SUCCESS) {
                parse_cb = cmd_info.cmd_reg_infos[idx].parse_cb != NULL ? cmd_info.cmd_reg_infos[idx].parse_cb : parse_cb;
                break;
            } else if (res == CLI_ERROR) {
                break;
            }
        }
    }

    switch (res) {
    case CLI_SUCCESS :
        argv[0] = command;
        argc = parse_cb(param, &argv[1]) + 1;
        handle_cb(argc, argv);
        break;

    case CLI_UNKWN_CMD :
        app_print("Unknown command - %s!\r\n", command);
        if (unkwn_cmd_handler)
            unkwn_cmd_handler(strlen(command), (uint8_t *)command);
        break;

    case  CLI_ERROR:
    default :
        app_print("Error!\r\n");
        break;
    }

symbol_print:
    sys_mfree(command);
    app_print("# ");
    return;
}

static void cmd_msg_process(struct cmd_msg *msg)
{
    switch (cmd_mode_type_get()) {
#if 0//def CONFIG_ATCMD
    case CMD_MODE_TYPE_AT:
        at_cmd_exec(msg);
        break;
#endif
    case CMD_MODE_TYPE_NORMAL:
    default :
        cmn_cmd_exec(msg);
        break;
    }

    return;
}

void cmd_unkwn_cmd_handler_reg(cmd_unkwn_handle_cb cb)
{
    unkwn_cmd_handler = cb;
}

void cmd_unkwn_cmd_handler_unreg(void)
{
    unkwn_cmd_handler = NULL;
}

uint8_t cmd_module_reg(enum cmd_module_id id, char *prefix, cmd_module_get_handle_cb get_handle_cb,
    cmd_module_help_cb help_cb, cmd_parse_cb parse_cb)
{
    if (id >= CMD_MODULE_MAX || get_handle_cb == NULL)
        return CLI_ERROR;

    cmd_info.cmd_reg_infos[id].prefix = prefix;
    cmd_info.cmd_reg_infos[id].get_handle_cb = get_handle_cb;
    cmd_info.cmd_reg_infos[id].help_cb = help_cb;
    cmd_info.cmd_reg_infos[id].parse_cb = parse_cb;

    return CLI_SUCCESS;
}

void cmd_mode_type_set(enum cmd_mode_type cmd_mode)
{
    cmd_info.cmd_mode = cmd_mode;
}

enum cmd_mode_type cmd_mode_type_get(void)
{
    return cmd_info.cmd_mode;
}

/**
 ****************************************************************************************
 * @brief CLI task main loop
 *
 * CLI task may received command and process it
 ****************************************************************************************
 */
static void cmd_cli_task(void *param)
{
    struct cmd_msg msg;

#ifdef CFG_WLAN_SUPPORT
    wifi_wait_ready();
#endif

    for (;;) {
        sys_queue_read(&cmd_queue, &msg, -1, false);
        cmd_msg_process(&msg);
    }
}

int cmd_shell_init(void)
{
    log_uart_rx_init();

    if (sys_queue_init(&cmd_queue, CLI_QUEUE_SIZE, sizeof(struct cmd_msg))) {
        return -2;
    }

    sys_memset(&cmd_info, 0, sizeof(struct cmd_module_info));
    cmd_mode_type_set(CMD_MODE_TYPE_NORMAL);
    if (cmd_module_reg(CMD_MODULE_COMMON, NULL, cmd_common_handle, cmd_common_help, NULL))
        return -1;

    if (sys_task_create_dynamic((const uint8_t *)"CLI task", CLI_STACK_SIZE, CLI_PRIORITY, cmd_cli_task, NULL) == NULL) {
        return -1;
    }
//    trace_uart_rx_cb_register(cmd_info_send);

    return 0;
}

int cmd_info_send(int id, void *msg_data, uint16_t len)
{
    struct cmd_msg msg;

    msg.id   = CMD_MSG_ID(0, id);
    msg.len  = len;
    msg.data = msg_data;

    return sys_queue_write(&cmd_queue, &msg, 0, true);
}
