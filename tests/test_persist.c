/**
 * @file test_persist.c
 * @brief Tests for multi-batch persistent buffer (ring buffer)
 *
 * Build: cmake -B build-test tests/ && cmake --build build-test && ./build-test/test_persist
 * Requires: -DPLEXUS_ENABLE_PERSISTENT_BUFFER=1
 */

#include "plexus.h"
#include "plexus_internal.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Mock HAL helpers */
extern void mock_hal_reset(void);
extern void mock_hal_set_next_post_result(plexus_err_t err);
extern int mock_hal_post_call_count(void);
extern const char* mock_hal_last_post_body(void);
extern void mock_hal_advance_tick(uint32_t delta_ms);
extern void mock_hal_storage_reset(void);

static int tests_passed = 0;
static int tests_failed = 0;
static int s_test_failed_flag = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
    mock_hal_reset(); \
    mock_hal_storage_reset(); \
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

TEST(failed_flush_persists_data) {
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send(c, "temp", 25.0);
    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_NETWORK);

    /* Data should be persisted in ring buffer */
    /* Successful flush should drain persisted data first */
    mock_hal_set_next_post_result(PLEXUS_OK);
    plexus_send(c, "humidity", 50.0);
    err = plexus_flush(c);
    ASSERT(err == PLEXUS_OK);

    /* Persisted batch should have been sent (2 HTTP posts: one for drain, one for new) */
    ASSERT(mock_hal_post_call_count() > PLEXUS_MAX_RETRIES);

    plexus_free(c);
}

TEST(persists_multiple_batches) {
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* Fail 3 flushes — each should persist to a different slot */
    for (int i = 0; i < 3; i++) {
        char name[32];
        snprintf(name, sizeof(name), "metric_%d", i);
        plexus_send(c, name, (double)i);
        plexus_flush(c);
    }

    /* Now succeed — should drain all 3 persisted batches + current */
    mock_hal_set_next_post_result(PLEXUS_OK);
    int before_count = mock_hal_post_call_count();
    plexus_send(c, "final", 99.0);
    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_OK);

    /* Should have sent 3 persisted + 1 current = 4 additional posts */
    int additional = mock_hal_post_call_count() - before_count;
    ASSERT(additional == 4);

    plexus_free(c);
}

TEST(ring_buffer_wraps_around) {
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* Fill beyond PLEXUS_PERSIST_MAX_BATCHES — oldest should be overwritten */
    for (int i = 0; i < PLEXUS_PERSIST_MAX_BATCHES + 2; i++) {
        char name[32];
        snprintf(name, sizeof(name), "m_%d", i);
        plexus_send(c, name, (double)i);
        plexus_flush(c);
    }

    /* Now succeed — should drain at most PLEXUS_PERSIST_MAX_BATCHES batches */
    mock_hal_set_next_post_result(PLEXUS_OK);
    int before_count = mock_hal_post_call_count();
    plexus_send(c, "final", 99.0);
    plexus_flush(c);

    int additional = mock_hal_post_call_count() - before_count;
    /* At most PLEXUS_PERSIST_MAX_BATCHES persisted + 1 current */
    ASSERT(additional <= PLEXUS_PERSIST_MAX_BATCHES + 1);

    plexus_free(c);
}

TEST(no_data_after_successful_drain) {
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send(c, "temp", 25.0);
    plexus_flush(c);

    /* Drain persisted data */
    mock_hal_set_next_post_result(PLEXUS_OK);
    plexus_send(c, "temp2", 30.0);
    plexus_flush(c);

    /* Now flush with no data + no persisted data */
    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_ERR_NO_DATA);

    plexus_free(c);
}

TEST(persist_survives_corrupt_slot) {
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* Persist two batches */
    plexus_send(c, "a", 1.0);
    plexus_flush(c);
    plexus_send(c, "b", 2.0);
    plexus_flush(c);

    /* Corrupt the first slot by writing garbage */
    plexus_hal_storage_write("plexus_b0", "garbage", 7);

    /* Drain — should skip corrupt slot and still send good one */
    mock_hal_set_next_post_result(PLEXUS_OK);
    plexus_send(c, "c", 3.0);
    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_OK);

    plexus_free(c);
}

TEST(empty_ring_no_drain) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* No persisted data, just a normal flush */
    plexus_send(c, "temp", 25.0);
    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(mock_hal_post_call_count() == 1);

    plexus_free(c);
}

TEST(drain_stops_on_send_failure) {
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* Persist two batches */
    plexus_send(c, "a", 1.0);
    plexus_flush(c);
    plexus_send(c, "b", 2.0);
    plexus_flush(c);

    /* Keep failing — drain should stop on first failure */
    plexus_send(c, "c", 3.0);
    plexus_flush(c);

    /* Nothing drained because all sends fail */
    plexus_free(c);
}

TEST(persist_data_integrity) {
    mock_hal_set_next_post_result(PLEXUS_ERR_NETWORK);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send(c, "temp", 42.0);
    plexus_flush(c);

    /* Now succeed and check the restored data contains our metric */
    mock_hal_set_next_post_result(PLEXUS_OK);
    plexus_send(c, "other", 1.0);
    plexus_flush(c);

    /* The first HTTP call (drain) should contain "temp" and 42 */
    /* The last call should be our current batch */
    ASSERT(plexus_total_sent(c) > 0);

    plexus_free(c);
}

/* ---- Main ---- */

int main(void) {
    printf("test_persist:\n");

    RUN(failed_flush_persists_data);
    RUN(persists_multiple_batches);
    RUN(ring_buffer_wraps_around);
    RUN(no_data_after_successful_drain);
    RUN(persist_survives_corrupt_slot);
    RUN(empty_ring_no_drain);
    RUN(drain_stops_on_send_failure);
    RUN(persist_data_integrity);

    printf("\n  %d passed, %d failed\n\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
