/**
 * @file test_threadsafe.c
 * @brief Tests for thread safety (mutex wrapping) feature
 *
 * Build: cmake -B build-test tests/ && cmake --build build-test && ./build-test/test_threadsafe
 * Requires: -DPLEXUS_ENABLE_THREAD_SAFE=1
 */

#include "plexus.h"
#include "plexus_internal.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Mock HAL helpers */
extern void mock_hal_reset(void);
extern void mock_hal_set_next_post_result(plexus_err_t err);
extern int mock_hal_mutex_lock_count(void);
extern int mock_hal_mutex_unlock_count(void);
extern void mock_hal_mutex_reset(void);

static int tests_passed = 0;
static int tests_failed = 0;
static int s_test_failed_flag = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
    mock_hal_reset(); \
    mock_hal_mutex_reset(); \
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

/* ---- Tests ---- */

TEST(send_acquires_and_releases_mutex) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    mock_hal_mutex_reset();

    plexus_send_number(c, "temp", 25.0);

    ASSERT(mock_hal_mutex_lock_count() > 0);
    ASSERT(mock_hal_mutex_lock_count() == mock_hal_mutex_unlock_count());

    plexus_free(c);
}

TEST(flush_acquires_and_releases_mutex) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "temp", 25.0);
    mock_hal_mutex_reset();

    plexus_flush(c);

    ASSERT(mock_hal_mutex_lock_count() > 0);
    ASSERT(mock_hal_mutex_lock_count() == mock_hal_mutex_unlock_count());

    plexus_free(c);
}

TEST(clear_acquires_and_releases_mutex) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "temp", 25.0);
    mock_hal_mutex_reset();

    plexus_clear(c);

    ASSERT(mock_hal_mutex_lock_count() > 0);
    ASSERT(mock_hal_mutex_lock_count() == mock_hal_mutex_unlock_count());

    plexus_free(c);
}

TEST(set_endpoint_acquires_mutex) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    mock_hal_mutex_reset();

    plexus_set_endpoint(c, "https://custom.example.com/api/ingest");

    ASSERT(mock_hal_mutex_lock_count() > 0);
    ASSERT(mock_hal_mutex_lock_count() == mock_hal_mutex_unlock_count());

    plexus_free(c);
}

TEST(mutex_balanced_on_flush_error) {
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "temp", 25.0);
    mock_hal_mutex_reset();

    plexus_flush(c);

    /* Even on error, locks and unlocks must be balanced */
    ASSERT(mock_hal_mutex_lock_count() == mock_hal_mutex_unlock_count());

    plexus_free(c);
}

TEST(mutex_balanced_on_no_data_flush) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    mock_hal_mutex_reset();

    plexus_flush(c);

    /* Flush with no data acquires and releases */
    ASSERT(mock_hal_mutex_lock_count() == mock_hal_mutex_unlock_count());

    plexus_free(c);
}

/* ---- Main ---- */

int main(void) {
    printf("test_threadsafe:\n");

    RUN(send_acquires_and_releases_mutex);
    RUN(flush_acquires_and_releases_mutex);
    RUN(clear_acquires_and_releases_mutex);
    RUN(set_endpoint_acquires_mutex);
    RUN(mutex_balanced_on_flush_error);
    RUN(mutex_balanced_on_no_data_flush);

    printf("\n  %d passed, %d failed\n\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
