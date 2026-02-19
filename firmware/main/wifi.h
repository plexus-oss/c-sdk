/**
 * @file wifi.h
 * @brief WiFi station management for Plexus generic firmware
 *
 * Initializes WiFi STA using credentials from encrypted NVS.
 * Includes event-group based reconnection handling.
 */

#ifndef PLEXUS_WIFI_H
#define PLEXUS_WIFI_H

#include "esp_err.h"

/**
 * Initialize WiFi STA using credentials stored in NVS by the provisioning step.
 * Blocks until connected or timeout (30 seconds).
 *
 * @return ESP_OK on successful connection, ESP_ERR_TIMEOUT on failure.
 */
esp_err_t wifi_init_from_nvs(void);

/**
 * Check if WiFi is currently connected.
 */
bool wifi_is_connected(void);

#endif /* PLEXUS_WIFI_H */
