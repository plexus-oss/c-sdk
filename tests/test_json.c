/**
 * @file test_json.c
 * @brief Host-side unit tests for Plexus JSON serialization
 *
 * Build and run:
 *   cmake -B build-test tests && cmake --build build-test && ./build-test/test_json
 */

/* Include internal header to access plexus_json_serialize and struct fields */
#include "plexus_internal.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

/* Mock HAL helpers */
extern void mock_hal_reset(void);

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

TEST(serialize_single_number) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "temperature", 72.5);

    char buf[1024];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"points\":[") != NULL);
    ASSERT(strstr(buf, "\"metric\":\"temperature\"") != NULL);
    ASSERT(strstr(buf, "\"value\":72.5") != NULL);
    ASSERT(strstr(buf, "\"source_id\":\"dev-001\"") != NULL);

    plexus_free(c);
}

TEST(serialize_multiple_metrics) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "temp", 20.0);
    plexus_send_number(c, "humidity", 55.0);

    char buf[2048];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"temp\"") != NULL);
    ASSERT(strstr(buf, "\"humidity\"") != NULL);
    ASSERT(strstr(buf, "},{") != NULL);

    plexus_free(c);
}

TEST(serialize_integer_value) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "count", 42.0);

    char buf[1024];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"value\":42") != NULL);

    plexus_free(c);
}

TEST(serialize_negative_value) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "delta", -3.14);

    char buf[1024];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "-3.14") != NULL);

    plexus_free(c);
}

TEST(serialize_zero) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "zero", 0.0);

    char buf[1024];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"value\":0") != NULL);

    plexus_free(c);
}

TEST(serialize_nan_becomes_null) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "bad", NAN);

    char buf[1024];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"value\":null") != NULL);

    plexus_free(c);
}

TEST(serialize_with_timestamp) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number_ts(c, "temp", 25.0, 1700000000000ULL);

    char buf[1024];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"timestamp\":1700000000000") != NULL);

    plexus_free(c);
}

#if PLEXUS_ENABLE_STRING_VALUES
TEST(serialize_string_value) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_string(c, "status", "running");

    char buf[1024];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"value\":\"running\"") != NULL);

    plexus_free(c);
}

TEST(serialize_string_with_special_chars) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_string(c, "msg", "hello \"world\"\nnewline");

    char buf[1024];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\\\"world\\\"") != NULL);
    ASSERT(strstr(buf, "\\n") != NULL);

    plexus_free(c);
}
#endif

#if PLEXUS_ENABLE_BOOL_VALUES
TEST(serialize_bool_true) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_bool(c, "armed", true);

    char buf[1024];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"value\":true") != NULL);

    plexus_free(c);
}

TEST(serialize_bool_false) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_bool(c, "armed", false);

    char buf[1024];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"value\":false") != NULL);

    plexus_free(c);
}
#endif

#if PLEXUS_ENABLE_TAGS
TEST(serialize_with_tags) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    const char* keys[] = {"location", "unit"};
    const char* vals[] = {"room-1", "celsius"};
    plexus_send_number_tagged(c, "temp", 25.0, keys, vals, 2);

    char buf[2048];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"tags\":{") != NULL);
    ASSERT(strstr(buf, "\"location\":\"room-1\"") != NULL);
    ASSERT(strstr(buf, "\"unit\":\"celsius\"") != NULL);

    plexus_free(c);
}
#endif

TEST(serialize_buffer_too_small) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_send_number(c, "temperature", 72.5);

    char buf[16];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len == -1);

    plexus_free(c);
}

TEST(serialize_null_client) {
    char buf[1024];
    int len = plexus_json_serialize(NULL, buf, sizeof(buf));
    ASSERT(len == -1);
}

TEST(serialize_empty) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    char buf[1024];
    int len = plexus_json_serialize(c, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strcmp(buf, "{\"points\":[]}") == 0);

    plexus_free(c);
}

/* ---- Main ---- */

int main(void) {
    printf("test_json:\n");

    RUN(serialize_single_number);
    RUN(serialize_multiple_metrics);
    RUN(serialize_integer_value);
    RUN(serialize_negative_value);
    RUN(serialize_zero);
    RUN(serialize_nan_becomes_null);
    RUN(serialize_with_timestamp);

#if PLEXUS_ENABLE_STRING_VALUES
    RUN(serialize_string_value);
    RUN(serialize_string_with_special_chars);
#endif

#if PLEXUS_ENABLE_BOOL_VALUES
    RUN(serialize_bool_true);
    RUN(serialize_bool_false);
#endif

#if PLEXUS_ENABLE_TAGS
    RUN(serialize_with_tags);
#endif

    RUN(serialize_buffer_too_small);
    RUN(serialize_null_client);
    RUN(serialize_empty);

    printf("\n  %d passed, %d failed\n\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
