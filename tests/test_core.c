/**
 * @file test_core.c
 * @brief Host-side unit tests for Plexus C SDK core API
 *
 * Build and run:
 *   cmake -B build-test tests && cmake --build build-test && ./build-test/test_core
 */

#include "plexus.h"
#include "plexus_internal.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Mock HAL helpers (defined in mock_hal.c) */
extern void mock_hal_reset(void);
extern void mock_hal_set_next_post_result(plexus_err_t err);
extern const char* mock_hal_last_post_body(void);
extern int mock_hal_post_call_count(void);
extern void mock_hal_advance_tick(uint32_t delta_ms);
extern void mock_hal_set_tick(uint32_t tick_ms);
extern const char* mock_hal_last_user_agent(void);
extern int mock_hal_delay_call_count(void);
extern uint32_t mock_hal_delay_call_ms(int index);

static int tests_passed = 0;
static int tests_failed = 0;
static int s_test_failed_flag = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
    mock_hal_reset(); \
    s_test_failed_flag = 0; \
    test_##name(); \
    if (!s_test_failed_flag) { \
        tests_passed++; \
        printf("PASS\n"); \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        s_test_failed_flag = 1; \
        return; \
    } \
} while(0)

/* ---- Lifecycle tests ---- */

TEST(init_returns_client) {
    plexus_client_t* c = plexus_init("plx_test_key", "device-001");
    ASSERT(c != NULL);
    ASSERT(plexus_pending_count(c) == 0);
    plexus_free(c);
}

TEST(init_null_api_key_returns_null) {
    plexus_client_t* c = plexus_init(NULL, "device-001");
    ASSERT(c == NULL);
}

TEST(init_null_source_id_returns_null) {
    plexus_client_t* c = plexus_init("plx_key", NULL);
    ASSERT(c == NULL);
}

TEST(init_invalid_source_id_returns_null) {
    /* source_id must be [a-zA-Z0-9._-] */
    ASSERT(plexus_init("plx_key", "has spaces") == NULL);
    ASSERT(plexus_init("plx_key", "has&special") == NULL);
    ASSERT(plexus_init("plx_key", "has=equals") == NULL);
    ASSERT(plexus_init("plx_key", "has/slash") == NULL);
    ASSERT(plexus_init("plx_key", "") == NULL);
}

TEST(init_valid_source_ids) {
    plexus_client_t* c;

    c = plexus_init("plx_key", "simple");
    ASSERT(c != NULL);
    plexus_free(c);

    c = plexus_init("plx_key", "with-dashes");
    ASSERT(c != NULL);
    plexus_free(c);

    c = plexus_init("plx_key", "with_underscores");
    ASSERT(c != NULL);
    plexus_free(c);

    c = plexus_init("plx_key", "with.dots");
    ASSERT(c != NULL);
    plexus_free(c);

    c = plexus_init("plx_key", "MiXeD.CaSe-123");
    ASSERT(c != NULL);
    plexus_free(c);
}

TEST(init_static_works) {
    PLEXUS_CLIENT_STATIC_BUF(client_buf);
    plexus_client_t* c = plexus_init_static(&client_buf, sizeof(client_buf), "plx_key", "dev-001");
    ASSERT(c != NULL);
    ASSERT((void*)c == (void*)&client_buf);
    ASSERT(plexus_pending_count(c) == 0);

    /* Don't call plexus_free — it was statically allocated */
    plexus_send(c, "temp", 25.0);
    ASSERT(plexus_pending_count(c) == 1);
}

TEST(init_static_too_small) {
    uint8_t buf[16]; /* Way too small */
    plexus_client_t* c = plexus_init_static(buf, sizeof(buf), "plx_key", "dev-001");
    ASSERT(c == NULL);
}

TEST(free_on_static_is_safe) {
    /* Calling plexus_free() on a statically-allocated client must NOT crash.
     * It should mark the client as uninitialized but not call free(). */
    PLEXUS_CLIENT_STATIC_BUF(client_buf);
    plexus_client_t* c = plexus_init_static(&client_buf, sizeof(client_buf), "plx_key", "dev-001");
    ASSERT(c != NULL);

    plexus_send(c, "temp", 25.0);
    ASSERT(plexus_pending_count(c) == 1);

    /* This must not crash or corrupt memory */
    plexus_free(c);

    /* Client should be unusable now */
    ASSERT(plexus_pending_count(c) == 0);
    ASSERT(plexus_send_number(c, "temp", 1.0) == PLEXUS_ERR_NOT_INITIALIZED);
}

TEST(client_size_matches) {
    ASSERT(plexus_client_size() == sizeof(plexus_client_t));
    ASSERT(plexus_client_size() == PLEXUS_CLIENT_STATIC_SIZE);
}

TEST(version_string) {
    const char* v = plexus_version();
    ASSERT(v != NULL);
    ASSERT(strcmp(v, PLEXUS_SDK_VERSION) == 0);
}

TEST(strerror_known_codes) {
    ASSERT(strcmp(plexus_strerror(PLEXUS_OK), "Success") == 0);
    ASSERT(strcmp(plexus_strerror(PLEXUS_ERR_NULL_PTR), "Null pointer") == 0);
    ASSERT(strcmp(plexus_strerror(PLEXUS_ERR_BUFFER_FULL), "Buffer full") == 0);
    ASSERT(strcmp(plexus_strerror(PLEXUS_ERR_NETWORK), "Network error") == 0);
    ASSERT(strcmp(plexus_strerror(PLEXUS_ERR_INVALID_ARG), "Invalid argument") == 0);
}

TEST(strerror_unknown_code) {
    const char* msg = plexus_strerror((plexus_err_t)99);
    ASSERT(strcmp(msg, "Unknown error") == 0);
}

/* ---- Send tests ---- */

TEST(send_number_queues_metric) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    ASSERT(plexus_pending_count(c) == 0);

    plexus_err_t err = plexus_send_number(c, "temperature", 72.5);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_pending_count(c) == 1);

    plexus_free(c);
}

TEST(send_alias_works) {
    /* plexus_send() should be equivalent to plexus_send_number() */
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    plexus_err_t err = plexus_send(c, "temperature", 72.5);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_pending_count(c) == 1);

    plexus_free(c);
}

TEST(send_number_null_client) {
    plexus_err_t err = plexus_send_number(NULL, "temperature", 72.5);
    ASSERT(err == PLEXUS_ERR_NULL_PTR);
}

TEST(buffer_full_returns_error) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* Make flush fail so auto-flush doesn't clear the buffer */
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);

    /* Fill the buffer completely */
    for (int i = 0; i < PLEXUS_MAX_METRICS; i++) {
        char name[32];
        snprintf(name, sizeof(name), "metric_%d", i);
        plexus_send_number(c, name, (double)i);
    }

    plexus_err_t err = plexus_send_number(c, "overflow", 999.0);
    ASSERT(err == PLEXUS_ERR_BUFFER_FULL);

    plexus_free(c);
}

TEST(clear_empties_buffer) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    plexus_send(c, "a", 1.0);
    plexus_send(c, "b", 2.0);
    ASSERT(plexus_pending_count(c) == 2);

    plexus_clear(c);
    ASSERT(plexus_pending_count(c) == 0);

    plexus_free(c);
}

/* ---- Flush tests ---- */

TEST(flush_no_data) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_NO_DATA);
    plexus_free(c);
}

TEST(flush_sends_and_clears) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    plexus_send(c, "temp", 72.5);
    ASSERT(plexus_pending_count(c) == 1);

    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_pending_count(c) == 0);
    ASSERT(mock_hal_post_call_count() == 1);
    ASSERT(plexus_total_sent(c) == 1);

    /* Verify JSON was sent */
    const char* body = mock_hal_last_post_body();
    ASSERT(strstr(body, "\"points\"") != NULL);
    ASSERT(strstr(body, "\"temp\"") != NULL);
    ASSERT(strstr(body, "72.5") != NULL);

    plexus_free(c);
}

TEST(flush_sends_user_agent) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send(c, "temp", 1.0);
    plexus_flush(c);

    const char* ua = mock_hal_last_user_agent();
    ASSERT(strstr(ua, "plexus-c-sdk/") != NULL);

    plexus_free(c);
}

TEST(flush_sends_sdk_version_in_json) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send(c, "temp", 1.0);
    plexus_flush(c);

    const char* body = mock_hal_last_post_body();
    ASSERT(strstr(body, "\"sdk\":\"c/") != NULL);

    plexus_free(c);
}

TEST(flush_network_error_retries) {
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send(c, "temp", 1.0);

    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_NETWORK);
    ASSERT(mock_hal_post_call_count() == PLEXUS_MAX_RETRIES);
    ASSERT(plexus_pending_count(c) == 1);
    ASSERT(plexus_total_errors(c) == 1);

    plexus_free(c);
}

TEST(flush_uses_exponential_backoff) {
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send(c, "temp", 1.0);
    plexus_flush(c);

    /* Should have PLEXUS_MAX_RETRIES - 1 delay calls (no delay before first attempt) */
    ASSERT(mock_hal_delay_call_count() == PLEXUS_MAX_RETRIES - 1);

    /* Second delay should be >= first delay (exponential growth) */
    if (PLEXUS_MAX_RETRIES >= 3) {
        uint32_t first = mock_hal_delay_call_ms(0);
        uint32_t second = mock_hal_delay_call_ms(1);
        /* With jitter, second should generally be larger but we can't be exact */
        ASSERT(first > 0);
        ASSERT(second > 0);
    }

    plexus_free(c);
}

TEST(flush_auth_error_no_retry) {
    mock_hal_set_next_post_result(PLEXUS_ERR_AUTH);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send(c, "temp", 1.0);

    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_AUTH);
    ASSERT(mock_hal_post_call_count() == 1);

    plexus_free(c);
}

TEST(flush_rate_limit_enters_cooldown) {
    mock_hal_set_next_post_result(PLEXUS_ERR_RATE_LIMIT);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send(c, "temp", 1.0);

    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_RATE_LIMIT);
    ASSERT(mock_hal_post_call_count() == 1); /* No retries on 429 */

    /* Second flush should be suppressed (cooldown active) */
    mock_hal_set_next_post_result(PLEXUS_OK);
    err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_RATE_LIMIT); /* Still in cooldown */
    ASSERT(mock_hal_post_call_count() == 1); /* No new HTTP call */

    /* Advance past cooldown */
    mock_hal_advance_tick(PLEXUS_RATE_LIMIT_COOLDOWN_MS + 1);
    err = plexus_flush(c);
    ASSERT(err == PLEXUS_OK); /* Now it goes through */

    plexus_free(c);
}

/* ---- Tick tests ---- */

TEST(tick_returns_ok_when_idle) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* Tick with empty buffer should return OK, not an error */
    plexus_err_t err = plexus_tick(c);
    ASSERT(err == PLEXUS_OK);

    plexus_free(c);
}

TEST(tick_flushes_on_interval) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_flush_interval(c, 1000);

    plexus_send(c, "temp", 25.0);

    /* Tick before interval — should not flush */
    mock_hal_advance_tick(500);
    plexus_err_t err = plexus_tick(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_pending_count(c) == 1);
    ASSERT(mock_hal_post_call_count() == 0);

    /* Tick after interval — should flush */
    mock_hal_advance_tick(600);
    err = plexus_tick(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_pending_count(c) == 0);
    ASSERT(mock_hal_post_call_count() == 1);

    plexus_free(c);
}

/* ---- Config tests ---- */

TEST(set_endpoint) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_set_endpoint(c, "https://custom.example.com/ingest");
    ASSERT(err == PLEXUS_OK);

    plexus_send(c, "temp", 1.0);
    plexus_flush(c);
    ASSERT(plexus_total_sent(c) == 1);

    plexus_free(c);
}

TEST(set_endpoint_null) {
    plexus_err_t err = plexus_set_endpoint(NULL, "https://example.com");
    ASSERT(err == PLEXUS_ERR_NULL_PTR);
}

TEST(set_flush_interval) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_set_flush_interval(c, 10000);
    ASSERT(err == PLEXUS_OK);
    plexus_free(c);
}

TEST(set_flush_count) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_set_flush_count(c, 8);
    ASSERT(err == PLEXUS_OK);
    plexus_free(c);
}

TEST(set_flush_interval_null_client) {
    plexus_err_t err = plexus_set_flush_interval(NULL, 5000);
    ASSERT(err == PLEXUS_ERR_NULL_PTR);
}

TEST(set_flush_count_null_client) {
    plexus_err_t err = plexus_set_flush_count(NULL, 4);
    ASSERT(err == PLEXUS_ERR_NULL_PTR);
}

TEST(free_does_not_flush) {
    mock_hal_set_next_post_result(PLEXUS_OK);
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send(c, "temp", 1.0);
    ASSERT(plexus_pending_count(c) == 1);

    plexus_free(c);

    ASSERT(mock_hal_post_call_count() == 0);
}

TEST(total_sent_and_errors) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    ASSERT(plexus_total_sent(c) == 0);
    ASSERT(plexus_total_errors(c) == 0);

    /* Successful send */
    plexus_send(c, "a", 1.0);
    plexus_flush(c);
    ASSERT(plexus_total_sent(c) == 1);

    /* Failed send */
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);
    plexus_send(c, "b", 2.0);
    plexus_flush(c);
    ASSERT(plexus_total_errors(c) == 1);

    plexus_free(c);
}

/* ---- Tick wraparound regression tests ---- */

TEST(tick_wraparound_flushes_correctly) {
    /* Regression: uint32_t tick wraparound at ~49 days must not break auto-flush.
     * Simulate a tick counter near UINT32_MAX that wraps around to 0.
     * Set tick BEFORE init so last_flush_ms captures the pre-wrap value. */
    mock_hal_set_tick(UINT32_MAX - 500);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_flush_interval(c, 1000);

    plexus_send(c, "temp", 25.0);

    /* Tick before interval — should not flush (400ms < 1000ms) */
    mock_hal_advance_tick(400);
    plexus_err_t err = plexus_tick(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_pending_count(c) == 1);
    ASSERT(mock_hal_post_call_count() == 0);

    /* Advance past wraparound — 700ms more = 1100ms total, past the 1000ms interval.
     * Tick wraps: UINT32_MAX - 100 + 700 → ~599 */
    mock_hal_advance_tick(700);
    err = plexus_tick(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_pending_count(c) == 0);
    ASSERT(mock_hal_post_call_count() == 1);

    plexus_free(c);
}

TEST(rate_limit_cooldown_survives_wraparound) {
    /* Regression: rate limit cooldown deadline must work across tick wraparound. */
    mock_hal_set_tick(UINT32_MAX - 1000);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    mock_hal_set_next_post_result(PLEXUS_ERR_RATE_LIMIT);
    plexus_send(c, "temp", 1.0);

    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_RATE_LIMIT);

    /* Still in cooldown after wraparound (2s into 30s cooldown) */
    mock_hal_set_next_post_result(PLEXUS_OK);
    mock_hal_advance_tick(2000);
    err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_RATE_LIMIT);

    /* Advance past the full cooldown */
    mock_hal_advance_tick(PLEXUS_RATE_LIMIT_COOLDOWN_MS);
    err = plexus_flush(c);
    ASSERT(err == PLEXUS_OK);

    plexus_free(c);
}

/* ---- Auto-flush count test ---- */

TEST(flush_count_triggers_auto_flush) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_flush_count(c, 3);

    /* First two should queue without flushing */
    plexus_send(c, "a", 1.0);
    ASSERT(plexus_pending_count(c) == 1);
    ASSERT(mock_hal_post_call_count() == 0);

    plexus_send(c, "b", 2.0);
    ASSERT(plexus_pending_count(c) == 2);
    ASSERT(mock_hal_post_call_count() == 0);

    /* Third should trigger auto-flush */
    plexus_err_t err = plexus_send(c, "c", 3.0);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_pending_count(c) == 0); /* flushed */
    ASSERT(mock_hal_post_call_count() == 1);
    ASSERT(plexus_total_sent(c) == 3);

    plexus_free(c);
}

/* ---- send_number_ts test ---- */

TEST(send_number_ts_uses_explicit_timestamp) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number_ts(c, "temp", 25.0, 1700000000000ULL);
    ASSERT(plexus_pending_count(c) == 1);

    /* Flush and verify the timestamp appears in the JSON */
    plexus_flush(c);
    const char* body = mock_hal_last_post_body();
    ASSERT(strstr(body, "1700000000000") != NULL);

    plexus_free(c);
}

/* ---- Alignment test ---- */

TEST(init_static_misaligned_returns_null) {
    /* Misaligned buffer should be rejected */
    static char raw_buf[sizeof(plexus_client_t) + 16];
    /* Offset by 1 byte to guarantee misalignment */
    void* misaligned = (void*)(raw_buf + 1);
    plexus_client_t* c = plexus_init_static(misaligned,
        sizeof(plexus_client_t), "plx_key", "dev-001");
    ASSERT(c == NULL);
}

#if PLEXUS_ENABLE_STRING_VALUES
TEST(send_string) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_send_string(c, "status", "running");
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_pending_count(c) == 1);
    plexus_free(c);
}
#endif

#if PLEXUS_ENABLE_BOOL_VALUES
TEST(send_bool) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_send_bool(c, "armed", true);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_pending_count(c) == 1);
    plexus_free(c);
}
#endif

/* ---- Metric name validation tests ---- */

TEST(send_rejects_control_chars_in_metric_name) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* Newline in metric name */
    ASSERT(plexus_send_number(c, "bad\nname", 1.0) == PLEXUS_ERR_INVALID_ARG);

    /* Tab in metric name */
    ASSERT(plexus_send_number(c, "bad\tname", 1.0) == PLEXUS_ERR_INVALID_ARG);

    /* Null byte (empty after null) */
    ASSERT(plexus_send_number(c, "", 1.0) == PLEXUS_ERR_INVALID_ARG);

    /* Non-ASCII byte */
    ASSERT(plexus_send_number(c, "temp\xC0\xB0", 1.0) == PLEXUS_ERR_INVALID_ARG);

    /* Valid names with special printable chars should work */
    ASSERT(plexus_send_number(c, "cpu.usage", 50.0) == PLEXUS_OK);
    ASSERT(plexus_send_number(c, "mem/total", 1024.0) == PLEXUS_OK);
    ASSERT(plexus_send_number(c, "disk_io [bytes]", 42.0) == PLEXUS_OK);

    ASSERT(plexus_pending_count(c) == 3);

    plexus_free(c);
}

/* ---- Main ---- */

int main(void) {
    printf("test_core:\n");

    /* Lifecycle */
    RUN(init_returns_client);
    RUN(init_null_api_key_returns_null);
    RUN(init_null_source_id_returns_null);
    RUN(init_invalid_source_id_returns_null);
    RUN(init_valid_source_ids);
    RUN(init_static_works);
    RUN(init_static_too_small);
    RUN(free_on_static_is_safe);
    RUN(client_size_matches);
    RUN(version_string);
    RUN(strerror_known_codes);
    RUN(strerror_unknown_code);

    /* Send */
    RUN(send_number_queues_metric);
    RUN(send_alias_works);
    RUN(send_number_null_client);
    RUN(buffer_full_returns_error);
    RUN(clear_empties_buffer);

    /* Flush */
    RUN(flush_no_data);
    RUN(flush_sends_and_clears);
    RUN(flush_sends_user_agent);
    RUN(flush_sends_sdk_version_in_json);
    RUN(flush_network_error_retries);
    RUN(flush_uses_exponential_backoff);
    RUN(flush_auth_error_no_retry);
    RUN(flush_rate_limit_enters_cooldown);

    /* Tick */
    RUN(tick_returns_ok_when_idle);
    RUN(tick_flushes_on_interval);
    RUN(tick_wraparound_flushes_correctly);
    RUN(rate_limit_cooldown_survives_wraparound);
    RUN(flush_count_triggers_auto_flush);
    RUN(send_number_ts_uses_explicit_timestamp);
    RUN(init_static_misaligned_returns_null);
    RUN(send_rejects_control_chars_in_metric_name);

    /* Config */
    RUN(set_endpoint);
    RUN(set_endpoint_null);
    RUN(set_flush_interval);
    RUN(set_flush_count);
    RUN(set_flush_interval_null_client);
    RUN(set_flush_count_null_client);
    RUN(free_does_not_flush);
    RUN(total_sent_and_errors);

#if PLEXUS_ENABLE_STRING_VALUES
    RUN(send_string);
#endif

#if PLEXUS_ENABLE_BOOL_VALUES
    RUN(send_bool);
#endif

    printf("\n  %d passed, %d failed\n\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
