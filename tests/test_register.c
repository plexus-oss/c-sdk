/**
 * @file test_register.c
 * @brief Tests for auto-registration feature
 *
 * Build: cmake -B build-test tests/ && cmake --build build-test && ./build-test/test_register
 * Requires: -DPLEXUS_ENABLE_AUTO_REGISTER=1 -DPLEXUS_ENABLE_PERSISTENT_BUFFER=1
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
extern void mock_hal_storage_reset(void);

/* Registration-specific helpers */
extern void mock_hal_set_register_response(const char* json, plexus_err_t result);
extern const char* mock_hal_last_post_response_url(void);
extern const char* mock_hal_last_post_response_body(void);
extern void mock_hal_register_reset(void);

static int tests_passed = 0;
static int tests_failed = 0;
static int s_test_failed_flag = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
    mock_hal_reset(); \
    mock_hal_storage_reset(); \
    mock_hal_register_reset(); \
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

TEST(set_device_identity) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_set_device_identity(c, "myhost", "esp32");
    ASSERT(err == PLEXUS_OK);
    plexus_free(c);
}

TEST(set_device_identity_null_client) {
    plexus_err_t err = plexus_set_device_identity(NULL, "host", "platform");
    ASSERT(err == PLEXUS_ERR_NULL_PTR);
}

TEST(not_registered_initially) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    ASSERT(!plexus_is_registered(c));
    plexus_free(c);
}

TEST(register_device_success) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_device_identity(c, "myhost", "esp32");

    mock_hal_set_register_response(
        "{\"device_token\":\"plxd_testtoken123\",\"source_id\":\"dev-001\",\"org_id\":\"org_xyz\"}",
        PLEXUS_OK);

    plexus_err_t err = plexus_register_device(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_is_registered(c));

    plexus_free(c);
}

TEST(register_device_url_correct) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_endpoint(c, "https://app.plexus.company/api/ingest");

    mock_hal_set_register_response(
        "{\"device_token\":\"plxd_tok\",\"source_id\":\"dev-001\"}",
        PLEXUS_OK);

    plexus_register_device(c);

    const char* url = mock_hal_last_post_response_url();
    ASSERT(strstr(url, "/api/sources/register") != NULL);
    ASSERT(strstr(url, "app.plexus.company") != NULL);
    ASSERT(strstr(url, "/api/ingest") == NULL);

    plexus_free(c);
}

TEST(register_device_sends_json_body) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_device_identity(c, "myhost", "esp32");

    mock_hal_set_register_response(
        "{\"device_token\":\"plxd_tok\",\"source_id\":\"dev-001\"}",
        PLEXUS_OK);

    plexus_register_device(c);

    const char* body = mock_hal_last_post_response_body();
    ASSERT(strstr(body, "\"name\":\"dev-001\"") != NULL);
    ASSERT(strstr(body, "\"hostname\":\"myhost\"") != NULL);
    ASSERT(strstr(body, "\"platform\":\"esp32\"") != NULL);

    plexus_free(c);
}

TEST(register_device_noop_if_registered) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    mock_hal_set_register_response(
        "{\"device_token\":\"plxd_tok\",\"source_id\":\"dev-001\"}",
        PLEXUS_OK);

    plexus_register_device(c);
    ASSERT(plexus_is_registered(c));

    int calls = mock_hal_post_call_count();
    plexus_register_device(c); /* Should be no-op */
    ASSERT(mock_hal_post_call_count() == calls);

    plexus_free(c);
}

TEST(register_device_network_error) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    mock_hal_set_register_response(NULL, PLEXUS_ERR_NETWORK);

    plexus_err_t err = plexus_register_device(c);
    ASSERT(err == PLEXUS_ERR_NETWORK);
    ASSERT(!plexus_is_registered(c));

    plexus_free(c);
}

TEST(register_device_auth_error) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    mock_hal_set_register_response(NULL, PLEXUS_ERR_AUTH);

    plexus_err_t err = plexus_register_device(c);
    ASSERT(err == PLEXUS_ERR_AUTH);
    ASSERT(!plexus_is_registered(c));

    plexus_free(c);
}

TEST(register_device_missing_token_in_response) {
    /* Registration succeeds even without device_token — only source_id matters */
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    mock_hal_set_register_response("{\"source_id\":\"dev-001\"}", PLEXUS_OK);

    plexus_err_t err = plexus_register_device(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_is_registered(c));

    plexus_free(c);
}

TEST(register_device_updates_source_id) {
    plexus_client_t* c = plexus_init("plx_key", "pending");

    mock_hal_set_register_response(
        "{\"device_token\":\"plxd_tok\",\"source_id\":\"slug-001\"}",
        PLEXUS_OK);

    plexus_register_device(c);
    ASSERT(plexus_is_registered(c));

    /* Send a metric and check the source_id in the JSON */
    plexus_send_number(c, "temp", 25.0);
    plexus_flush(c);

    const char* body = mock_hal_last_post_body();
    ASSERT(strstr(body, "\"source_id\":\"slug-001\"") != NULL);

    plexus_free(c);
}

TEST(register_json_builder) {
    char buf[256];
    int len = plexus_json_build_register(buf, sizeof(buf), "dev-001", "myhost", "esp32");
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"name\":\"dev-001\"") != NULL);
    ASSERT(strstr(buf, "\"hostname\":\"myhost\"") != NULL);
    ASSERT(strstr(buf, "\"platform\":\"esp32\"") != NULL);
}

TEST(register_json_builder_no_optional) {
    char buf[256];
    int len = plexus_json_build_register(buf, sizeof(buf), "dev-001", "", "");
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"name\":\"dev-001\"") != NULL);
    ASSERT(strstr(buf, "\"hostname\"") == NULL);
    ASSERT(strstr(buf, "\"platform\"") == NULL);
}

TEST(is_registered_null_client) {
    ASSERT(!plexus_is_registered(NULL));
}

/* ---- Main ---- */

int main(void) {
    printf("test_register:\n");

    RUN(set_device_identity);
    RUN(set_device_identity_null_client);
    RUN(not_registered_initially);
    RUN(register_device_success);
    RUN(register_device_url_correct);
    RUN(register_device_sends_json_body);
    RUN(register_device_noop_if_registered);
    RUN(register_device_network_error);
    RUN(register_device_auth_error);
    RUN(register_device_missing_token_in_response);
    RUN(register_device_updates_source_id);
    RUN(register_json_builder);
    RUN(register_json_builder_no_optional);
    RUN(is_registered_null_client);

    printf("\n  %d passed, %d failed\n\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
