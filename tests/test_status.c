/**
 * @file test_status.c
 * @brief Tests for connection status callback feature
 *
 * Build: cmake -B build-test tests/ && cmake --build build-test && ./build-test/test_status
 * Requires: -DPLEXUS_ENABLE_STATUS_CALLBACK=1
 */

#include "plexus.h"
#include "plexus_internal.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Mock HAL helpers */
extern void mock_hal_reset(void);
extern void mock_hal_set_next_post_result(plexus_err_t err);
extern void mock_hal_advance_tick(uint32_t delta_ms);

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

/* Callback tracking */
static plexus_conn_status_t s_last_cb_status;
static int s_cb_call_count = 0;
static void* s_cb_user_data = NULL;

static void status_callback(plexus_conn_status_t status, void* user_data) {
    s_last_cb_status = status;
    s_cb_call_count++;
    s_cb_user_data = user_data;
}

static void reset_cb(void) {
    s_cb_call_count = 0;
    s_cb_user_data = NULL;
}

/* ---- Tests ---- */

TEST(initial_status_is_disconnected) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    ASSERT(plexus_get_status(c) == PLEXUS_STATUS_DISCONNECTED);
    plexus_free(c);
}

TEST(register_callback) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    int sentinel = 42;
    plexus_err_t err = plexus_on_status_change(c, status_callback, &sentinel);
    ASSERT(err == PLEXUS_OK);
    plexus_free(c);
}

TEST(callback_on_successful_flush) {
    reset_cb();
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    int sentinel = 99;
    plexus_on_status_change(c, status_callback, &sentinel);

    plexus_send(c, "temp", 25.0);
    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(s_cb_call_count == 1);
    ASSERT(s_last_cb_status == PLEXUS_STATUS_CONNECTED);
    ASSERT(s_cb_user_data == &sentinel);
    ASSERT(plexus_get_status(c) == PLEXUS_STATUS_CONNECTED);

    plexus_free(c);
}

TEST(callback_on_auth_failure) {
    reset_cb();
    mock_hal_set_next_post_result(PLEXUS_ERR_AUTH);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_on_status_change(c, status_callback, NULL);

    plexus_send(c, "temp", 25.0);
    plexus_flush(c);

    ASSERT(s_cb_call_count == 1);
    ASSERT(s_last_cb_status == PLEXUS_STATUS_AUTH_FAILED);
    ASSERT(plexus_get_status(c) == PLEXUS_STATUS_AUTH_FAILED);

    plexus_free(c);
}

TEST(callback_on_rate_limit) {
    reset_cb();
    mock_hal_set_next_post_result(PLEXUS_ERR_RATE_LIMIT);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_on_status_change(c, status_callback, NULL);

    plexus_send(c, "temp", 25.0);
    plexus_flush(c);

    ASSERT(s_cb_call_count == 1);
    ASSERT(s_last_cb_status == PLEXUS_STATUS_RATE_LIMITED);

    plexus_free(c);
}

TEST(callback_on_network_exhaustion) {
    reset_cb();
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_on_status_change(c, status_callback, NULL);

    /* First: establish CONNECTED status */
    plexus_send(c, "temp", 25.0);
    plexus_flush(c);
    ASSERT(s_cb_call_count == 1);
    ASSERT(s_last_cb_status == PLEXUS_STATUS_CONNECTED);

    /* Now fail with network error: CONNECTED -> DISCONNECTED */
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);
    plexus_send(c, "temp2", 30.0);
    plexus_flush(c);

    ASSERT(s_cb_call_count == 2);
    ASSERT(s_last_cb_status == PLEXUS_STATUS_DISCONNECTED);

    plexus_free(c);
}

TEST(callback_only_on_state_change) {
    reset_cb();
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_on_status_change(c, status_callback, NULL);

    /* First flush: DISCONNECTED -> CONNECTED */
    plexus_send(c, "a", 1.0);
    plexus_flush(c);
    ASSERT(s_cb_call_count == 1);

    /* Second flush: CONNECTED -> CONNECTED (no change, no callback) */
    plexus_send(c, "b", 2.0);
    plexus_flush(c);
    ASSERT(s_cb_call_count == 1); /* unchanged */

    /* Fail: CONNECTED -> DISCONNECTED */
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);
    plexus_send(c, "c", 3.0);
    plexus_flush(c);
    ASSERT(s_cb_call_count == 2);
    ASSERT(s_last_cb_status == PLEXUS_STATUS_DISCONNECTED);

    plexus_free(c);
}

/* ---- Main ---- */

int main(void) {
    printf("test_status:\n");

    RUN(initial_status_is_disconnected);
    RUN(register_callback);
    RUN(callback_on_successful_flush);
    RUN(callback_on_auth_failure);
    RUN(callback_on_rate_limit);
    RUN(callback_on_network_exhaustion);
    RUN(callback_only_on_state_change);

    printf("\n  %d passed, %d failed\n\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
