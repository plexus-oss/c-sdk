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
 *   - MUST return PLEXUS_OK        on HTTP 2xx
 *   - MUST return PLEXUS_ERR_AUTH   on HTTP 401
 *   - MUST return PLEXUS_ERR_RATE_LIMIT on HTTP 429
 *   - MUST return PLEXUS_ERR_SERVER on HTTP 5xx
 *   - MUST return PLEXUS_ERR_NETWORK on connection failure or other errors
 *   - SHOULD respect PLEXUS_HTTP_TIMEOUT_MS for connect/read timeouts
 *
 * @param url      Full URL (e.g., "https://app.plexus.company/api/ingest")
 * @param api_key  API key string for the x-api-key header
 * @param body     JSON request body (null-terminated)
 * @param body_len Length of body in bytes (excluding null terminator)
 * @return         PLEXUS_OK on success, error code on failure
 */
plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
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

    (void)body_len;
    return PLEXUS_ERR_HAL;
}

/* ========================================================================= */
/* OPTIONAL: HTTP GET (only needed if PLEXUS_ENABLE_COMMANDS=1)              */
/* ========================================================================= */

#if PLEXUS_ENABLE_COMMANDS
/**
 * Send an HTTP GET request and return the response body.
 *
 * Only required when command polling is enabled. The SDK uses this to fetch
 * pending commands from the server.
 *
 * Contract:
 *   - MUST set header: x-api-key: <api_key>
 *   - MUST write response body to response_buf (null-terminated)
 *   - MUST set *response_len to actual bytes written
 *   - MUST return PLEXUS_OK        on HTTP 2xx
 *   - MUST return PLEXUS_ERR_AUTH   on HTTP 401
 *   - Same HTTP status mapping as http_post
 *
 * @param url          Full URL
 * @param api_key      API key for x-api-key header
 * @param response_buf Buffer to write response body into
 * @param buf_size     Size of response_buf
 * @param response_len Output: actual bytes written to response_buf
 * @return             PLEXUS_OK on success, error code on failure
 */
plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
                                  char* response_buf, size_t buf_size,
                                  size_t* response_len) {
    if (!url || !api_key || !response_buf || !response_len) {
        return PLEXUS_ERR_NULL_PTR;
    }

    *response_len = 0;

    /* TODO: Implement HTTP GET for your platform */

    (void)buf_size;
    return PLEXUS_ERR_HAL;
}
#endif /* PLEXUS_ENABLE_COMMANDS */

/* ========================================================================= */
/* REQUIRED: Timestamps                                                      */
/* ========================================================================= */

/**
 * Get current wall-clock time in milliseconds since Unix epoch.
 *
 * Used to timestamp each metric. If your platform has no RTC or NTP,
 * return 0 — the server will assign a timestamp on arrival.
 *
 * Contract:
 *   - SHOULD return milliseconds since 1970-01-01 00:00:00 UTC
 *   - MAY return 0 if wall-clock time is unavailable
 *
 * @return Unix timestamp in milliseconds, or 0 if unavailable
 */
uint64_t plexus_hal_get_time_ms(void) {
    /* TODO: Return epoch milliseconds from RTC, NTP, or SNTP
     *
     * Examples:
     *   - ESP32: gettimeofday() after SNTP sync
     *   - STM32: HAL_RTC_GetTime() + date-to-epoch conversion
     *   - Zephyr: k_uptime_get() (monotonic only — return 0 for wall-clock)
     */
    return 0;
}

/**
 * Get monotonic tick count in milliseconds since boot.
 *
 * Used for flush interval timing and internal bookkeeping. Must be
 * monotonically increasing (except for natural 32-bit wrap at ~49.7 days).
 *
 * Contract:
 *   - MUST return a monotonically increasing millisecond counter
 *   - MUST NOT return wall-clock time (use plexus_hal_get_time_ms for that)
 *   - Wrapping at UINT32_MAX is expected and handled by the SDK
 *
 * @return Milliseconds since boot/start
 */
uint32_t plexus_hal_get_tick_ms(void) {
    /* TODO: Return monotonic milliseconds
     *
     * Examples:
     *   - Arduino: millis()
     *   - STM32:   HAL_GetTick()
     *   - Zephyr:  k_uptime_get_32()
     *   - FreeRTOS: xTaskGetTickCount() * portTICK_PERIOD_MS
     */
    return 0;
}

/* ========================================================================= */
/* OPTIONAL: Debug logging                                                   */
/* ========================================================================= */

/* ========================================================================= */
/* REQUIRED: Delay                                                           */
/* ========================================================================= */

/**
 * Block for the specified number of milliseconds.
 *
 * Used between retry attempts when plexus_flush() fails. Must actually
 * delay — returning immediately defeats the purpose of retry backoff.
 *
 * On RTOS platforms, this should yield the CPU (e.g., vTaskDelay).
 * On bare-metal, a busy-wait loop is acceptable.
 *
 * @param ms Milliseconds to delay
 */
void plexus_hal_delay_ms(uint32_t ms) {
    /* TODO: Implement delay for your platform
     *
     * Examples:
     *   - Arduino:   delay(ms)
     *   - STM32:     HAL_Delay(ms)
     *   - FreeRTOS:  vTaskDelay(pdMS_TO_TICKS(ms))
     *   - Zephyr:    k_msleep(ms)
     */
    (void)ms;
}

/* ========================================================================= */
/* OPTIONAL: Debug logging                                                   */
/* ========================================================================= */

/**
 * Output a debug log message.
 *
 * Only called when PLEXUS_DEBUG=1 is defined. Can be a no-op if your
 * platform has no debug output channel.
 *
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void plexus_hal_log(const char* fmt, ...) {
#if PLEXUS_DEBUG
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* TODO: Output buf to your debug channel
     *
     * Examples:
     *   - Arduino: Serial.println(buf)
     *   - STM32:   HAL_UART_Transmit(&huart2, buf, len, 100)
     *   - Zephyr:  printk("%s\n", buf)
     */
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
 * Called when all flush retries fail, to save unsent telemetry for later.
 * Data will be re-sent on the next plexus_flush() call.
 *
 * Contract:
 *   - MUST persist data across power cycles
 *   - MUST overwrite any previous data under the same key
 *   - Key is always "plexus_buf" (single buffer strategy)
 *   - Data size is at most PLEXUS_JSON_BUFFER_SIZE bytes
 *
 * @param key   Storage key (always "plexus_buf")
 * @param data  Pointer to data to write
 * @param len   Number of bytes to write
 * @return      PLEXUS_OK on success, PLEXUS_ERR_HAL on failure
 */
plexus_err_t plexus_hal_storage_write(const char* key, const void* data, size_t len) {
    /* TODO: Write to flash/EEPROM/NVS
     *
     * Examples:
     *   - ESP32: nvs_set_blob(handle, key, data, len)
     *   - STM32: HAL_FLASH_Program() or external EEPROM via I2C
     *   - Zephyr: settings_save_one(key, data, len)
     */
    (void)key;
    (void)data;
    (void)len;
    return PLEXUS_ERR_HAL;
}

/**
 * Read data from persistent storage.
 *
 * Called at the start of plexus_flush() to check for previously persisted data.
 *
 * Contract:
 *   - MUST set *out_len to 0 if key not found (this is not an error)
 *   - MUST NOT write more than max_len bytes to data
 *   - Return PLEXUS_OK even if key not found (just set out_len=0)
 *
 * @param key      Storage key
 * @param data     Buffer to read into
 * @param max_len  Size of the output buffer
 * @param out_len  Output: actual bytes read (0 if key not found)
 * @return         PLEXUS_OK on success (including key not found)
 */
plexus_err_t plexus_hal_storage_read(const char* key, void* data, size_t max_len, size_t* out_len) {
    /* TODO: Read from flash/EEPROM/NVS */
    (void)key;
    (void)data;
    (void)max_len;
    if (out_len) *out_len = 0;
    return PLEXUS_ERR_HAL;
}

/**
 * Clear data associated with a key from persistent storage.
 *
 * Called after persisted data is successfully sent.
 *
 * @param key  Storage key to erase
 * @return     PLEXUS_OK on success
 */
plexus_err_t plexus_hal_storage_clear(const char* key) {
    /* TODO: Erase from flash/EEPROM/NVS */
    (void)key;
    return PLEXUS_ERR_HAL;
}

#endif /* PLEXUS_ENABLE_PERSISTENT_BUFFER */

/* ========================================================================= */
/* Verification Checklist                                                    */
/* ========================================================================= */

/*
 * Before submitting your HAL implementation, verify:
 *
 * [ ] plexus_hal_http_post returns PLEXUS_ERR_AUTH on HTTP 401
 * [ ] plexus_hal_http_post returns PLEXUS_ERR_RATE_LIMIT on HTTP 429
 * [ ] plexus_hal_http_post returns PLEXUS_ERR_SERVER on HTTP 5xx
 * [ ] plexus_hal_http_post returns PLEXUS_ERR_NETWORK on connection failure
 * [ ] plexus_hal_http_post sets Content-Type: application/json header
 * [ ] plexus_hal_http_post sets x-api-key header
 * [ ] plexus_hal_get_tick_ms returns monotonic milliseconds (not wall-clock)
 * [ ] plexus_hal_get_time_ms returns 0 if wall-clock unavailable (not garbage)
 * [ ] plexus_hal_delay_ms actually delays (not a no-op) for retry backoff
 * [ ] Host tests pass with your HAL stubbed into tests/mock_hal.c
 * [ ] Memory usage stays within your target (check with sizeof(plexus_client_t))
 *
 * If implementing persistent storage:
 * [ ] plexus_hal_storage_read sets *out_len=0 when key not found
 * [ ] Data survives power cycle
 * [ ] Write/read round-trip preserves data exactly
 */

#endif /* MY_PLATFORM */
