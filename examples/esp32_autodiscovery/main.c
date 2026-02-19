/**
 * @file main.c
 * @brief Plexus C SDK — "3-click" auto-discovery example for ESP32
 *
 * This example demonstrates the full zero-configuration pipeline:
 *   1. Device auto-registers with the Plexus server
 *   2. I2C bus scan detects connected sensors (BME280, MPU6050, etc.)
 *   3. Heartbeat announces sensors → dashboard auto-generates panels
 *
 * Setup:
 *   - Set CONFIG_PLEXUS_API_KEY in menuconfig (or hardcode below)
 *   - Connect I2C sensors to default pins (SDA=21, SCL=22)
 *   - Flash and monitor: idf.py flash monitor
 *
 * Build requirements (sdkconfig or CMakeLists.txt):
 *   -DPLEXUS_ENABLE_AUTO_REGISTER=1
 *   -DPLEXUS_ENABLE_SENSOR_DISCOVERY=1
 *   -DPLEXUS_ENABLE_HEARTBEAT=1
 *   -DPLEXUS_ENABLE_PERSISTENT_BUFFER=1
 */

#include "plexus.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "plexus_example";

#ifndef CONFIG_PLEXUS_API_KEY
#define CONFIG_PLEXUS_API_KEY "plx_your_api_key_here"
#endif

#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif

/* Forward declaration — implement WiFi init for your project */
extern void wifi_init_sta(void);

void app_main(void) {
    /* Initialize NVS (required for WiFi + persistent storage) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Connect to WiFi */
    wifi_init_sta();

    /* Optional: sync time via NTP for accurate timestamps */
    extern void plexus_hal_init_time(const char* ntp_server);
    plexus_hal_init_time(NULL);

    /* ================================================================= */
    /* Plexus initialization                                             */
    /* ================================================================= */

    /* Initialize with API key and a placeholder source_id.
     * After registration, the server may assign a different slug. */
    plexus_client_t* px = plexus_init(CONFIG_PLEXUS_API_KEY, "pending");
    if (!px) {
        ESP_LOGE(TAG, "Failed to initialize Plexus client");
        return;
    }

    /* Set device metadata for heartbeat */
    plexus_set_device_info(px, "ESP32", APP_VERSION);
    plexus_set_device_identity(px, "esp32-autodiscovery", "ESP32-DevKitC");

    /* ================================================================= */
    /* Step 1+2: Register device to create source on server              */
    /* ================================================================= */

    if (!plexus_is_registered(px)) {
        ESP_LOGI(TAG, "Registering as new device...");
        plexus_err_t reg_err = plexus_register_device(px);
        if (reg_err != PLEXUS_OK) {
            ESP_LOGE(TAG, "Registration failed: %s", plexus_strerror(reg_err));
            /* Continue anyway — can still send telemetry with API key */
        } else {
            ESP_LOGI(TAG, "Device registered successfully");
        }
    }

    /* ================================================================= */
    /* Step 3: Auto-detect sensors on I2C bus                            */
    /* ================================================================= */

    plexus_hal_i2c_init(0);
    plexus_scan_sensors(px);

    uint8_t sensor_count = plexus_detected_sensor_count(px);
    ESP_LOGI(TAG, "Detected %d sensors:", sensor_count);
    for (uint8_t i = 0; i < sensor_count; i++) {
        const plexus_detected_sensor_t* s = plexus_detected_sensor(px, i);
        ESP_LOGI(TAG, "  [0x%02X] %s — %s (%d metrics)",
                 s->addr, s->descriptor->name,
                 s->descriptor->description,
                 s->descriptor->metric_count);
    }

    /* ================================================================= */
    /* Step 4: Send heartbeat with sensor info                           */
    /*         Dashboard can auto-generate panels from this data         */
    /* ================================================================= */

    plexus_heartbeat(px);

    /* ================================================================= */
    /* Main loop: read sensors and send telemetry                        */
    /* ================================================================= */

    ESP_LOGI(TAG, "Entering main loop (100ms tick)");

    while (1) {
        /* Read all detected sensors and queue metrics */
        plexus_sensor_read_all(px);

        /* Flush metrics + periodic heartbeat */
        plexus_tick(px);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
