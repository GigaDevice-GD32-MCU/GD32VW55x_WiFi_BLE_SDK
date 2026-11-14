#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"

#include "wrapper_os.h"
#include "wifi_management.h"
#include "wifi_eloop.h"
#include "wifi_netlink.h"
#include "wifi_net_ip.h"
#include "wifi_sta_config.h"
#include "wifi_sta.h"
#include "debug_print.h"
#include "gd32vw55x_platform.h"

static volatile bool g_wifi_dhcp_ready = false;
static volatile bool g_wifi_dhcp_failed = false;

#define WIFI_STA_DELAY_MS(ms)    vTaskDelay(pdMS_TO_TICKS(ms))

static void wifi_sta_dhcp_success(void *eloop_data, void *user_ctx)
{
    struct wifi_ip_addr_cfg cfg;

    sys_memset(&cfg, 0, sizeof(cfg));

    if (wifi_get_vif_ip(WIFI_VIF_INDEX_DEFAULT, &cfg) == 0) {
        g_wifi_dhcp_ready = true;
        g_wifi_dhcp_failed = false;

        printf("\r\n[WiFi] DHCP success!\r\n");
        printf("[WiFi] IP      : " IP_FMT "\r\n", IP_ARG(cfg.ipv4.addr));
        printf("[WiFi] Netmask : " IP_FMT "\r\n", IP_ARG(cfg.ipv4.mask));
        printf("[WiFi] Gateway : " IP_FMT "\r\n", IP_ARG(cfg.ipv4.gw));
        printf("[WiFi] DNS     : " IP_FMT "\r\n", IP_ARG(cfg.ipv4.dns));
        printf("[WiFi] Network ready.\r\n");
        LED_ON(LED_WIFI_READY);
    } else {
        printf("[WiFi] DHCP event received, but IP query failed.\r\n");
        g_wifi_dhcp_failed = true;
    }
}

static void wifi_sta_dhcp_fail(void *eloop_data, void *user_ctx)
{
    g_wifi_dhcp_ready = false;
    g_wifi_dhcp_failed = true;
    printf("[WiFi] DHCP failed.\r\n");
}

static void wifi_sta_disconnect(void *eloop_data, void *user_ctx)
{
    g_wifi_dhcp_ready = false;
    g_wifi_dhcp_failed = true;
    printf("[WiFi] Disconnected from AP.\r\n");
}

static void wifi_sta_task(void *arg)
{
    struct mac_scan_result candidate;
    int ret;
    int wait_count;

    printf("\r\n========== WiFi STA START ==========\r\n");
    printf("[WiFi] SSID: %s\r\n", WIFI_STA_SSID);

    /* 注册事件，只执行一次 */
    ret = eloop_event_register(WIFI_MGMT_EVENT_DHCP_SUCCESS, wifi_sta_dhcp_success, NULL, NULL);
    if (ret != 0) {
        printf("[WiFi] Register DHCP_SUCCESS failed.\r\n");
        goto Exit;
    }

    ret = eloop_event_register(WIFI_MGMT_EVENT_DHCP_FAIL, wifi_sta_dhcp_fail, NULL, NULL);
    if (ret != 0) {
        printf("[WiFi] Register DHCP_FAIL failed.\r\n");
        goto Exit;
    }

    ret = eloop_event_register(WIFI_MGMT_EVENT_DISCONNECT, wifi_sta_disconnect, NULL, NULL);
    if (ret != 0) {
        printf("[WiFi] Register DISCONNECT failed.\r\n");
        goto Exit;
    }

    while (1) {
        /* DHCP 已成功，保持在线，等待断线事件 */
        if (g_wifi_dhcp_ready) {
            WIFI_STA_DELAY_MS(1000);
            continue;
        }

        g_wifi_dhcp_failed = false;

        printf("\r\n[WiFi] Start connection attempt...\r\n");

        /* 扫描目标 AP */
        printf("[WiFi] Scanning AP...\r\n");

        ret = wifi_management_scan(1, WIFI_STA_SSID);
        if (ret != 0) {
            printf("[WiFi] Scan failed: %d\r\n", ret);
            goto Retry;
        }

        sys_memset(&candidate, 0, sizeof(candidate));

        ret = wifi_netlink_candidate_ap_find(WIFI_VIF_INDEX_DEFAULT, NULL, WIFI_STA_SSID, &candidate);
        if (ret != 0) {
            printf("[WiFi] Target AP not found.\r\n");
            goto Retry;
        }

        printf("[WiFi] Target AP found.\r\n");

        /* 连接 AP */
        printf("[WiFi] Connecting...\r\n");

        ret = wifi_management_connect((char *)WIFI_STA_SSID, (char *)WIFI_STA_PASSWORD, 1);
        if (ret != 0) {
            printf("[WiFi] Connect failed: %d\r\n", ret);
            goto Retry;
        }

        printf("[WiFi] Connection request completed.\r\n");

        /* 等待 DHCP 成功，最多等待 20 秒 */
        printf("[WiFi] Waiting for DHCP...\r\n");

        for (wait_count = 0; wait_count < 40; wait_count++) {
            if (g_wifi_dhcp_ready || g_wifi_dhcp_failed) {
                break;
            }

            WIFI_STA_DELAY_MS(500);
        }

        if (g_wifi_dhcp_ready) {
            printf("[WiFi] Network ready, monitoring connection...\r\n");
            continue;
        }

        printf("[WiFi] DHCP timeout or connection lost.\r\n");
        wifi_management_disconnect();

Retry:
        printf("[WiFi] Retry connection after 5 seconds...\r\n");
        WIFI_STA_DELAY_MS(5000);
    }

Exit:
    sys_task_delete(NULL);
}

int wifi_sta_start(void)
{
    void *handle;

    handle = sys_task_create_dynamic(
        (const uint8_t *)"wifi_sta",
        WIFI_STA_STACK_SIZE,
        OS_TASK_PRIORITY(WIFI_STA_TASK_PRIO),
        (task_func_t)wifi_sta_task,
        NULL
    );

    if (handle == NULL) {
        printf("[WiFi] Create STA task failed.\r\n");
        return -1;
    }

    return 0;
}