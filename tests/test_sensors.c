/**
 * @file test_sensors.c
 * @brief Tests for I2C sensor discovery feature
 *
 * Build: cmake -B build-test tests/ && cmake --build build-test && ./build-test/test_sensors
 * Requires: -DPLEXUS_ENABLE_SENSOR_DISCOVERY=1 -DPLEXUS_ENABLE_HEARTBEAT=1
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

/* I2C mock helpers */
extern void mock_hal_i2c_reset(void);
extern void mock_hal_i2c_add_device(uint8_t addr);
extern void mock_hal_i2c_set_reg(uint8_t addr, uint8_t reg, uint8_t val);

static int tests_passed = 0;
static int tests_failed = 0;
static int s_test_failed_flag = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
    mock_hal_reset(); \
    mock_hal_i2c_reset(); \
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

TEST(scan_no_devices) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_scan_sensors(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_detected_sensor_count(c) == 0);
    plexus_free(c);
}

TEST(scan_detects_bme280) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* Add a BME280 at 0x76 with chip_id 0x60 */
    mock_hal_i2c_add_device(0x76);
    mock_hal_i2c_set_reg(0x76, 0xD0, 0x60); /* BME280 chip_id */

    plexus_err_t err = plexus_scan_sensors(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_detected_sensor_count(c) == 1);

    const plexus_detected_sensor_t* s = plexus_detected_sensor(c, 0);
    ASSERT(s != NULL);
    ASSERT(strcmp(s->descriptor->name, "BME280") == 0);
    ASSERT(s->addr == 0x76);
    ASSERT(s->active == true);
    ASSERT(s->descriptor->metric_count == 3);

    plexus_free(c);
}

TEST(scan_detects_bmp280) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* BMP280 has chip_id 0x58 */
    mock_hal_i2c_add_device(0x77);
    mock_hal_i2c_set_reg(0x77, 0xD0, 0x58);

    plexus_scan_sensors(c);
    ASSERT(plexus_detected_sensor_count(c) == 1);

    const plexus_detected_sensor_t* s = plexus_detected_sensor(c, 0);
    ASSERT(strcmp(s->descriptor->name, "BME280") == 0);

    plexus_free(c);
}

TEST(scan_detects_mpu6050) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* Add MPU6050 at 0x68 with WHO_AM_I 0x68 */
    mock_hal_i2c_add_device(0x68);
    mock_hal_i2c_set_reg(0x68, 0x75, 0x68); /* WHO_AM_I */

    plexus_scan_sensors(c);
    ASSERT(plexus_detected_sensor_count(c) == 1);

    const plexus_detected_sensor_t* s = plexus_detected_sensor(c, 0);
    ASSERT(strcmp(s->descriptor->name, "MPU6050") == 0);
    ASSERT(s->addr == 0x68);
    ASSERT(s->descriptor->metric_count == 6);

    plexus_free(c);
}

TEST(scan_detects_multiple_sensors) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    mock_hal_i2c_add_device(0x68);
    mock_hal_i2c_set_reg(0x68, 0x75, 0x68); /* MPU6050 */

    mock_hal_i2c_add_device(0x76);
    mock_hal_i2c_set_reg(0x76, 0xD0, 0x60); /* BME280 */

    plexus_scan_sensors(c);
    ASSERT(plexus_detected_sensor_count(c) == 2);

    plexus_free(c);
}

TEST(scan_ignores_unknown_device) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* Add a device that responds but doesn't match any known sensor */
    mock_hal_i2c_add_device(0x50);

    plexus_scan_sensors(c);
    ASSERT(plexus_detected_sensor_count(c) == 0);

    plexus_free(c);
}

TEST(scan_rejects_wrong_chip_id) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    /* Device at BME280 address but wrong chip_id */
    mock_hal_i2c_add_device(0x76);
    mock_hal_i2c_set_reg(0x76, 0xD0, 0xFF);

    plexus_scan_sensors(c);
    ASSERT(plexus_detected_sensor_count(c) == 0);

    plexus_free(c);
}

TEST(scan_registers_metrics_for_heartbeat) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    mock_hal_i2c_add_device(0x76);
    mock_hal_i2c_set_reg(0x76, 0xD0, 0x60);

    plexus_scan_sensors(c);

    /* Heartbeat should contain auto-registered metrics */
    plexus_heartbeat(c);
    const char* body = mock_hal_last_post_body();
    ASSERT(strstr(body, "\"temperature\"") != NULL);
    ASSERT(strstr(body, "\"humidity\"") != NULL);
    ASSERT(strstr(body, "\"pressure\"") != NULL);

    plexus_free(c);
}

TEST(sensor_read_all_queues_metrics) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    mock_hal_i2c_add_device(0x76);
    mock_hal_i2c_set_reg(0x76, 0xD0, 0x60); /* BME280 */

    plexus_scan_sensors(c);
    ASSERT(plexus_detected_sensor_count(c) == 1);

    /* Reading will trigger BME280 measurement cycle */
    plexus_err_t err = plexus_sensor_read_all(c);
    ASSERT(err == PLEXUS_OK);

    /* BME280 should queue 3 metrics */
    ASSERT(plexus_pending_count(c) == 3);

    plexus_free(c);
}

TEST(sensor_read_all_no_sensors) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_err_t err = plexus_sensor_read_all(c);
    ASSERT(err == PLEXUS_OK);
    ASSERT(plexus_pending_count(c) == 0);
    plexus_free(c);
}

TEST(detected_sensor_out_of_range) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    ASSERT(plexus_detected_sensor(c, 0) == NULL);
    ASSERT(plexus_detected_sensor(c, 255) == NULL);
    plexus_free(c);
}

TEST(detected_sensor_count_null) {
    ASSERT(plexus_detected_sensor_count(NULL) == 0);
}

TEST(scan_null_client) {
    plexus_err_t err = plexus_scan_sensors(NULL);
    ASSERT(err == PLEXUS_ERR_NULL_PTR);
}

TEST(heartbeat_includes_sensors_array) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_device_info(c, "esp32", "1.0.0");

    mock_hal_i2c_add_device(0x76);
    mock_hal_i2c_set_reg(0x76, 0xD0, 0x60); /* BME280 */

    plexus_scan_sensors(c);
    plexus_heartbeat(c);

    const char* body = mock_hal_last_post_body();
    ASSERT(strstr(body, "\"sensors\":[") != NULL);
    ASSERT(strstr(body, "\"name\":\"BME280\"") != NULL);
    ASSERT(strstr(body, "\"description\":\"Environmental sensor\"") != NULL);
    ASSERT(strstr(body, "\"sample_rate\":") != NULL);

    plexus_free(c);
}

TEST(heartbeat_no_sensors_when_none_detected) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");
    plexus_set_device_info(c, "esp32", "1.0.0");

    plexus_heartbeat(c);

    const char* body = mock_hal_last_post_body();
    /* No sensors array if nothing detected */
    ASSERT(strstr(body, "\"sensors\"") == NULL);

    plexus_free(c);
}

TEST(custom_sensor_register) {
    static const char* const test_metrics[] = {"lux"};
    static const plexus_sensor_descriptor_t test_desc = {
        .name = "BH1750",
        .description = "Light sensor",
        .metrics = test_metrics,
        .metric_count = 1,
        .i2c_addrs = {0x23, 0, 0, 0},
        .default_sample_rate_hz = 1.0f,
        .probe = NULL, /* ACK-only detection */
        .read = NULL,
    };

    plexus_err_t err = plexus_sensor_register(&test_desc);
    ASSERT(err == PLEXUS_OK);

    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    mock_hal_i2c_add_device(0x23);

    plexus_scan_sensors(c);
    ASSERT(plexus_detected_sensor_count(c) == 1);

    const plexus_detected_sensor_t* s = plexus_detected_sensor(c, 0);
    ASSERT(strcmp(s->descriptor->name, "BH1750") == 0);

    plexus_free(c);
}

TEST(mpu6050_read_queues_six_metrics) {
    plexus_client_t* c = plexus_init("plx_key", "dev-001");

    mock_hal_i2c_add_device(0x68);
    mock_hal_i2c_set_reg(0x68, 0x75, 0x68); /* WHO_AM_I */

    plexus_scan_sensors(c);
    ASSERT(plexus_detected_sensor_count(c) == 1);

    plexus_sensor_read_all(c);
    /* MPU6050 should queue 6 metrics (accel_xyz + gyro_xyz) */
    ASSERT(plexus_pending_count(c) == 6);

    plexus_free(c);
}

/* ---- Main ---- */

int main(void) {
    printf("test_sensors:\n");

    RUN(scan_no_devices);
    RUN(scan_detects_bme280);
    RUN(scan_detects_bmp280);
    RUN(scan_detects_mpu6050);
    RUN(scan_detects_multiple_sensors);
    RUN(scan_ignores_unknown_device);
    RUN(scan_rejects_wrong_chip_id);
    RUN(scan_registers_metrics_for_heartbeat);
    RUN(sensor_read_all_queues_metrics);
    RUN(sensor_read_all_no_sensors);
    RUN(detected_sensor_out_of_range);
    RUN(detected_sensor_count_null);
    RUN(scan_null_client);
    RUN(heartbeat_includes_sensors_array);
    RUN(heartbeat_no_sensors_when_none_detected);
    RUN(custom_sensor_register);
    RUN(mpu6050_read_queues_six_metrics);

    printf("\n  %d passed, %d failed\n\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
