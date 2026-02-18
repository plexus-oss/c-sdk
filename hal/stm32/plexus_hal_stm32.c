/**
 * @file plexus_hal_stm32.c
 * @brief STM32 HAL implementation for Plexus C SDK
 *
 * This implementation targets STM32F4/F7/H7 series with LwIP middleware.
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
 * Note: This implementation uses HTTP (not HTTPS) for simplicity.
 * For HTTPS, integrate mbedTLS with LwIP's altcp_tls layer.
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

/* External handles (defined in main.c or CubeMX generated code) */
extern UART_HandleTypeDef huart2;  /* Debug UART - adjust as needed */
extern RTC_HandleTypeDef hrtc;     /* RTC handle - optional */

/* ------------------------------------------------------------------------- */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------- */

/**
 * URL parsing result
 */
typedef struct {
    char host[128];
    uint16_t port;
    char path[256];
    int is_https;
} parsed_url_t;

/**
 * Parse a URL into host, port, and path components
 *
 * @param url        Full URL (e.g., "http://example.com:8080/api/ingest")
 * @param result     Parsed result
 * @return           0 on success, -1 on error
 */
static int parse_url(const char* url, parsed_url_t* result) {
    if (!url || !result) {
        return -1;
    }

    memset(result, 0, sizeof(parsed_url_t));
    result->port = 80;  /* Default HTTP port */

    const char* p = url;

    /* Parse scheme */
    if (strncmp(p, "https://", 8) == 0) {
        result->is_https = 1;
        result->port = 443;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        result->is_https = 0;
        p += 7;
    } else {
        /* Assume HTTP if no scheme */
        result->is_https = 0;
    }

    /* Find end of host (port separator, path, or end of string) */
    const char* host_end = p;
    while (*host_end && *host_end != ':' && *host_end != '/') {
        host_end++;
    }

    /* Copy host */
    size_t host_len = host_end - p;
    if (host_len >= sizeof(result->host)) {
        return -1;
    }
    strncpy(result->host, p, host_len);
    result->host[host_len] = '\0';

    p = host_end;

    /* Parse port if present */
    if (*p == ':') {
        p++;
        result->port = (uint16_t)atoi(p);
        while (*p && *p != '/') {
            p++;
        }
    }

    /* Copy path (or default to /) */
    if (*p == '/') {
        strncpy(result->path, p, sizeof(result->path) - 1);
    } else {
        strcpy(result->path, "/");
    }

    return 0;
}

/**
 * Read HTTP response and extract status code
 *
 * @param sock       Socket to read from
 * @return           HTTP status code, or -1 on error
 */
static int read_http_status(int sock) {
    char buf[256];
    int total_read = 0;
    int status_code = -1;

    /* Read response header (at least the status line) */
    while (total_read < (int)sizeof(buf) - 1) {
        int n = lwip_recv(sock, buf + total_read, 1, 0);
        if (n <= 0) {
            break;
        }
        total_read += n;

        /* Check for end of status line */
        if (total_read >= 4 && buf[total_read - 1] == '\n') {
            break;
        }
    }

    buf[total_read] = '\0';

    /* Parse status line: "HTTP/1.x NNN ..." */
    if (total_read > 12) {
        const char* status_start = strchr(buf, ' ');
        if (status_start) {
            status_code = atoi(status_start + 1);
        }
    }

    /* Drain remaining response (we don't need the body) */
    char drain_buf[256];
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  /* 100ms timeout for draining */
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (lwip_recv(sock, drain_buf, sizeof(drain_buf), 0) > 0) {
        /* Keep reading until timeout or connection closed */
    }

    return status_code;
}

/* ------------------------------------------------------------------------- */
/* HAL Implementation                                                        */
/* ------------------------------------------------------------------------- */

/**
 * Perform HTTP POST request using LwIP sockets
 *
 * @param url      Target URL
 * @param api_key  API key for x-api-key header
 * @param body     JSON body to send
 * @param body_len Length of body
 * @return         PLEXUS_OK on 2xx response, error code otherwise
 */
plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* body, size_t body_len) {
    if (!url || !api_key || !body) {
        return PLEXUS_ERR_NULL_PTR;
    }

    /* Parse URL */
    parsed_url_t parsed;
    if (parse_url(url, &parsed) != 0) {
        return PLEXUS_ERR_HAL;
    }

    /* HTTPS not supported without mbedTLS integration.
     * Refuse to silently downgrade — API keys would be sent in plaintext. */
    if (parsed.is_https) {
        plexus_hal_log("ERROR: HTTPS not supported on STM32 without mbedTLS. "
                       "Use an http:// endpoint or integrate mbedTLS with LwIP altcp_tls.");
        return PLEXUS_ERR_HAL;
    }

    int sock = -1;
    plexus_err_t result = PLEXUS_ERR_NETWORK;

    /* Resolve hostname */
    struct hostent* host = lwip_gethostbyname(parsed.host);
    if (!host) {
#if PLEXUS_DEBUG
        plexus_hal_log("DNS lookup failed for %s", parsed.host);
#endif
        return PLEXUS_ERR_NETWORK;
    }

    /* Create socket */
    sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
#if PLEXUS_DEBUG
        plexus_hal_log("Socket creation failed");
#endif
        return PLEXUS_ERR_NETWORK;
    }

    /* Set socket timeouts */
    struct timeval tv;
    tv.tv_sec = PLEXUS_HTTP_TIMEOUT_MS / 1000;
    tv.tv_usec = (PLEXUS_HTTP_TIMEOUT_MS % 1000) * 1000;
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Connect to server */
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
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n",
        parsed.path,
        parsed.host,
        api_key,
        (unsigned int)body_len);

    if (header_len < 0 || header_len >= (int)sizeof(header_buf)) {
#if PLEXUS_DEBUG
        plexus_hal_log("Header buffer overflow");
#endif
        result = PLEXUS_ERR_HAL;
        goto cleanup;
    }

    /* Send header */
    if (lwip_send(sock, header_buf, header_len, 0) != header_len) {
#if PLEXUS_DEBUG
        plexus_hal_log("Failed to send HTTP header");
#endif
        goto cleanup;
    }

    /* Send body */
    if (lwip_send(sock, body, body_len, 0) != (int)body_len) {
#if PLEXUS_DEBUG
        plexus_hal_log("Failed to send HTTP body");
#endif
        goto cleanup;
    }

    /* Read response */
    int status_code = read_http_status(sock);

#if PLEXUS_DEBUG
    plexus_hal_log("HTTP response: %d", status_code);
#endif

    /* Map HTTP status to plexus error */
    if (status_code >= 200 && status_code < 300) {
        result = PLEXUS_OK;
    } else if (status_code == 401) {
        result = PLEXUS_ERR_AUTH;
    } else if (status_code == 429) {
        result = PLEXUS_ERR_RATE_LIMIT;
    } else if (status_code >= 500) {
        result = PLEXUS_ERR_SERVER;
    } else if (status_code >= 400) {
        result = PLEXUS_ERR_NETWORK;  /* Client error */
    } else {
        result = PLEXUS_ERR_NETWORK;  /* Unknown/parse error */
    }

cleanup:
    if (sock >= 0) {
        lwip_close(sock);
    }
    return result;
}

#if PLEXUS_ENABLE_COMMANDS

/**
 * Perform HTTP GET using LwIP sockets and return response body
 */
plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
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
                       "Use an http:// endpoint or integrate mbedTLS with LwIP altcp_tls.");
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
        "Connection: close\r\n"
        "\r\n",
        parsed.path,
        parsed.host,
        api_key);

    if (header_len < 0 || header_len >= (int)sizeof(header_buf)) {
        result = PLEXUS_ERR_HAL;
        goto cleanup;
    }

    if (lwip_send(sock, header_buf, header_len, 0) != header_len) {
        goto cleanup;
    }

    /* Read response: first find the end of headers (\r\n\r\n), then read body */
    {
        char recv_buf[256];
        int total_read = 0;
        int header_end = -1;
        char full_resp[2048];
        int full_len = 0;
        int status_code = -1;

        /* Read entire response */
        while (full_len < (int)sizeof(full_resp) - 1) {
            int n = lwip_recv(sock, recv_buf, sizeof(recv_buf), 0);
            if (n <= 0) break;
            if (full_len + n >= (int)sizeof(full_resp)) n = (int)sizeof(full_resp) - 1 - full_len;
            memcpy(full_resp + full_len, recv_buf, n);
            full_len += n;
        }
        full_resp[full_len] = '\0';

        /* Parse status code */
        const char* status_start = strchr(full_resp, ' ');
        if (status_start) {
            status_code = atoi(status_start + 1);
        }

        /* Find body start */
        const char* body = strstr(full_resp, "\r\n\r\n");
        if (body) {
            body += 4;
            size_t body_len = full_len - (body - full_resp);
            if (body_len >= buf_size) body_len = buf_size - 1;
            memcpy(response_buf, body, body_len);
            response_buf[body_len] = '\0';
            *response_len = body_len;
        }

        /* Map status */
        if (status_code >= 200 && status_code < 300) {
            result = PLEXUS_OK;
        } else if (status_code == 401) {
            result = PLEXUS_ERR_AUTH;
        } else if (status_code == 429) {
            result = PLEXUS_ERR_RATE_LIMIT;
        } else if (status_code >= 500) {
            result = PLEXUS_ERR_SERVER;
        }

        (void)total_read;
        (void)header_end;
    }

cleanup:
    if (sock >= 0) {
        lwip_close(sock);
    }
    return result;
}

#endif /* PLEXUS_ENABLE_COMMANDS */

/**
 * Get current time in milliseconds since Unix epoch
 *
 * Uses RTC if available and configured with a valid time.
 * Returns 0 if RTC not available (server will timestamp).
 */
uint64_t plexus_hal_get_time_ms(void) {
#ifdef HAL_RTC_MODULE_ENABLED
    RTC_TimeTypeDef time;
    RTC_DateTypeDef date;

    /* Get current time and date from RTC */
    if (HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK) {
        return 0;
    }
    /* Must read date after time to unlock RTC registers */
    if (HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK) {
        return 0;
    }

    /* Check if RTC has a valid time set (year > 0) */
    if (date.Year == 0) {
        return 0;  /* RTC not set, let server timestamp */
    }

    /* Convert to Unix timestamp */
    /* Note: This is a simplified calculation. For production,
     * use a proper time library or handle leap years correctly. */
    uint32_t year = 2000 + date.Year;
    uint32_t days = 0;

    /* Days from years since 1970 */
    for (uint32_t y = 1970; y < year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }

    /* Days from months */
    static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (uint8_t m = 1; m < date.Month && m <= 12; m++) {
        days += days_in_month[m - 1];
        /* Add leap day for February */
        if (m == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
            days++;
        }
    }

    /* Add days in current month */
    days += date.Date - 1;

    /* Convert to seconds and add time */
    uint64_t timestamp_s = (uint64_t)days * 86400ULL +
                           (uint64_t)time.Hours * 3600ULL +
                           (uint64_t)time.Minutes * 60ULL +
                           (uint64_t)time.Seconds;

    /* Convert to milliseconds */
    uint64_t timestamp_ms = timestamp_s * 1000ULL;

    /* Add subseconds if available (STM32 RTC can provide sub-second precision) */
    if (time.SecondFraction > 0) {
        uint32_t subsec_ms = ((time.SecondFraction - time.SubSeconds) * 1000) /
                            (time.SecondFraction + 1);
        timestamp_ms += subsec_ms;
    }

    return timestamp_ms;
#else
    /* RTC not enabled, return 0 to use server-side timestamp */
    return 0;
#endif
}

/**
 * Get monotonic tick count in milliseconds
 *
 * Uses HAL_GetTick() which returns ms since boot.
 * Note: This wraps around approximately every 49.7 days (2^32 ms).
 */
uint32_t plexus_hal_get_tick_ms(void) {
    return HAL_GetTick();
}

void plexus_hal_delay_ms(uint32_t ms) {
    HAL_Delay(ms);
}

/**
 * Debug logging via UART
 *
 * Outputs to UART2 by default. Adjust huart2 reference as needed
 * for your hardware configuration.
 */
void plexus_hal_log(const char* fmt, ...) {
#if PLEXUS_DEBUG
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf) - 2, fmt, args);
    va_end(args);

    if (len > 0) {
        /* Add newline */
        if (len < (int)sizeof(buf) - 2) {
            buf[len++] = '\r';
            buf[len++] = '\n';
        }

        /* Transmit via UART (blocking) */
        HAL_UART_Transmit(&huart2, (uint8_t*)buf, len, 100);

        /* Alternative: Use ITM/SWO for debugging via debugger
         * for (int i = 0; i < len; i++) {
         *     ITM_SendChar(buf[i]);
         * }
         */
    }
#else
    (void)fmt;
#endif
}

#endif /* STM32 */
