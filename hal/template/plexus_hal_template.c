/**
 * @file plexus_hal_template.c
 * @brief HAL porting template for Plexus C SDK
 *
 * Copy this file to implement Plexus support for a new platform.
 *
 * Steps:
 *   1. Copy this file to hal/<your_platform>/plexus_hal_<your_platform>.c
 *   2. Replace the platform guard below with your platform's define
 *   3. Implement each function (see contract requirements in comments)
 *   4. Update CMakeLists.txt to include your HAL source
 *   5. Update library.json srcFilter if supporting PlatformIO
 *   6. Add a build step to .github/workflows/ci.yml
 *
 * Function categories:
 *   REQUIRED — SDK will not function without these
 *   OPTIONAL — SDK works without these (graceful fallback)
 *
 * Reference implementations:
 *   - ESP32 (ESP-IDF):  hal/esp32/plexus_hal_esp32.c
 *   - Arduino:          hal/arduino/plexus_hal_arduino.cpp
 *   - STM32 (LwIP):    hal/stm32/plexus_hal_stm32.c
 */

#include "plexus.h"

/* Replace MY_PLATFORM with your platform define (e.g., NRF52, RP2040, ZEPHYR) */
#if defined(MY_PLATFORM)

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Add your platform-specific headers here:
 *   #include "your_platform_hal.h"
 *   #include "your_http_library.h"
 */

/* ========================================================================= */
/* REQUIRED: HTTP POST                                                       */
/* ========================================================================= */

/**
 * Send an HTTP POST request with a JSON body.
 *
 * This is the primary network function — the SDK cannot send telemetry
 * without it.
 *
 * Contract:
 *   - MUST send body as-is (it is already valid JSON)
 *   - MUST set headers: Content-Type: application/json, x-api-key: <api_key>
 *   - MUST set header: User-Agent: <user_agent>
 *   - MUST return PLEXUS_OK             on HTTP 2xx
 *   - MUST return PLEXUS_ERR_AUTH       on HTTP 401
 *   - MUST return PLEXUS_ERR_BILLING    on HTTP 402
 *   - MUST return PLEXUS_ERR_FORBIDDEN  on HTTP 403
 *   - MUST return PLEXUS_ERR_RATE_LIMIT on HTTP 429
 *   - MUST return PLEXUS_ERR_SERVER     on HTTP 5xx
 *   - MUST return PLEXUS_ERR_NETWORK    on connection failure or other errors
 *   - SHOULD respect PLEXUS_HTTP_TIMEOUT_MS for connect/read timeouts
 *
 * @param url        Full URL (e.g., "https://app.plexus.company/api/ingest")
 * @param api_key    API key string for the x-api-key header
 * @param user_agent User-Agent header value (e.g., "plexus-c-sdk/0.5.0")
 * @param body       JSON request body (null-terminated)
 * @param body_len   Length of body in bytes (excluding null terminator)
 * @return           PLEXUS_OK on success, error code on failure
 */
plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* user_agent,
                                   const char* body, size_t body_len) {
    if (!url || !api_key || !body) {
        return PLEXUS_ERR_NULL_PTR;
    }

    /* TODO: Implement HTTP POST for your platform
     *
     * Typical steps:
     *   1. Parse URL to extract host, port, path
     *   2. Resolve hostname via DNS
     *   3. Open TCP connection (with TLS if HTTPS)
     *   4. Send HTTP POST with headers and body
     *   5. Read response status code
     *   6. Close connection
     *   7. Map HTTP status to plexus_err_t
     */

    (void)user_agent;
    (void)body_len;
    return PLEXUS_ERR_HAL;
}

/* ========================================================================= */
/* REQUIRED: Timestamps                                                      */
/* ========================================================================= */

/**
 * Get current wall-clock time in milliseconds since Unix epoch.
 *
 * @return Unix timestamp in milliseconds, or 0 if unavailable
 */
uint64_t plexus_hal_get_time_ms(void) {
    /* TODO: Return epoch milliseconds from RTC, NTP, or SNTP */
    return 0;
}

/**
 * Get monotonic tick count in milliseconds since boot.
 *
 * Contract:
 *   - MUST return a monotonically increasing millisecond counter
 *   - MUST NOT return wall-clock time
 *   - Wrapping at UINT32_MAX is expected and handled by the SDK
 *
 * @return Milliseconds since boot/start
 */
uint32_t plexus_hal_get_tick_ms(void) {
    /* TODO: Return monotonic milliseconds */
    return 0;
}

/* ========================================================================= */
/* REQUIRED: Delay                                                           */
/* ========================================================================= */

/**
 * Block for the specified number of milliseconds.
 *
 * Used between retry attempts. On RTOS platforms, this should yield the CPU.
 *
 * @param ms Milliseconds to delay
 */
void plexus_hal_delay_ms(uint32_t ms) {
    (void)ms;
}

/* ========================================================================= */
/* OPTIONAL: Debug logging                                                   */
/* ========================================================================= */

/**
 * Output a debug log message (printf-style).
 *
 * Only called when PLEXUS_DEBUG=1. Can be a no-op.
 */
void plexus_hal_log(const char* fmt, ...) {
#if PLEXUS_DEBUG
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* TODO: Output buf to your debug channel */
#else
    (void)fmt;
#endif
}

/* ========================================================================= */
/* OPTIONAL: Persistent storage (only if PLEXUS_ENABLE_PERSISTENT_BUFFER=1)  */
/* ========================================================================= */

#if PLEXUS_ENABLE_PERSISTENT_BUFFER

/**
 * Write data to persistent (flash/EEPROM) storage.
 *
 * @param key   Storage key (e.g., "plexus_meta", "plexus_b0")
 * @param data  Pointer to data to write
 * @param len   Number of bytes to write
 * @return      PLEXUS_OK on success, PLEXUS_ERR_HAL on failure
 */
plexus_err_t plexus_hal_storage_write(const char* key, const void* data, size_t len) {
    (void)key;
    (void)data;
    (void)len;
    return PLEXUS_ERR_HAL;
}

/**
 * Read data from persistent storage.
 *
 * Contract:
 *   - MUST set *out_len to 0 if key not found
 *   - MUST return PLEXUS_OK when key not found (it is not an error)
 *   - MUST NOT write more than max_len bytes to data
 *
 * @param key      Storage key
 * @param data     Buffer to read into
 * @param max_len  Size of the output buffer
 * @param out_len  Output: actual bytes read (0 if key not found)
 * @return         PLEXUS_OK on success (including key not found)
 */
plexus_err_t plexus_hal_storage_read(const char* key, void* data, size_t max_len, size_t* out_len) {
    (void)key;
    (void)data;
    (void)max_len;
    if (out_len) *out_len = 0;
    return PLEXUS_OK;
}

/**
 * Clear data associated with a key from persistent storage.
 */
plexus_err_t plexus_hal_storage_clear(const char* key) {
    (void)key;
    return PLEXUS_ERR_HAL;
}

#endif /* PLEXUS_ENABLE_PERSISTENT_BUFFER */

/* ========================================================================= */
/* OPTIONAL: Thread safety (only if PLEXUS_ENABLE_THREAD_SAFE=1)             */
/* ========================================================================= */

#if PLEXUS_ENABLE_THREAD_SAFE

/**
 * Create a recursive mutex.
 * Must be recursive because plexus_send() → auto_flush() → plexus_flush()
 * can nest lock acquisitions.
 *
 * @return Opaque mutex handle, or NULL on failure
 */
void* plexus_hal_mutex_create(void) {
    /* TODO: Create a recursive mutex for your platform */
    return (void*)1; /* Non-NULL stub */
}

void plexus_hal_mutex_lock(void* mutex) {
    /* TODO: Acquire the mutex (block until available) */
    (void)mutex;
}

void plexus_hal_mutex_unlock(void* mutex) {
    /* TODO: Release the mutex */
    (void)mutex;
}

void plexus_hal_mutex_destroy(void* mutex) {
    /* TODO: Free mutex resources */
    (void)mutex;
}

#endif /* PLEXUS_ENABLE_THREAD_SAFE */

/* ========================================================================= */
/* Verification Checklist                                                    */
/* ========================================================================= */

/*
 * Before submitting your HAL implementation, verify:
 *
 * [ ] plexus_hal_http_post returns PLEXUS_ERR_AUTH on HTTP 401
 * [ ] plexus_hal_http_post returns PLEXUS_ERR_BILLING on HTTP 402
 * [ ] plexus_hal_http_post returns PLEXUS_ERR_FORBIDDEN on HTTP 403
 * [ ] plexus_hal_http_post returns PLEXUS_ERR_RATE_LIMIT on HTTP 429
 * [ ] plexus_hal_http_post returns PLEXUS_ERR_SERVER on HTTP 5xx
 * [ ] plexus_hal_http_post returns PLEXUS_ERR_NETWORK on connection failure
 * [ ] plexus_hal_http_post sets Content-Type: application/json header
 * [ ] plexus_hal_http_post sets x-api-key header
 * [ ] plexus_hal_http_post sets User-Agent header
 * [ ] plexus_hal_get_tick_ms returns monotonic milliseconds (not wall-clock)
 * [ ] plexus_hal_get_time_ms returns 0 if wall-clock unavailable (not garbage)
 * [ ] plexus_hal_delay_ms actually delays (not a no-op) for retry backoff
 * [ ] Host tests pass with your HAL stubbed into tests/mock_hal.c
 * [ ] Memory usage stays within your target (check with plexus_client_size())
 *
 * If implementing persistent storage:
 * [ ] plexus_hal_storage_read returns PLEXUS_OK with *out_len=0 when key not found
 * [ ] Data survives power cycle
 * [ ] Write/read round-trip preserves data exactly
 */

#endif /* MY_PLATFORM */
