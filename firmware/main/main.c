/**
 * @file main.c
 * @brief Plexus Generic Firmware for ESP32
 *
 * Two runtime modes based on NVS state:
 *
 *   1. Provisioning mode (no API key in NVS):
 *      Listens on UART0 for a JSON provisioning packet from the dashboard.
 *      LED blinks to indicate waiting. Reboots when credentials are received.
 *
 *   2. Operational mode (API key present):
 *      WiFi connect -> NTP sync -> I2C sensor scan -> server registration
 *      -> telemetry + command polling loop.
 *
 * Credentials are stored in ESP32 encrypted NVS — never baked into the binary.
 * Flash this firmware via the Plexus dashboard or idf.py.
 */

#include "plexus_firmware_config.h"
#include "plexus.h"
#include "provisioning.h"
#include "wifi.h"
#include "tls_certs.h"
#include "system_metrics.h"
#include "adc.h"
#include "gps.h"
#include "canbus.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs_sec_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"

#include <string.h>

static const char* TAG = "plexus_fw";

#define FIRMWARE_VERSION "0.2.1"

/* ── NVS initialization with encryption support ───────────────────────────── */

static esp_err_t init_nvs_encrypted(void) {
    /* Try encrypted NVS first (requires nvs_keys partition) */
    esp_err_t err;

    /* Find the NVS keys partition */
    const esp_partition_t* keys_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, NULL);

    if (keys_part) {
        nvs_sec_cfg_t sec_cfg;
        err = nvs_flash_read_security_cfg(keys_part, &sec_cfg);
        if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
            ESP_LOGI(TAG, "Generating NVS encryption keys");
            err = nvs_flash_generate_keys(keys_part, &sec_cfg);
        }

        if (err == ESP_OK) {
            err = nvs_flash_secure_init_partition("nvs", &sec_cfg);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "NVS initialized with encryption");
                return ESP_OK;
            }

            if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
                ESP_LOGW(TAG, "Encrypted NVS needs erase");
                nvs_flash_erase();
                err = nvs_flash_secure_init_partition("nvs", &sec_cfg);
                if (err == ESP_OK) return ESP_OK;
            }
        }

        ESP_LOGW(TAG, "Encrypted NVS failed (%s), falling back to plain NVS",
                 esp_err_to_name(err));
    }

    /* Fallback: plain NVS (development / DevKit boards without keys partition) */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized (unencrypted)");
    }
    return err;
}

/* ── NTP time sync ────────────────────────────────────────────────────────── */

static void sync_time(void) {
    ESP_LOGI(TAG, "Synchronizing time via NTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    int retry = 0;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (retry >= 30) {
        ESP_LOGW(TAG, "NTP sync timed out — timestamps may be inaccurate");
    } else {
        ESP_LOGI(TAG, "Time synchronized");
    }
}

/* ── Main entry point ─────────────────────────────────────────────────────── */

void app_main(void) {
    ESP_LOGI(TAG, "Plexus Generic Firmware v%s", FIRMWARE_VERSION);

    /* Initialize encrypted NVS */
    esp_err_t err = init_nvs_encrypted();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }

    /* ── Mode selection ──────────────────────────────────────────────────── */

    if (!provisioning_has_api_key()) {
        /* No credentials — enter provisioning mode */
        provisioning_start_serial(); /* Blocks, reboots when done */
        return; /* Never reached */
    }

    /* ── Operational mode ────────────────────────────────────────────────── */

    ESP_LOGI(TAG, "Credentials found — starting operational mode");

    /* Load credentials from NVS */
    char api_key[PLEXUS_MAX_API_KEY_LEN] = {0};
    char endpoint[PLEXUS_MAX_ENDPOINT_LEN] = {0};

    provisioning_load_api_key(api_key, sizeof(api_key));
    provisioning_load_endpoint(endpoint, sizeof(endpoint));

    /* Connect WiFi */
    err = wifi_init_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed — rebooting in 10s");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }

    /* Sync time for accurate telemetry timestamps */
    sync_time();

    /* Initialize Plexus client */
    plexus_client_t* px = plexus_init(api_key, "pending");
    if (!px) {
        ESP_LOGE(TAG, "Failed to initialize Plexus client");
        return;
    }

    plexus_set_endpoint(px, endpoint);
    plexus_set_device_info(px, "ESP32", FIRMWARE_VERSION);
    plexus_set_device_identity(px, "plexus-firmware", "ESP32");

    /* Register device to create source on server */
    if (!plexus_is_registered(px)) {
        ESP_LOGI(TAG, "Registering as new device...");
        plexus_err_t reg_err = plexus_register_device(px);
        if (reg_err != PLEXUS_OK) {
            ESP_LOGW(TAG, "Registration failed: %s — continuing with API key",
                     plexus_strerror(reg_err));
        } else {
            ESP_LOGI(TAG, "Device registered");
        }
    }

    /* ── I2C sensors ────────────────────────────────────────────────────── */

    plexus_hal_i2c_init(0);
    plexus_scan_sensors(px);

    uint8_t sensor_count = plexus_detected_sensor_count(px);
    ESP_LOGI(TAG, "Detected %d I2C sensor(s):", sensor_count);
    for (uint8_t i = 0; i < sensor_count; i++) {
        const plexus_detected_sensor_t* s = plexus_detected_sensor(px, i);
        ESP_LOGI(TAG, "  [0x%02X] %s (%d metrics)",
                 s->addr, s->descriptor->name, s->descriptor->metric_count);
    }

    /* ── System metrics (always available) ───────────────────────────── */

    system_metrics_register(px);
    ESP_LOGI(TAG, "System metrics registered");

    /* ── Built-in ADC auto-detection ─────────────────────────────────── */

    int adc_count = adc_auto_detect();
    if (adc_count > 0) {
        adc_register_metrics(px);
        ESP_LOGI(TAG, "ADC: %d active channel(s)", adc_count);
    }

    /* ── GPS (UART2, non-blocking probe) ─────────────────────────────── */

    bool gps_ok = gps_init_default();
    if (gps_ok) {
        gps_register_metrics(px);
        ESP_LOGI(TAG, "GPS module detected");
    } else {
        ESP_LOGI(TAG, "GPS not detected — will retry if data appears");
        /* GPS read is safe to call even without a module */
        gps_register_metrics(px);
    }

    /* ── CAN bus (TWAI, requires external transceiver) ───────────────── */

    bool can_ok = canbus_init_default();
    if (can_ok) {
        canbus_register_metrics(px);
        ESP_LOGI(TAG, "CAN bus initialized");
    } else {
        ESP_LOGW(TAG, "CAN bus init failed — transceiver may not be connected");
    }

    /* Send initial heartbeat with full sensor/subsystem info */
    plexus_heartbeat(px);

    /* ── Main telemetry loop ─────────────────────────────────────────────── */

    ESP_LOGI(TAG, "Entering main loop");

    uint32_t tick = 0;

    while (1) {
        /* I2C sensors (BME280, MPU6050, INA219, etc.) */
        plexus_sensor_read_all(px);

        /* Built-in ADC channels */
        adc_read_all(px);

        /* GPS NMEA parsing (non-blocking) */
        gps_read(px);

        /* CAN bus frames (non-blocking) */
        if (can_ok) {
            canbus_read(px);
        }

        /* System metrics every ~5 seconds (50 ticks at 100ms) */
        if (tick % 50 == 0) {
            system_metrics_read(px);
        }

        plexus_tick(px);
        tick++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
