/**
 * @file plexus_hal_stm32.c
 * @brief STM32 HAL implementation for Plexus C SDK
 *
 * Targets STM32F4/F7/H7 series with LwIP middleware.
 * Requires:
 * - STM32 HAL drivers
 * - LwIP with sockets API enabled
 * - Optional: RTC for timestamps, UART for debug logging
 *
 * LwIP configuration (lwipopts.h):
 *   #define LWIP_SOCKET      1
 *   #define LWIP_DNS         1
 *   #define LWIP_SO_RCVTIMEO 1
 *   #define LWIP_SO_SNDTIMEO 1
 *
 * *** TLS/HTTPS NOTE ***
 * This HAL uses plain HTTP. For HTTPS (required for production), you must
 * integrate mbedTLS with LwIP's altcp_tls layer:
 *   1. Enable mbedTLS in CubeMX (Middleware > mbedTLS)
 *   2. Configure altcp_tls in lwipopts.h:
 *        #define LWIP_ALTCP          1
 *        #define LWIP_ALTCP_TLS      1
 *        #define LWIP_ALTCP_TLS_MBEDTLS 1
 *   3. Replace lwip_socket/connect/send/recv with altcp equivalents
 *   4. Load the server's root CA certificate for verification
 *
 * Alternatively, use a TLS-terminating proxy on your network edge.
 */

#include "plexus.h"

#if defined(STM32F4) || defined(STM32F7) || defined(STM32H7) || defined(STM32_HAL)

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* STM32 HAL headers — select the correct header for your series */
#if defined(STM32F4)
    #include "stm32f4xx_hal.h"
#elif defined(STM32F7)
    #include "stm32f7xx_hal.h"
#elif defined(STM32H7)
    #include "stm32h7xx_hal.h"
#else
    #error "Define STM32F4, STM32F7, or STM32H7 for your target series"
#endif

/* LwIP headers */
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

/* Configurable peripheral handles.
 * Override these with -DPLEXUS_STM32_DEBUG_UART=huart3 etc. in your build. */
#ifndef PLEXUS_STM32_DEBUG_UART
#define PLEXUS_STM32_DEBUG_UART huart2
#endif

#ifndef PLEXUS_STM32_RTC
#define PLEXUS_STM32_RTC hrtc
#endif

extern UART_HandleTypeDef PLEXUS_STM32_DEBUG_UART;
extern RTC_HandleTypeDef PLEXUS_STM32_RTC;

/* ------------------------------------------------------------------------- */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------- */

typedef struct {
    char host[128];
    uint16_t port;
    char path[256];
    int is_https;
} parsed_url_t;

static int parse_url(const char* url, parsed_url_t* result) {
    if (!url || !result) {
        return -1;
    }

    memset(result, 0, sizeof(parsed_url_t));
    result->port = 80;

    const char* p = url;

    if (strncmp(p, "https://", 8) == 0) {
        result->is_https = 1;
        result->port = 443;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        result->is_https = 0;
        p += 7;
    } else {
        result->is_https = 0;
    }

    const char* host_end = p;
    while (*host_end && *host_end != ':' && *host_end != '/') {
        host_end++;
    }

    size_t host_len = host_end - p;
    if (host_len >= sizeof(result->host)) {
        return -1;
    }
    strncpy(result->host, p, host_len);
    result->host[host_len] = '\0';

    p = host_end;

    if (*p == ':') {
        p++;
        result->port = (uint16_t)atoi(p);
        while (*p && *p != '/') {
            p++;
        }
    }

    if (*p == '/') {
        strncpy(result->path, p, sizeof(result->path) - 1);
    } else {
        strcpy(result->path, "/");
    }

    return 0;
}

static int read_http_status(int sock) {
    char buf[256];
    int total_read = 0;

    while (total_read < (int)sizeof(buf) - 1) {
        int n = lwip_recv(sock, buf + total_read, 1, 0);
        if (n <= 0) {
            break;
        }
        total_read += n;

        if (total_read >= 4 && buf[total_read - 1] == '\n') {
            break;
        }
    }

    buf[total_read] = '\0';

    int status_code = -1;
    if (total_read > 12) {
        const char* status_start = strchr(buf, ' ');
        if (status_start) {
            status_code = atoi(status_start + 1);
        }
    }

    /* Drain remaining response */
    char drain_buf[256];
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (lwip_recv(sock, drain_buf, sizeof(drain_buf), 0) > 0) {
        /* Keep reading until timeout or connection closed */
    }

    return status_code;
}

static plexus_err_t map_http_status(int status_code) {
    if (status_code >= 200 && status_code < 300) return PLEXUS_OK;
    if (status_code == 401) return PLEXUS_ERR_AUTH;
    if (status_code == 429) return PLEXUS_ERR_RATE_LIMIT;
    if (status_code >= 500) return PLEXUS_ERR_SERVER;
    return PLEXUS_ERR_NETWORK;
}

/* ------------------------------------------------------------------------- */
/* HAL Implementation                                                        */
/* ------------------------------------------------------------------------- */

plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* user_agent,
                                   const char* body, size_t body_len) {
    if (!url || !api_key || !body) {
        return PLEXUS_ERR_NULL_PTR;
    }

    parsed_url_t parsed;
    if (parse_url(url, &parsed) != 0) {
        return PLEXUS_ERR_HAL;
    }

    if (parsed.is_https) {
        plexus_hal_log("HTTPS not supported without mbedTLS — see TLS NOTE in plexus_hal_stm32.c");
        return PLEXUS_ERR_HAL;
    }

    int sock = -1;
    plexus_err_t result = PLEXUS_ERR_NETWORK;

    struct hostent* host = lwip_gethostbyname(parsed.host);
    if (!host) {
#if PLEXUS_DEBUG
        plexus_hal_log("DNS lookup failed for %s", parsed.host);
#endif
        return PLEXUS_ERR_NETWORK;
    }

    sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
#if PLEXUS_DEBUG
        plexus_hal_log("Socket creation failed");
#endif
        return PLEXUS_ERR_NETWORK;
    }

    struct timeval tv;
    tv.tv_sec = PLEXUS_HTTP_TIMEOUT_MS / 1000;
    tv.tv_usec = (PLEXUS_HTTP_TIMEOUT_MS % 1000) * 1000;
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = lwip_htons(parsed.port);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    if (lwip_connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
#if PLEXUS_DEBUG
        plexus_hal_log("Connection to %s:%d failed", parsed.host, parsed.port);
#endif
        goto cleanup;
    }

    /* Build HTTP request */
    char header_buf[512];
    int header_len = snprintf(header_buf, sizeof(header_buf),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "x-api-key: %s\r\n"
        "User-Agent: %s\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n",
        parsed.path,
        parsed.host,
        api_key,
        user_agent ? user_agent : "plexus-c-sdk",
        (unsigned int)body_len);

    if (header_len < 0 || header_len >= (int)sizeof(header_buf)) {
#if PLEXUS_DEBUG
        plexus_hal_log("Header buffer overflow");
#endif
        result = PLEXUS_ERR_HAL;
        goto cleanup;
    }

    if (lwip_send(sock, header_buf, header_len, 0) != header_len) {
#if PLEXUS_DEBUG
        plexus_hal_log("Failed to send HTTP header");
#endif
        goto cleanup;
    }

    if (lwip_send(sock, body, body_len, 0) != (int)body_len) {
#if PLEXUS_DEBUG
        plexus_hal_log("Failed to send HTTP body");
#endif
        goto cleanup;
    }

    {
        int status_code = read_http_status(sock);
#if PLEXUS_DEBUG
        plexus_hal_log("HTTP response: %d", status_code);
#endif
        result = map_http_status(status_code);
    }

cleanup:
    if (sock >= 0) {
        lwip_close(sock);
    }
    return result;
}

#if PLEXUS_ENABLE_COMMANDS

plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
                                  const char* user_agent,
                                  char* response_buf, size_t buf_size,
                                  size_t* response_len) {
    if (!url || !api_key || !response_buf || !response_len) {
        return PLEXUS_ERR_NULL_PTR;
    }

    *response_len = 0;

    parsed_url_t parsed;
    if (parse_url(url, &parsed) != 0) {
        return PLEXUS_ERR_HAL;
    }

    if (parsed.is_https) {
        plexus_hal_log("HTTPS not supported without mbedTLS — see TLS NOTE in plexus_hal_stm32.c");
        return PLEXUS_ERR_HAL;
    }

    int sock = -1;
    plexus_err_t result = PLEXUS_ERR_NETWORK;

    struct hostent* host = lwip_gethostbyname(parsed.host);
    if (!host) return PLEXUS_ERR_NETWORK;

    sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return PLEXUS_ERR_NETWORK;

    struct timeval tv;
    tv.tv_sec = PLEXUS_HTTP_TIMEOUT_MS / 1000;
    tv.tv_usec = (PLEXUS_HTTP_TIMEOUT_MS % 1000) * 1000;
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = lwip_htons(parsed.port);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    if (lwip_connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        goto cleanup;
    }

    /* Build GET request */
    char header_buf[512];
    int header_len = snprintf(header_buf, sizeof(header_buf),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "x-api-key: %s\r\n"
        "User-Agent: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        parsed.path,
        parsed.host,
        api_key,
        user_agent ? user_agent : "plexus-c-sdk");

    if (header_len < 0 || header_len >= (int)sizeof(header_buf)) {
        result = PLEXUS_ERR_HAL;
        goto cleanup;
    }

    if (lwip_send(sock, header_buf, header_len, 0) != header_len) {
        goto cleanup;
    }

    /* Read response directly into the caller-provided buffer.
     * This avoids a large stack allocation — the caller's buffer
     * is typically the client's json_buffer (PLEXUS_JSON_BUFFER_SIZE). */
    {
        char recv_chunk[256];
        int total_read = 0;
        int buf_limit = (int)buf_size - 1;

        while (total_read < buf_limit) {
            int n = lwip_recv(sock, recv_chunk, sizeof(recv_chunk), 0);
            if (n <= 0) break;
            int to_copy = n;
            if (total_read + to_copy > buf_limit) to_copy = buf_limit - total_read;
            memcpy(response_buf + total_read, recv_chunk, to_copy);
            total_read += to_copy;
        }
        response_buf[total_read] = '\0';

        /* Parse status code from first line */
        int status_code = -1;
        const char* status_start = strchr(response_buf, ' ');
        if (status_start) {
            status_code = atoi(status_start + 1);
        }

        /* Find body start (after \r\n\r\n) and shift it to front of buffer */
        const char* body = strstr(response_buf, "\r\n\r\n");
        if (body) {
            body += 4;
            size_t body_len = total_read - (body - response_buf);
            memmove(response_buf, body, body_len);
            response_buf[body_len] = '\0';
            *response_len = body_len;
        }

        result = map_http_status(status_code);
    }

cleanup:
    if (sock >= 0) {
        lwip_close(sock);
    }
    return result;
}

#endif /* PLEXUS_ENABLE_COMMANDS */

uint64_t plexus_hal_get_time_ms(void) {
#ifdef HAL_RTC_MODULE_ENABLED
    RTC_TimeTypeDef time;
    RTC_DateTypeDef date;

    if (HAL_RTC_GetTime(&PLEXUS_STM32_RTC, &time, RTC_FORMAT_BIN) != HAL_OK) {
        return 0;
    }
    if (HAL_RTC_GetDate(&PLEXUS_STM32_RTC, &date, RTC_FORMAT_BIN) != HAL_OK) {
        return 0;
    }

    if (date.Year == 0) {
        return 0;
    }

    uint32_t year = 2000 + date.Year;
    uint32_t days = 0;

    for (uint32_t y = 1970; y < year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }

    static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (uint8_t m = 1; m < date.Month && m <= 12; m++) {
        days += days_in_month[m - 1];
        if (m == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
            days++;
        }
    }

    days += date.Date - 1;

    uint64_t timestamp_s = (uint64_t)days * 86400ULL +
                           (uint64_t)time.Hours * 3600ULL +
                           (uint64_t)time.Minutes * 60ULL +
                           (uint64_t)time.Seconds;

    uint64_t timestamp_ms = timestamp_s * 1000ULL;

    if (time.SecondFraction > 0) {
        uint32_t subsec_ms = ((time.SecondFraction - time.SubSeconds) * 1000) /
                            (time.SecondFraction + 1);
        timestamp_ms += subsec_ms;
    }

    return timestamp_ms;
#else
    return 0;
#endif
}

uint32_t plexus_hal_get_tick_ms(void) {
    return HAL_GetTick();
}

void plexus_hal_delay_ms(uint32_t ms) {
    HAL_Delay(ms);
}

void plexus_hal_log(const char* fmt, ...) {
#if PLEXUS_DEBUG
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf) - 2, fmt, args);
    va_end(args);

    if (len > 0) {
        if (len < (int)sizeof(buf) - 2) {
            buf[len++] = '\r';
            buf[len++] = '\n';
        }
        HAL_UART_Transmit(&PLEXUS_STM32_DEBUG_UART, (uint8_t*)buf, len, 100);
    }
#else
    (void)fmt;
#endif
}

#endif /* STM32 */
