/**
 * @file provisioning.h
 * @brief Serial provisioning for Plexus generic firmware
 *
 * Handles receiving API key + WiFi credentials over UART0 (USB-serial)
 * from the Plexus dashboard. Credentials are stored in encrypted NVS
 * and never embedded in the firmware binary.
 *
 * Protocol:
 *   Dashboard sends (newline-terminated JSON):
 *     {"api_key":"plx_...","endpoint":"https://...","wifi_ssid":"...","wifi_pass":"..."}\n
 *
 *   Firmware responds:
 *     {"status":"ok","firmware_version":"0.2.1","sensors":["BME280","MPU6050"]}\n
 *
 *   Then reboots into operational mode.
 */

#ifndef PLEXUS_PROVISIONING_H
#define PLEXUS_PROVISIONING_H

#include <stdbool.h>
#include "esp_err.h"

#define PROVISIONING_NVS_KEY_API_KEY   "prov_apikey"
#define PROVISIONING_NVS_KEY_ENDPOINT  "prov_endpt"
#define PROVISIONING_NVS_KEY_WIFI_SSID "prov_ssid"
#define PROVISIONING_NVS_KEY_WIFI_PASS "prov_pass"

#define PROVISIONING_MAX_LINE_LEN      512
#define PROVISIONING_MAX_FIELD_LEN     256

/**
 * Check if an API key is stored in NVS (i.e. device has been provisioned).
 */
bool provisioning_has_api_key(void);

/**
 * Load the API key from NVS into the provided buffer.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not provisioned.
 */
esp_err_t provisioning_load_api_key(char* buf, size_t buf_len);

/**
 * Load the endpoint URL from NVS into the provided buffer.
 * Falls back to the default endpoint if not stored.
 */
esp_err_t provisioning_load_endpoint(char* buf, size_t buf_len);

/**
 * Load WiFi SSID from NVS.
 */
esp_err_t provisioning_load_wifi_ssid(char* buf, size_t buf_len);

/**
 * Load WiFi password from NVS.
 */
esp_err_t provisioning_load_wifi_pass(char* buf, size_t buf_len);

/**
 * Start serial provisioning mode. Blocks until a valid provisioning
 * packet is received on UART0, stores credentials in encrypted NVS,
 * sends a response with firmware version and detected sensors,
 * then reboots the device.
 */
void provisioning_start_serial(void);

#endif /* PLEXUS_PROVISIONING_H */
