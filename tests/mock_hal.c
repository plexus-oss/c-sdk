/**
 * @file mock_hal.c
 * @brief Mock HAL implementation for host-side testing
 *
 * Provides stub implementations of all HAL functions so tests
 * can compile and run on macOS/Linux without real hardware.
 */

#include "plexus.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Simulated tick counter */
static uint32_t s_tick_ms = 0;
static uint64_t s_time_ms = 1700000000000ULL; /* ~Nov 2023 in ms */

/* Track last HTTP POST for test assertions */
static char s_last_post_url[256] = {0};
static char s_last_post_body[PLEXUS_JSON_BUFFER_SIZE] = {0};
static size_t s_last_post_body_len = 0;
static plexus_err_t s_next_post_result = PLEXUS_OK;
static int s_post_call_count = 0;

/* ---- Test helpers (not part of HAL) ---- */

void mock_hal_reset(void) {
    s_tick_ms = 0;
    s_time_ms = 1700000000000ULL;
    s_last_post_url[0] = '\0';
    s_last_post_body[0] = '\0';
    s_last_post_body_len = 0;
    s_next_post_result = PLEXUS_OK;
    s_post_call_count = 0;
}

void mock_hal_set_tick(uint32_t tick_ms) {
    s_tick_ms = tick_ms;
}

void mock_hal_advance_tick(uint32_t delta_ms) {
    s_tick_ms += delta_ms;
}

void mock_hal_set_next_post_result(plexus_err_t err) {
    s_next_post_result = err;
}

const char* mock_hal_last_post_body(void) {
    return s_last_post_body;
}

size_t mock_hal_last_post_body_len(void) {
    return s_last_post_body_len;
}

int mock_hal_post_call_count(void) {
    return s_post_call_count;
}

/* ---- HAL function implementations ---- */

plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* body, size_t body_len) {
    (void)api_key;
    s_post_call_count++;

    if (url) {
        strncpy(s_last_post_url, url, sizeof(s_last_post_url) - 1);
    }
    if (body && body_len > 0 && body_len < sizeof(s_last_post_body)) {
        memcpy(s_last_post_body, body, body_len);
        s_last_post_body[body_len] = '\0';
        s_last_post_body_len = body_len;
    }

    return s_next_post_result;
}

#if PLEXUS_ENABLE_COMMANDS
plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
                                  char* response_buf, size_t buf_size,
                                  size_t* response_len) {
    (void)url;
    (void)api_key;
    if (response_buf && buf_size > 0) {
        const char* empty = "{\"commands\":[]}";
        size_t len = strlen(empty);
        if (len < buf_size) {
            memcpy(response_buf, empty, len + 1);
            if (response_len) *response_len = len;
        }
    }
    return PLEXUS_OK;
}
#endif

uint64_t plexus_hal_get_time_ms(void) {
    return s_time_ms++;
}

uint32_t plexus_hal_get_tick_ms(void) {
    return s_tick_ms;
}

void plexus_hal_delay_ms(uint32_t ms) {
    /* No-op in tests — don't actually wait */
    (void)ms;
}

void plexus_hal_log(const char* fmt, ...) {
    (void)fmt;
}

#if PLEXUS_ENABLE_PERSISTENT_BUFFER
static char s_storage_data[PLEXUS_JSON_BUFFER_SIZE] = {0};
static size_t s_storage_len = 0;
static bool s_storage_has_data = false;

plexus_err_t plexus_hal_storage_write(const char* key, const void* data, size_t len) {
    (void)key;
    if (len > sizeof(s_storage_data)) return PLEXUS_ERR_HAL;
    memcpy(s_storage_data, data, len);
    s_storage_len = len;
    s_storage_has_data = true;
    return PLEXUS_OK;
}

plexus_err_t plexus_hal_storage_read(const char* key, void* data, size_t max_len, size_t* out_len) {
    (void)key;
    if (!s_storage_has_data) {
        if (out_len) *out_len = 0;
        return PLEXUS_ERR_HAL;
    }
    size_t copy_len = s_storage_len < max_len ? s_storage_len : max_len;
    memcpy(data, s_storage_data, copy_len);
    if (out_len) *out_len = copy_len;
    return PLEXUS_OK;
}

plexus_err_t plexus_hal_storage_clear(const char* key) {
    (void)key;
    s_storage_has_data = false;
    s_storage_len = 0;
    return PLEXUS_OK;
}
#endif
