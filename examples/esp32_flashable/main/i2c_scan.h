/**
 * @file i2c_scan.h
 * @brief I2C bus scanning and sensor auto-detection for the flashable firmware.
 *
 * Scans the I2C bus for known sensors and provides single-shot read functions.
 * Designed for the flashable firmware only — not part of the Plexus C SDK library.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "plexus.h"

/* Maximum number of sensors that can be detected */
#define I2C_SCAN_MAX_SENSORS 8

/* Maximum number of metrics a single sensor can produce */
#define I2C_SCAN_MAX_METRICS 6

typedef struct {
    const char* name;          /* e.g., "temperature" */
    float       value;
} i2c_metric_t;

typedef struct {
    const char* sensor_name;   /* e.g., "BME280" */
    uint8_t     address;       /* I2C address */
    int         metric_count;
    i2c_metric_t metrics[I2C_SCAN_MAX_METRICS];
} i2c_sensor_t;

typedef struct {
    int          count;
    i2c_sensor_t sensors[I2C_SCAN_MAX_SENSORS];
    char         devices_str[128];  /* Comma-separated hex addresses */
} i2c_scan_result_t;

/**
 * Initialize the I2C master bus.
 *
 * @param sda_pin  GPIO for SDA (default 21 on ESP32)
 * @param scl_pin  GPIO for SCL (default 22 on ESP32)
 * @return true on success
 */
bool i2c_scan_init(int sda_pin, int scl_pin);

/**
 * Scan the bus and identify known sensors.
 * Call once at boot after i2c_scan_init().
 *
 * @param result  Populated with detected sensors
 */
void i2c_scan_detect(i2c_scan_result_t* result);

/**
 * Read all detected sensors and update their metric values.
 *
 * @param result  The scan result from i2c_scan_detect()
 */
void i2c_scan_read_all(i2c_scan_result_t* result);

/**
 * Send all sensor metrics via Plexus.
 *
 * @param px      Plexus client
 * @param result  The scan result with updated metrics
 */
void i2c_scan_send(plexus_client_t* px, const i2c_scan_result_t* result);
