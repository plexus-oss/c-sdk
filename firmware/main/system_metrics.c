/**
 * @file system_metrics.c
 * @brief ESP32 system health metrics
 *
 * These are always available on any ESP32 — no external hardware needed.
 * Matches the Python agent's SystemSensor behavior.
 */

#include "system_metrics.h"
#include "plexus_firmware_config.h"
#include "plexus.h"

#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_IDF_TARGET_ESP32
#include "driver/temperature_sensor.h"
#endif

static const char* TAG = "sys_metrics";

/* Metric names (must match what we register with heartbeat) */
#define METRIC_CHIP_TEMP       "sys.chip_temp"
#define METRIC_FREE_HEAP       "sys.free_heap_kb"
#define METRIC_MIN_FREE_HEAP   "sys.min_free_heap_kb"
#define METRIC_UPTIME          "sys.uptime_s"
#define METRIC_WIFI_RSSI       "sys.wifi_rssi"
#define METRIC_TASK_COUNT      "sys.task_count"
#define METRIC_FREE_HEAP_PCT   "sys.free_heap_pct"

/* Internal temp sensor handle (ESP32-S2/S3/C3/C6 — not classic ESP32) */
#if SOC_TEMP_SENSOR_SUPPORTED
static temperature_sensor_handle_t s_temp_handle = NULL;
#endif

void system_metrics_register(plexus_client_t* client) {
    if (!client) return;

#if PLEXUS_ENABLE_HEARTBEAT
    plexus_register_metric(client, METRIC_CHIP_TEMP);
    plexus_register_metric(client, METRIC_FREE_HEAP);
    plexus_register_metric(client, METRIC_MIN_FREE_HEAP);
    plexus_register_metric(client, METRIC_UPTIME);
    plexus_register_metric(client, METRIC_WIFI_RSSI);
    plexus_register_metric(client, METRIC_TASK_COUNT);
    plexus_register_metric(client, METRIC_FREE_HEAP_PCT);
#endif

    /* Initialize temperature sensor if supported */
#if SOC_TEMP_SENSOR_SUPPORTED
    temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t err = temperature_sensor_install(&temp_cfg, &s_temp_handle);
    if (err == ESP_OK) {
        temperature_sensor_enable(s_temp_handle);
        ESP_LOGI(TAG, "Internal temperature sensor initialized");
    } else {
        ESP_LOGW(TAG, "Temperature sensor init failed: %s", esp_err_to_name(err));
    }
#endif

    ESP_LOGI(TAG, "System metrics registered (7 metrics)");
}

void system_metrics_read(plexus_client_t* client) {
    if (!client) return;

    /* ── Chip temperature ─────────────────────────────────────────────── */
#if SOC_TEMP_SENSOR_SUPPORTED
    if (s_temp_handle) {
        float temp_c = 0;
        if (temperature_sensor_get_celsius(s_temp_handle, &temp_c) == ESP_OK) {
            plexus_send_number(client, METRIC_CHIP_TEMP, (double)temp_c);
        }
    }
#else
    /* Classic ESP32: use the legacy internal temp sensor (approximate) */
    extern uint8_t temprature_sens_read(void); /* ROM function, typo is intentional */
    float temp_f = (float)temprature_sens_read();
    float temp_c = (temp_f - 32.0f) / 1.8f; /* Convert Fahrenheit to Celsius */
    if (temp_c > -20.0f && temp_c < 100.0f) { /* Sanity check */
        plexus_send_number(client, METRIC_CHIP_TEMP, (double)temp_c);
    }
#endif

    /* ── Free heap ────────────────────────────────────────────────────── */
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    uint32_t total_heap = free_heap + (free_heap - min_free_heap) * 2; /* approximate */

    plexus_send_number(client, METRIC_FREE_HEAP, (double)free_heap / 1024.0);
    plexus_send_number(client, METRIC_MIN_FREE_HEAP, (double)min_free_heap / 1024.0);

    /* Heap percentage (approximate — ESP32 total heap varies by config) */
    if (total_heap > 0) {
        double pct = ((double)free_heap / (double)total_heap) * 100.0;
        if (pct > 100.0) pct = 100.0;
        plexus_send_number(client, METRIC_FREE_HEAP_PCT, pct);
    }

    /* ── Uptime ───────────────────────────────────────────────────────── */
    int64_t uptime_us = esp_timer_get_time();
    plexus_send_number(client, METRIC_UPTIME, (double)uptime_us / 1000000.0);

    /* ── WiFi RSSI ────────────────────────────────────────────────────── */
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        plexus_send_number(client, METRIC_WIFI_RSSI, (double)ap_info.rssi);
    }

    /* ── FreeRTOS task count ──────────────────────────────────────────── */
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    plexus_send_number(client, METRIC_TASK_COUNT, (double)task_count);
}
