/*!
    \file    ota_demo.c
    \brief   OTA demonstration program for GD32VW55x SDK.

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

#include "stdio.h"
#include "stdint.h"
#include "lwip/sockets.h"
#include "wrapper_os.h"
#include "config_gdm32.h"

#include "rom_export.h"
#include "raw_flash_api.h"
#include <app_cfg.h>
#include "dbg_print.h"
#include "ota_demo.h"

/* mbedtls headers for HTTPS support */
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/error.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"

#include "FreeRTOS.h"
#include "task.h"
#include "core_feature_timer.h"

#ifdef CONFIG_OTA_DEMO_SUPPORT

#define HTTP_GET_MAX_LEN            1024
#define RECBUFFER_LEN               1516
#define INVALID_SOCKET              (-1)
#define OTA_SOCKET_RECV_TIMEOUT     60000

#define HTTP_PORT                   8088
#define HTTPS_PORT                  443
#define TERM                        "\r\n"
#define ENDING                      "\r\n\r\n"

struct ota_srv_cfg {
    char host[IP4ADDR_STRLEN_MAX];
    int32_t port;
    int32_t sockfd;
    char image_url[OTA_IMAGE_URL_MAX_LEN];
    int use_ssl;  /* 0=HTTP, 1=HTTPS */
};
static struct ota_srv_cfg ota_demo_cfg;

/* SSL context for HTTPS connections */
typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_net_context server_fd;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt cacert;
} ota_ssl_context_t;

static ota_ssl_context_t *g_ota_ssl_ctx = NULL;

/**
 ****************************************************************************************
 * @brief Initialize the remote OTA server
 *
 * @param[in] srv_addr    IPv4 address of the remote OTA server
 * @param[in] image_url:  URL of the OTA image
 * @return    If the initialization is done or not
 ****************************************************************************************
 */
int32_t ota_demo_cfg_init(const char *srv_addr, const char *image_url, int use_ssl)
{
    if (srv_addr == NULL || strlen(srv_addr) >= sizeof(ota_demo_cfg.host))
        return -1;
    if (image_url == NULL || strlen(image_url) >= sizeof(ota_demo_cfg.image_url))
        return -1;

    /* Save SSL mode */
    ota_demo_cfg.use_ssl = use_ssl;

    /* Set port based on protocol: 443 for HTTPS, 80 for HTTP */
    ota_demo_cfg.port = ota_demo_cfg.use_ssl ? HTTPS_PORT : HTTP_PORT;
    ota_demo_cfg.sockfd = -1;

    sys_memcpy(ota_demo_cfg.host, srv_addr, strlen(srv_addr) + 1);
    sys_memcpy(ota_demo_cfg.image_url, image_url, strlen(image_url) + 1);

    app_print("OTA config: %s mode, server=%s, port=%d, path=%s\r\n",
              ota_demo_cfg.use_ssl ? "HTTPS" : "HTTP",
              ota_demo_cfg.host, ota_demo_cfg.port, ota_demo_cfg.image_url);

    return 0;
}

/**
 ****************************************************************************************
 * @brief Initialize SSL context for HTTPS
 ****************************************************************************************
 */
static int32_t ssl_init(const char *host, uint32_t port)
{
    int ret;
    const char *pers = "ota_ssl_client";

    if (g_ota_ssl_ctx != NULL) {
        return 0; // Already initialized
    }

    g_ota_ssl_ctx = (ota_ssl_context_t *)sys_zalloc(sizeof(ota_ssl_context_t));
    if (g_ota_ssl_ctx == NULL) {
        app_print("Failed to allocate SSL context\r\n");
        return -1;
    }

    /* Initialize SSL structures */
    mbedtls_ssl_init(&g_ota_ssl_ctx->ssl);
    mbedtls_ssl_config_init(&g_ota_ssl_ctx->conf);
    mbedtls_net_init(&g_ota_ssl_ctx->server_fd);
    mbedtls_entropy_init(&g_ota_ssl_ctx->entropy);
    mbedtls_ctr_drbg_init(&g_ota_ssl_ctx->ctr_drbg);
    mbedtls_x509_crt_init(&g_ota_ssl_ctx->cacert);

    /* Seed random number generator */
    ret = mbedtls_ctr_drbg_seed(&g_ota_ssl_ctx->ctr_drbg,
                                mbedtls_entropy_func,
                                &g_ota_ssl_ctx->entropy,
                                (const unsigned char *)pers,
                                strlen(pers));
    if (ret != 0) {
        app_print("mbedtls_ctr_drbg_seed failed: -0x%x\r\n", -ret);
        goto exit;
    }

    /* Connect to server */
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", port);
    ret = mbedtls_net_connect(&g_ota_ssl_ctx->server_fd, host, port_str, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        app_print("mbedtls_net_connect failed: -0x%x\r\n", -ret);
        goto exit;
    }
    app_print("Connected to %s:%d\r\n", host, port);

    /* Setup SSL/TLS config */
    ret = mbedtls_ssl_config_defaults(&g_ota_ssl_ctx->conf,
                                     MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        app_print("mbedtls_ssl_config_defaults failed: -0x%x\r\n", -ret);
        goto exit;
    }

    /* Skip certificate verification for OTA (optional - can be enabled if you have CA cert) */
    mbedtls_ssl_conf_authmode(&g_ota_ssl_ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&g_ota_ssl_ctx->conf, mbedtls_ctr_drbg_random, &g_ota_ssl_ctx->ctr_drbg);
    mbedtls_ssl_conf_read_timeout(&g_ota_ssl_ctx->conf, OTA_SOCKET_RECV_TIMEOUT);

    /* Setup SSL context */
    ret = mbedtls_ssl_setup(&g_ota_ssl_ctx->ssl, &g_ota_ssl_ctx->conf);
    if (ret != 0) {
        app_print("mbedtls_ssl_setup failed: -0x%x\r\n", -ret);
        goto exit;
    }

    /* Set hostname for SNI */
    mbedtls_ssl_set_hostname(&g_ota_ssl_ctx->ssl, host);

    /* Set I/O functions */
    mbedtls_ssl_set_bio(&g_ota_ssl_ctx->ssl, &g_ota_ssl_ctx->server_fd,
                       mbedtls_net_send, NULL, mbedtls_net_recv_timeout);

    /* Perform SSL handshake */
    app_print("Performing SSL handshake...\r\n");
    while ((ret = mbedtls_ssl_handshake(&g_ota_ssl_ctx->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            app_print("mbedtls_ssl_handshake failed: -0x%x\r\n", -ret);
            goto exit;
        }
    }
    app_print("SSL handshake completed\r\n");

    return 0;

exit:
    if (g_ota_ssl_ctx) {
        mbedtls_net_free(&g_ota_ssl_ctx->server_fd);
        mbedtls_ssl_free(&g_ota_ssl_ctx->ssl);
        mbedtls_ssl_config_free(&g_ota_ssl_ctx->conf);
        mbedtls_ctr_drbg_free(&g_ota_ssl_ctx->ctr_drbg);
        mbedtls_entropy_free(&g_ota_ssl_ctx->entropy);
        mbedtls_x509_crt_free(&g_ota_ssl_ctx->cacert);
        sys_mfree(g_ota_ssl_ctx);
        g_ota_ssl_ctx = NULL;
    }
    return -1;
}

/**
 ****************************************************************************************
 * @brief Cleanup SSL context
 ****************************************************************************************
 */
static void ssl_cleanup(void)
{
    if (g_ota_ssl_ctx) {
        mbedtls_net_free(&g_ota_ssl_ctx->server_fd);
        mbedtls_ssl_free(&g_ota_ssl_ctx->ssl);
        mbedtls_ssl_config_free(&g_ota_ssl_ctx->conf);
        mbedtls_ctr_drbg_free(&g_ota_ssl_ctx->ctr_drbg);
        mbedtls_entropy_free(&g_ota_ssl_ctx->entropy);
        mbedtls_x509_crt_free(&g_ota_ssl_ctx->cacert);
        sys_mfree(g_ota_ssl_ctx);
        g_ota_ssl_ctx = NULL;
    }
}

/**
 ****************************************************************************************
 * @brief Initialize http socket
 *
 * @param[in] host     Pointer to the input host name
 * @param[in] port:    Target port to be connected
 * @return    Status code to know if ota succeed or not
 *             -1      Create socket fail
 *             -2      Connect fail
 *             other   Created socket id
 ****************************************************************************************
 */
static int32_t http_socket_init(char *host, uint32_t port)
{
    /* Check if HTTPS mode is enabled */
    if (ota_demo_cfg.use_ssl) {
        app_print("Initializing HTTPS connection...\r\n");
        int ret = ssl_init(host, port);
        if (ret == 0) {
            app_print("HTTPS connection established\r\n");
            return 1; // Return non-negative value to indicate success
        } else {
            app_print("HTTPS connection failed\r\n");
            return -1;
        }
    }

    /* HTTP mode - original implementation */
    struct sockaddr_in sock;
    int32_t sid = INVALID_SOCKET;
    int32_t n = 1, ret;

    if ((sid = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        app_print("Create socket failed.\r\n");
        return -1;
    } else {
        int recv_timeout_ms = OTA_SOCKET_RECV_TIMEOUT;

        setsockopt(sid, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout_ms, sizeof(recv_timeout_ms));
        setsockopt(sid, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));
    }
    app_print("Socket ID: %d\r\n", sid);

    app_print("Connect to:\r\n");
    app_print("\tHost: %s", host);
    app_print("\tPort: %d\r\n", port);

    sock.sin_family = AF_INET;
    sock.sin_port = htons(port);
    sock.sin_addr.s_addr = inet_addr(host);
    ret = connect(sid, (struct sockaddr *)&sock, sizeof(struct sockaddr_in));
    if (ret == 0) {
        app_print("Connect successfully.\r\n");
        return sid;
    } else {
        app_print("Connect failed.\r\n");
        close(sid);
        return -2;
    }
}

/**
 ****************************************************************************************
 * @brief Get http responses code
 *
 * @param[in] httpbuf     Pointer to the http responses string
 * @return    Http responses code value
 ****************************************************************************************
 */
static int32_t http_rsp_code(uint8_t *httpbuf)
{
    int32_t response_code;
    char *p_start = NULL;
    char *p_end = NULL;
    char re_code[10] = {0};

    sys_memset(re_code, 0, sizeof(re_code));

    p_start = strstr((char *)httpbuf, "HTTP/1.");
    if (NULL == p_start)
        return -1;
    p_start += strlen("HTTP/1.1");
    while (*p_start == ' ')
        p_start++;

    p_end = strstr(p_start, " ");
    if (p_end) {
        if (p_end - p_start > sizeof(re_code))
            return -2;

        sys_memcpy(re_code, p_start, (p_end - p_start));
    }

    response_code = atoi(re_code);
    return response_code;
}

/**
 ****************************************************************************************
 * @brief Get http responses string header length
 *
 * @param[in] httpbuf     Pointer to the http responses string
 * @return    Http responses string header length value
 ****************************************************************************************
 */
static int32_t http_hdr_len(uint8_t *httpbuf)
{
    char *p_start = NULL;
    char *p_end = NULL;
    int32_t headlen = 0;

    p_start = (char *)httpbuf;
    p_end = strstr((char *)httpbuf, ENDING);
    if (p_end == NULL) {
        app_print("Can't not find the http head!\r\n");
        return 0;
    }
    p_end = p_end + strlen(ENDING);
    headlen = (p_end - p_start);

    return headlen;
}

/**
 ****************************************************************************************
 * @brief Get http responses string body length
 *
 * @param[in] httpbuf     Pointer to the http responses string
 * @return    Http responses string body length value
 ****************************************************************************************
 */
static int32_t http_body_len(uint8_t *httpbuf)
{
    char *p_start = NULL;
    char *p_end = NULL;
    char bodyLenbuf[10] = {0};
    int32_t bodylen = 0;  //Content-Length:

    p_start = strstr((char *)httpbuf, "Content-Length:");
    if (p_start == NULL)
        return 0;
    p_start = p_start + strlen("Content-Length:");
    while (*p_start == ' ')
        p_start++;
    p_end = strstr(p_start, TERM);
    if (p_end == NULL)
        return 0;

    sys_memcpy(bodyLenbuf, p_start, (p_end - p_start));
    bodylen = atoi(bodyLenbuf);

    return bodylen;
}

/**
 ****************************************************************************************
 * @brief Send get http responses image information
 *
 * @param[in] sid         Http socket id
 * @param[in] host        Pointer to the http host
 * @param[in] port        Pointer to the bin url
 * @return    Status code to know if processing is succeed or not
               -1         Malloc failed
               -2         Send failed
                0         Run success
 ****************************************************************************************
 */
static int32_t http_req_image(int32_t sid, char *host, uint16_t port, char *url)
{
    char *getBuf = NULL;
    int32_t totalLen = 0;
    int32_t ret;

    getBuf = (char *)sys_malloc(HTTP_GET_MAX_LEN);
    if (getBuf == NULL)
        return -1;

    snprintf(getBuf, HTTP_GET_MAX_LEN, "%s /%s %s%s%s%s:%d%s%s%s",
                                        "GET", url, "HTTP/1.1", TERM,
                                        "Host:", host, port, TERM,
                                        "Connection: keep-alive\r\n", ENDING);

    app_print("Send: %s", getBuf);
    totalLen = strlen(getBuf);

    /* Use SSL write for HTTPS, otherwise use regular send */
    if (ota_demo_cfg.use_ssl && g_ota_ssl_ctx) {
        ret = mbedtls_ssl_write(&g_ota_ssl_ctx->ssl, (const unsigned char *)getBuf, totalLen);
        if (ret > 0)
            ret = 0;
        else {
            app_print("SSL write failed: -0x%x\r\n", -ret);
            ret = -2;
        }
    } else {
        ret = send(sid, getBuf, totalLen, 0);
        if (ret > 0)
            ret = 0;
        else
            ret = -2;
    }

    sys_mfree(getBuf);
    return ret;
}

/**
 ****************************************************************************************
 * @brief Get http responses of the OTA image
 *
 * @param[in] sid         Http socket id
 * @param[in] running_idx Running image idx
 * @return    Status code to know if ota succeed or not
 *             -1         Malloc space fail
 *             -2         Get nothing from http service
 *             -3         Get http responses code is not 200
 *             -4         Received data length is unexpected
 *             -5         Write flash fail
 *             -6         Get data from http service fail
 *              0         Run success
 ****************************************************************************************
 */
static int32_t http_rsp_image(int32_t sid, uint32_t running_idx)
{
    uint8_t *recvbuf, *buf;
    int32_t recv_len, hdr_len, body_len, offset;
    uint32_t new_img_addr = 0xFFFFFFFF, erase_start_addr;
    int32_t ret = 0;
    uint32_t img_size;

    recvbuf = sys_malloc(RECBUFFER_LEN);
    if (recvbuf == NULL)
        return -1;

    sys_memset(recvbuf, 0, RECBUFFER_LEN);

    /* Use SSL read for HTTPS, otherwise use regular recv */
    if (ota_demo_cfg.use_ssl && g_ota_ssl_ctx) {
        recv_len = mbedtls_ssl_read(&g_ota_ssl_ctx->ssl, recvbuf, RECBUFFER_LEN);
        if (recv_len <= 0) {
            app_print("SSL read failed: -0x%x\r\n", -recv_len);
            ret = -2;
            goto Exit;
        }
    } else {
        recv_len = recv(sid, recvbuf, RECBUFFER_LEN, 0);
        if (recv_len <= 0) {
            ret = -2;
            goto Exit;
        }
    }

    if (200 != http_rsp_code(recvbuf)) {
        ret = -3;
        goto Exit;
    }

    if (running_idx == IMAGE_0) {
        new_img_addr = RE_IMG_1_OFFSET;
        img_size = RE_IMG_1_END - RE_IMG_1_OFFSET;
    } else {
        new_img_addr = RE_IMG_0_OFFSET;
        img_size = RE_IMG_1_OFFSET - RE_IMG_0_OFFSET;
    }

    app_print("HTTP response 200 ok\r\n");
    hdr_len = http_hdr_len(recvbuf);
    body_len = http_body_len(recvbuf);
    if (body_len > img_size) {
        app_print("Content too long: %d\r\n", body_len);
        ret = -4;
        goto Exit;
    }
    app_print("Content length: %d\r\n", body_len);

    offset = 0;
    buf = recvbuf + hdr_len;
    recv_len -= hdr_len;
    if (recv_len < 0) {
        ret = -5;
        goto Exit;
    }

    erase_start_addr = new_img_addr;
    do {
        if (recv_len > 0) {
            if (offset + recv_len > img_size) {   //flash security check
                app_print("received too long: %d\r\n", offset + recv_len);
                ret = -6;
                goto Exit;
            }

            while(offset + new_img_addr + recv_len > erase_start_addr) {
                ret = raw_flash_erase(erase_start_addr, 0x1000);
                if (ret != 0) {
                    goto Exit;
                } else {
                    erase_start_addr += 0x1000;
                }
            }
            app_print("Write to 0x%x with len %d\r\n", offset + new_img_addr, recv_len);
            ret = raw_flash_write((new_img_addr + offset), buf, recv_len);
            if (ret != 0) {
                goto Exit;
            }

            offset += recv_len;
        }
        recv_len = body_len - offset;

        if (0 == recv_len)
            break;

        if (recv_len > RECBUFFER_LEN)
            recv_len = RECBUFFER_LEN;

        /* Use SSL read for HTTPS, otherwise use regular recv */
        if (ota_demo_cfg.use_ssl && g_ota_ssl_ctx) {
            recv_len = mbedtls_ssl_read(&g_ota_ssl_ctx->ssl, recvbuf, recv_len);
            if (recv_len < 0 && recv_len != MBEDTLS_ERR_SSL_WANT_READ && recv_len != MBEDTLS_ERR_SSL_WANT_WRITE) {
                app_print("SSL read error: -0x%x\r\n", -recv_len);
                ret = -7;
                goto Exit;
            }
        } else {
            recv_len = recv(sid, recvbuf, recv_len, 0);
            if (recv_len < 0 && (recv_len != -EAGAIN && recv_len != -EWOULDBLOCK)) {
                app_print("Http socket recv error\r\n");
                ret = -7;
                goto Exit;
            }
        }
        buf = recvbuf;
    } while (offset < body_len);

Exit:
    sys_mfree(recvbuf);
    return ret;
}

/**
 ****************************************************************************************
 * @brief OTA demo task main loop
 ****************************************************************************************
 */
static void ota_demo_task(void)
{
    char *host = ota_demo_cfg.host;
    char *bin_name = ota_demo_cfg.image_url;
    uint32_t port = ota_demo_cfg.port;
    uint8_t running_idx = IMAGE_0;
    int32_t res;

    app_print("Start OTA test...\r\n");

    res = rom_sys_status_get(SYS_RUNNING_IMG, LEN_SYS_RUNNING_IMG, &running_idx);
    if (res < 0) {
        app_print("Get sys running idx failed! (res = %d)\r\n", res);
        goto Exit;
    }

    ota_demo_cfg.sockfd = http_socket_init(host, port);
    if (ota_demo_cfg.sockfd < 0) {
        app_print("Init socket failed! (sid = %d)\r\n", ota_demo_cfg.sockfd);
        goto Exit;
    }

    res = http_req_image(ota_demo_cfg.sockfd, host, ota_demo_cfg.port, bin_name);
    if (0 == res) {
        res = http_rsp_image(ota_demo_cfg.sockfd, running_idx);
        if (res < 0) {
            app_print("Get Firmware Reponse failed! (res = %d)\r\n", res);
            goto Exit;
        }
    } else {
        app_print("Request Firmware failed! (res = %d)\r\n", res);
        goto Exit;
    }

    /* Set image status */
    res = rom_sys_set_img_flag(running_idx, (IMG_FLAG_IA_MASK | IMG_FLAG_NEWER_MASK), (IMG_FLAG_IA_OK | IMG_FLAG_OLDER));
    res |= rom_sys_set_img_flag(!running_idx, (IMG_FLAG_IA_MASK | IMG_FLAG_VERIFY_MASK | IMG_FLAG_NEWER_MASK), 0);
    res |= rom_sys_set_img_flag(!running_idx, IMG_FLAG_NEWER_MASK, IMG_FLAG_NEWER);
    if (res != 0) {
        app_print("Set sys image status failed! (res = %d)\r\n", res);
        goto Exit;
    }

    app_print("Download new image successfully. Rebooting...\r\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    SysTimer_SoftwareReset();
Exit:
    /* Cleanup SSL context if using HTTPS */
    if (ota_demo_cfg.use_ssl) {
        ssl_cleanup();
    }

    /* Close socket for HTTP mode */
    if (ota_demo_cfg.sockfd >= 0 && !ota_demo_cfg.use_ssl)
        close(ota_demo_cfg.sockfd);

    sys_task_delete(NULL);
}

/**
 ****************************************************************************************
 * @brief Entry of ota demo task
 ****************************************************************************************
 */
int32_t ota_demo_start(void)
{
    /**
     * Test with Python3 HTTP Server: (copy the ota bin file to the following cmd running directory)
     * # python -m http.server 80 --bind HostIP
     */
    void *handle;

    handle = sys_task_create_dynamic((const uint8_t *)"ota_demo",
                    OTA_DEMO_STACK_SIZE, OS_TASK_PRIORITY(OTA_DEMO_TASK_PRIO),
                    (task_func_t)ota_demo_task, NULL);
    if (handle == NULL) {
        app_print("Create ota demo task failed.\r\n");
        return -1;
    }

    return 0;
}

#endif /* CONFIG_OTA_DEMO_SUPPORT */
