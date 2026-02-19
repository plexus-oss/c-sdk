/**
 * @file test_heartbeat.c
 * @brief Tests for device heartbeat / registration feature
 *
 * Build: cmake -B build-test tests/ && cmake --build build-test && ./build-test/test_heartbeat
 * Requires: -DPLEXUS_ENABLE_HEARTBEAT=1
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
extern const char* mock_hal_last_post_url(void);
extern void mock_hal_advance_tick(uint32_t delta_ms);
extern void mock_hal_set_tick(uint32_t tick_ms);

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

/* ---- Tests ---- */

TEST(register_metric) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_register_metric(c, "temperature");
    ASSERT(err == PLEXUS_OK);

    err = plexus_register_metric(c, "humidity");
    ASSERT(err == PLEXUS_OK);

    plexus_free(c);
}

TEST(register_metric_duplicate_ok) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_register_metric(c, "temperature");
    plexus_err_t err = plexus_register_metric(c, "temperature");
    ASSERT(err == PLEXUS_OK); /* duplicate is silently ignored */

    plexus_free(c);
}

TEST(register_metric_overflow) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    for (int i = 0; i < PLEXUS_MAX_REGISTERED_METRICS; i++) {
        char name[32];
        snprintf(name, sizeof(name), "metric_%d", i);
        ASSERT(plexus_register_metric(c, name) == PLEXUS_OK);
    }

    ASSERT(plexus_register_metric(c, "overflow") == PLEXUS_ERR_BUFFER_FULL);

    plexus_free(c);
}

TEST(set_device_info) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_set_device_info(c, "esp32", "1.0.0");
    ASSERT(err == PLEXUS_OK);
    plexus_free(c);
}

TEST(heartbeat_sends_json) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_device_info(c, "esp32", "1.0.0");
    plexus_register_metric(c, "temperature");
    plexus_register_metric(c, "humidity");

    plexus_err_t err = plexus_heartbeat(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(mock_hal_post_call_count() == 1);

    const char* body = mock_hal_last_post_body();
    ASSERT(strstr(body, "\"sdk\":\"c/") != NULL);
    ASSERT(strstr(body, "\"source_id\":\"dev-001\"") != NULL);
    ASSERT(strstr(body, "\"device_type\":\"esp32\"") != NULL);
    ASSERT(strstr(body, "\"firmware_version\":\"1.0.0\"") != NULL);
    ASSERT(strstr(body, "\"temperature\"") != NULL);
    ASSERT(strstr(body, "\"humidity\"") != NULL);
    ASSERT(strstr(body, "\"metrics\":[") != NULL);
    ASSERT(strstr(body, "\"uptime_ms\":") != NULL);
    ASSERT(strstr(body, "\"total_sent\":") != NULL);

    plexus_free(c);
}

TEST(heartbeat_url_derived_from_endpoint) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_endpoint(c, "https://custom.example.com/api/ingest");

    plexus_heartbeat(c);

    const char* url = mock_hal_last_post_url();
    ASSERT(strstr(url, "/api/heartbeat") != NULL);
    ASSERT(strstr(url, "custom.example.com") != NULL);
    /* Should NOT contain /api/ingest */
    ASSERT(strstr(url, "/api/ingest") == NULL);

    plexus_free(c);
}

TEST(tick_triggers_heartbeat_on_interval) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_device_info(c, "esp32", "1.0.0");

    /* Tick before heartbeat interval — should not trigger */
    mock_hal_advance_tick(1000);
    plexus_tick(c);
    ASSERT(mock_hal_post_call_count() == 0);

    /* Advance past heartbeat interval */
    mock_hal_advance_tick(PLEXUS_HEARTBEAT_INTERVAL_MS);
    plexus_tick(c);
    ASSERT(mock_hal_post_call_count() == 1);

    plexus_free(c);
}

TEST(heartbeat_no_device_info) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    /* No device info set — should still succeed with partial JSON */

    plexus_err_t err = plexus_heartbeat(c);
    ASSERT(err == PLEXUS_OK);

    const char* body = mock_hal_last_post_body();
    ASSERT(strstr(body, "\"source_id\":\"dev-001\"") != NULL);
    /* device_type should not appear if not set */
    ASSERT(strstr(body, "\"device_type\"") == NULL);

    plexus_free(c);
}

/* ---- Main ---- */

int main(void) {
    printf("test_heartbeat:\n");

    RUN(register_metric);
    RUN(register_metric_duplicate_ok);
    RUN(register_metric_overflow);
    RUN(set_device_info);
    RUN(heartbeat_sends_json);
    RUN(heartbeat_url_derived_from_endpoint);
    RUN(tick_triggers_heartbeat_on_interval);
    RUN(heartbeat_no_device_info);

    printf("\n  %d passed, %d failed\n\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
