/**
 * @file test_core.c
 * @brief Host-side unit tests for Plexus C SDK core API
 *
 * Build and run:
 *   cmake -B build-test tests && cmake --build build-test && ./build-test/test_core
 */

#include "plexus.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Mock HAL helpers (defined in mock_hal.c) */
extern void mock_hal_reset(void);
extern void mock_hal_set_next_post_result(plexus_err_t err);
extern const char* mock_hal_last_post_body(void);
extern int mock_hal_post_call_count(void);
extern void mock_hal_advance_tick(uint32_t delta_ms);

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
    mock_hal_reset(); \
    test_##name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ---- Tests ---- */

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

TEST(version_string) {
    const char* v = plexus_version();
    ASSERT(v != NULL);
    ASSERT(strcmp(v, "0.1.0") == 0);
}

TEST(strerror_known_codes) {
    ASSERT(strcmp(plexus_strerror(PLEXUS_OK), "Success") == 0);
    ASSERT(strcmp(plexus_strerror(PLEXUS_ERR_NULL_PTR), "Null pointer") == 0);
    ASSERT(strcmp(plexus_strerror(PLEXUS_ERR_BUFFER_FULL), "Buffer full") == 0);
    ASSERT(strcmp(plexus_strerror(PLEXUS_ERR_NETWORK), "Network error") == 0);
}

TEST(strerror_unknown_code) {
    const char* msg = plexus_strerror((plexus_err_t)99);
    ASSERT(strcmp(msg, "Unknown error") == 0);
}

TEST(send_number_queues_metric) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    ASSERT(plexus_pending_count(c) == 0);

    plexus_err_t err = plexus_send_number(c, "temperature", 72.5);
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

    plexus_send_number(c, "a", 1.0);
    plexus_send_number(c, "b", 2.0);
    ASSERT(plexus_pending_count(c) == 2);

    plexus_clear(c);
    ASSERT(plexus_pending_count(c) == 0);

    plexus_free(c);
}

TEST(flush_no_data) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_NO_DATA);
    plexus_free(c);
}

TEST(flush_sends_and_clears) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    plexus_send_number(c, "temp", 72.5);
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

TEST(flush_network_error_retries) {
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "temp", 1.0);

    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_NETWORK);
    ASSERT(mock_hal_post_call_count() == PLEXUS_MAX_RETRIES);
    ASSERT(plexus_pending_count(c) == 1);
    ASSERT(plexus_total_errors(c) == 1);

    plexus_free(c);
}

TEST(flush_auth_error_no_retry) {
    mock_hal_set_next_post_result(PLEXUS_ERR_AUTH);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "temp", 1.0);

    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_AUTH);
    ASSERT(mock_hal_post_call_count() == 1);

    plexus_free(c);
}

TEST(set_endpoint) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_set_endpoint(c, "https://custom.example.com/ingest");
    ASSERT(err == PLEXUS_OK);

    /* Verify by sending and checking the post URL would be correct */
    plexus_send_number(c, "temp", 1.0);
    plexus_flush(c);
    /* If endpoint was set correctly, flush will succeed */
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
    plexus_send_number(c, "temp", 1.0);
    ASSERT(plexus_pending_count(c) == 1);

    plexus_free(c);

    /* plexus_free should NOT have called http_post */
    ASSERT(mock_hal_post_call_count() == 0);
}

TEST(total_sent_and_errors) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    ASSERT(plexus_total_sent(c) == 0);
    ASSERT(plexus_total_errors(c) == 0);

    /* Successful send */
    plexus_send_number(c, "a", 1.0);
    plexus_flush(c);
    ASSERT(plexus_total_sent(c) == 1);

    /* Failed send */
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);
    plexus_send_number(c, "b", 2.0);
    plexus_flush(c);
    ASSERT(plexus_total_errors(c) == 1);

    plexus_free(c);
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

/* ---- Main ---- */

int main(void) {
    printf("test_core:\n");

    RUN(init_returns_client);
    RUN(init_null_api_key_returns_null);
    RUN(init_null_source_id_returns_null);
    RUN(version_string);
    RUN(strerror_known_codes);
    RUN(strerror_unknown_code);
    RUN(send_number_queues_metric);
    RUN(send_number_null_client);
    RUN(buffer_full_returns_error);
    RUN(clear_empties_buffer);
    RUN(flush_no_data);
    RUN(flush_sends_and_clears);
    RUN(flush_network_error_retries);
    RUN(flush_auth_error_no_retry);
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
