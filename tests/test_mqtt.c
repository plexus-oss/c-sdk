/**
 * @file test_mqtt.c
 * @brief Tests for MQTT transport feature
 *
 * Build: cmake -B build-test tests/ && cmake --build build-test && ./build-test/test_mqtt
 * Requires: -DPLEXUS_ENABLE_MQTT=1
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

/* Mock MQTT helpers */
extern void mock_hal_mqtt_reset(void);
extern void mock_hal_mqtt_set_connected(bool connected);
extern void mock_hal_mqtt_set_next_connect_result(plexus_err_t err);
extern void mock_hal_mqtt_set_next_publish_result(plexus_err_t err);
extern int mock_hal_mqtt_publish_count(void);
extern const char* mock_hal_mqtt_last_topic(void);
extern const char* mock_hal_mqtt_last_payload(void);

static int tests_passed = 0;
static int tests_failed = 0;
static int s_test_failed_flag = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
    mock_hal_reset(); \
    mock_hal_mqtt_reset(); \
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

TEST(default_transport_is_http) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    ASSERT(plexus_get_transport(c) == PLEXUS_TRANSPORT_HTTP);
    plexus_free(c);
}

TEST(set_transport_mqtt) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_set_transport_mqtt(c, "mqtt://broker.local:1883");
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_get_transport(c) == PLEXUS_TRANSPORT_MQTT);
    plexus_free(c);
}

TEST(mqtt_flush_publishes) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_transport_mqtt(c, "mqtt://broker.local:1883");

    plexus_send(c, "temp", 25.0);
    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(mock_hal_mqtt_publish_count() == 1);
    ASSERT(mock_hal_post_call_count() == 0); /* HTTP should NOT be used */

    plexus_free(c);
}

TEST(mqtt_uses_correct_topic) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_transport_mqtt(c, "mqtt://broker.local:1883");

    plexus_send(c, "temp", 25.0);
    plexus_flush(c);

    const char* topic = mock_hal_mqtt_last_topic();
    ASSERT(strstr(topic, "plexus/ingest/dev-001") != NULL);

    plexus_free(c);
}

TEST(mqtt_payload_is_json) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_transport_mqtt(c, "mqtt://broker.local:1883");

    plexus_send(c, "temp", 25.0);
    plexus_flush(c);

    const char* payload = mock_hal_mqtt_last_payload();
    ASSERT(strstr(payload, "\"points\"") != NULL);
    ASSERT(strstr(payload, "\"temp\"") != NULL);
    ASSERT(strstr(payload, "25") != NULL);

    plexus_free(c);
}

TEST(mqtt_connects_on_first_flush) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_transport_mqtt(c, "mqtt://broker.local:1883");

    /* Not connected yet */
    ASSERT(!plexus_hal_mqtt_is_connected());

    /* Flush should trigger connect */
    plexus_send(c, "temp", 25.0);
    plexus_flush(c);

    /* Now connected */
    ASSERT(plexus_hal_mqtt_is_connected());

    plexus_free(c);
}

TEST(mqtt_connect_failure) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_transport_mqtt(c, "mqtt://broker.local:1883");
    mock_hal_mqtt_set_next_connect_result(PLEXUS_ERR_TRANSPORT);

    plexus_send(c, "temp", 25.0);
    plexus_err_t err = plexus_flush(c);
    /* Should fail with transport error after retries */
    ASSERT(err != PLEXUS_OK);

    plexus_free(c);
}

TEST(mqtt_publish_failure_retries) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_transport_mqtt(c, "mqtt://broker.local:1883");
    mock_hal_mqtt_set_connected(true);
    mock_hal_mqtt_set_next_publish_result(PLEXUS_ERR_TRANSPORT);

    plexus_send(c, "temp", 25.0);
    plexus_err_t err = plexus_flush(c);
    ASSERT(err != PLEXUS_OK);
    /* Should have retried */
    ASSERT(mock_hal_mqtt_publish_count() == PLEXUS_MAX_RETRIES);

    plexus_free(c);
}

TEST(http_flush_still_works) {
    /* HTTP transport should still work as default */
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    /* Don't set MQTT — should use HTTP */

    plexus_send(c, "temp", 25.0);
    plexus_err_t err = plexus_flush(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(mock_hal_post_call_count() == 1);
    ASSERT(mock_hal_mqtt_publish_count() == 0);

    plexus_free(c);
}

TEST(mqtt_disconnect_on_free) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_transport_mqtt(c, "mqtt://broker.local:1883");

    plexus_send(c, "temp", 25.0);
    plexus_flush(c);
    ASSERT(plexus_hal_mqtt_is_connected());

    plexus_free(c);
    ASSERT(!plexus_hal_mqtt_is_connected());
}

TEST(set_transport_mqtt_null_uri) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_set_transport_mqtt(c, NULL);
    ASSERT(err == PLEXUS_ERR_NULL_PTR);
    plexus_free(c);
}

/* ---- Main ---- */

int main(void) {
    printf("test_mqtt:\n");

    RUN(default_transport_is_http);
    RUN(set_transport_mqtt);
    RUN(mqtt_flush_publishes);
    RUN(mqtt_uses_correct_topic);
    RUN(mqtt_payload_is_json);
    RUN(mqtt_connects_on_first_flush);
    RUN(mqtt_connect_failure);
    RUN(mqtt_publish_failure_retries);
    RUN(http_flush_still_works);
    RUN(mqtt_disconnect_on_free);
    RUN(set_transport_mqtt_null_uri);

    printf("\n  %d passed, %d failed\n\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
