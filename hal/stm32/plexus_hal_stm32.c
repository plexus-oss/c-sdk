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
 *
 * *** SECURITY WARNING ***
 * Without TLS, the API key is transmitted in cleartext. Do NOT use plain
 * HTTP on untrusted networks. Either integrate mbedTLS or use a
 * TLS-terminating proxy. Set the endpoint to an http:// URL explicitly
 * to acknowledge this risk.
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

/* FreeRTOS detection — use osDelay() to yield CPU instead of busy-waiting */
#if __has_include("cmsis_os.h")
    #include "cmsis_os.h"
    #define PLEXUS_HAS_FREERTOS 1
#elif __has_include("cmsis_os2.h")
    #include "cmsis_os2.h"
    #define PLEXUS_HAS_FREERTOS 1
#elif defined(INCLUDE_vTaskDelay) || defined(configUSE_PREEMPTION)
    #include "FreeRTOS.h"
    #include "task.h"
    #define PLEXUS_HAS_FREERTOS 1
#else
    #define PLEXUS_HAS_FREERTOS 0
#endif

/* LwIP headers */
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

/* HTTP header buffer must fit: method + path + host + api_key + user-agent + fixed text.
 * Worst case: ~140 bytes fixed text + path(256) + host(128) + api_key(128) + UA(~30) = ~682.
 * Round up to 768 for headroom. */
#define PLEXUS_STM32_HEADER_BUF_SIZE 768

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
        long port = strtol(p, NULL, 10);
        if (port <= 0 || port > 65535) {
            return -1;
        }
        result->port = (uint16_t)port;
        while (*p && *p != '/') {
            p++;
        }
    }

    if (*p == '/') {
        strncpy(result->path, p, sizeof(result->path) - 1);
        result->path[sizeof(result->path) - 1] = '\0';
    } else {
        strcpy(result->path, "/");
    }

    return 0;
}

/**
 * Send all bytes on a stream socket, handling partial writes.
 * Returns 0 on success, -1 on error.
 */
static int send_all(int sock, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = lwip_send(sock, data + sent, len - sent, 0);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/**
 * Resolve hostname using getaddrinfo (thread-safe, unlike gethostbyname).
 * Connects socket and returns it. Returns -1 on failure.
 */
static int connect_to_host(const char* host, uint16_t port) {
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = NULL;
    if (lwip_getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
#if PLEXUS_DEBUG
        plexus_hal_log("DNS lookup failed for %s", host);
#endif
        return -1;
    }

    int sock = lwip_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        lwip_freeaddrinfo(res);
#if PLEXUS_DEBUG
        plexus_hal_log("Socket creation failed");
#endif
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = PLEXUS_HTTP_TIMEOUT_MS / 1000;
    tv.tv_usec = (PLEXUS_HTTP_TIMEOUT_MS % 1000) * 1000;
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int err = lwip_connect(sock, res->ai_addr, res->ai_addrlen);
    lwip_freeaddrinfo(res);

    if (err < 0) {
#if PLEXUS_DEBUG
        plexus_hal_log("Connection to %s:%u failed", host, (unsigned)port);
#endif
        lwip_close(sock);
        return -1;
    }

    return sock;
}

static int read_http_status(int sock) {
    /* Read the first chunk — status line is always in the first recv.
     * Using a single recv() instead of byte-by-byte avoids per-byte
     * syscall overhead on LwIP. */
    char buf[256];
    int n = lwip_recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';

    /* Parse "HTTP/1.x NNN ..." */
    int status_code = -1;
    const char* status_start = strchr(buf, ' ');
    if (status_start) {
        status_code = atoi(status_start + 1);
    }

    /* Drain remaining response with a short timeout.
     * Connection: close means the server will close after the response,
     * so we just need to wait for the FIN. */
    char drain_buf[256];
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000; /* 10ms — just catch data already in the buffer */
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (lwip_recv(sock, drain_buf, sizeof(drain_buf), 0) > 0) {
        /* Drain until timeout or connection closed */
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
        /* Log unconditionally — this is a critical misconfiguration */
        plexus_hal_log("ERROR: HTTPS not supported on STM32 without mbedTLS. "
                       "Set endpoint to http:// or integrate mbedTLS. "
                       "See TLS NOTE in plexus_hal_stm32.c");
        return PLEXUS_ERR_HAL;
    }

    int sock = connect_to_host(parsed.host, parsed.port);
    if (sock < 0) {
        return PLEXUS_ERR_NETWORK;
    }

    plexus_err_t result = PLEXUS_ERR_NETWORK;

    /* Build HTTP request */
    char header_buf[PLEXUS_STM32_HEADER_BUF_SIZE];
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

    if (send_all(sock, header_buf, (size_t)header_len) != 0) {
#if PLEXUS_DEBUG
        plexus_hal_log("Failed to send HTTP header");
#endif
        goto cleanup;
    }

    if (send_all(sock, body, body_len) != 0) {
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
    lwip_close(sock);
    return result;
}

#if PLEXUS_ENABLE_AUTO_REGISTER

plexus_err_t plexus_hal_http_post_response(
    const char* url, const char* api_key, const char* user_agent,
    const char* body, size_t body_len,
    char* response_buf, size_t response_buf_size, size_t* response_len) {
    if (!url || !api_key || !body || !response_buf || !response_len) {
        return PLEXUS_ERR_NULL_PTR;
    }

    *response_len = 0;

    parsed_url_t parsed;
    if (parse_url(url, &parsed) != 0) {
        return PLEXUS_ERR_HAL;
    }

    if (parsed.is_https) {
        plexus_hal_log("ERROR: HTTPS not supported on STM32 without mbedTLS");
        return PLEXUS_ERR_HAL;
    }

    int sock = connect_to_host(parsed.host, parsed.port);
    if (sock < 0) {
        return PLEXUS_ERR_NETWORK;
    }

    plexus_err_t result = PLEXUS_ERR_NETWORK;

    char header_buf[PLEXUS_STM32_HEADER_BUF_SIZE];
    int header_len = snprintf(header_buf, sizeof(header_buf),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "x-api-key: %s\r\n"
        "User-Agent: %s\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n",
        parsed.path, parsed.host, api_key,
        user_agent ? user_agent : "plexus-c-sdk",
        (unsigned int)body_len);

    if (header_len < 0 || header_len >= (int)sizeof(header_buf)) {
        result = PLEXUS_ERR_HAL;
        goto cleanup;
    }

    if (send_all(sock, header_buf, (size_t)header_len) != 0) goto cleanup;
    if (send_all(sock, body, body_len) != 0) goto cleanup;

    /* Read response with body (same approach as http_get) */
    {
        char recv_chunk[256];
        int total_read = 0;
        int buf_limit = (int)response_buf_size - 1;

        while (total_read < buf_limit) {
            int n = lwip_recv(sock, recv_chunk, sizeof(recv_chunk), 0);
            if (n <= 0) break;
            int to_copy = n;
            if (total_read + to_copy > buf_limit) to_copy = buf_limit - total_read;
            memcpy(response_buf + total_read, recv_chunk, to_copy);
            total_read += to_copy;
        }
        response_buf[total_read] = '\0';

        int status_code = -1;
        const char* status_start = strchr(response_buf, ' ');
        if (status_start) {
            status_code = atoi(status_start + 1);
        }

        /* Extract body after \r\n\r\n */
        const char* resp_body = strstr(response_buf, "\r\n\r\n");
        if (resp_body) {
            resp_body += 4;
            size_t resp_body_len = total_read - (resp_body - response_buf);
            memmove(response_buf, resp_body, resp_body_len);
            response_buf[resp_body_len] = '\0';
            *response_len = resp_body_len;
        }

        result = map_http_status(status_code);
    }

cleanup:
    lwip_close(sock);
    return result;
}

#endif /* PLEXUS_ENABLE_AUTO_REGISTER */

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
        plexus_hal_log("ERROR: HTTPS not supported on STM32 without mbedTLS. "
                       "Set endpoint to http:// or integrate mbedTLS. "
                       "See TLS NOTE in plexus_hal_stm32.c");
        return PLEXUS_ERR_HAL;
    }

    int sock = connect_to_host(parsed.host, parsed.port);
    if (sock < 0) {
        return PLEXUS_ERR_NETWORK;
    }

    plexus_err_t result = PLEXUS_ERR_NETWORK;

    /* Build GET request */
    char header_buf[PLEXUS_STM32_HEADER_BUF_SIZE];
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

    if (send_all(sock, header_buf, (size_t)header_len) != 0) {
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
    lwip_close(sock);
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
#if PLEXUS_HAS_FREERTOS
    /* Yield CPU to other tasks during retry backoff */
    osDelay(ms);
#else
    /* Bare-metal: busy-wait via SysTick */
    HAL_Delay(ms);
#endif
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

/* ========================================================================= */
/* Thread safety: CMSIS-OS mutex (FreeRTOS) or no-op (bare-metal)            */
/* ========================================================================= */

#if PLEXUS_ENABLE_THREAD_SAFE

#if PLEXUS_HAS_FREERTOS

void* plexus_hal_mutex_create(void) {
    osMutexDef(plexus_mtx);
    return (void*)osMutexCreate(osMutex(plexus_mtx));
}

void plexus_hal_mutex_lock(void* mutex) {
    if (mutex) {
        osMutexWait((osMutexId)mutex, osWaitForever);
    }
}

void plexus_hal_mutex_unlock(void* mutex) {
    if (mutex) {
        osMutexRelease((osMutexId)mutex);
    }
}

void plexus_hal_mutex_destroy(void* mutex) {
    if (mutex) {
        osMutexDelete((osMutexId)mutex);
    }
}

#else /* Bare-metal: no-op stubs */

void* plexus_hal_mutex_create(void) { return (void*)1; }
void  plexus_hal_mutex_lock(void* mutex) { (void)mutex; }
void  plexus_hal_mutex_unlock(void* mutex) { (void)mutex; }
void  plexus_hal_mutex_destroy(void* mutex) { (void)mutex; }

#endif /* PLEXUS_HAS_FREERTOS */

#endif /* PLEXUS_ENABLE_THREAD_SAFE */

#endif /* STM32 */
