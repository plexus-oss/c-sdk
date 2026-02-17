/**
 * @file main.c
 * @brief Basic ESP32 example for Plexus C SDK
 *
 * This example demonstrates:
 * - WiFi connection
 * - NTP time synchronization
 * - Sending telemetry to Plexus
 *
 * Build with ESP-IDF:
 *   idf.py build flash monitor
 *
 * Or with PlatformIO:
 *   pio run -t upload -t monitor
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "plexus.h"

/* ========================================================================= */
/* Configuration - Update these values                                       */
/* ========================================================================= */

#define WIFI_SSID      "YourWiFiSSID"
#define WIFI_PASSWORD  "YourWiFiPassword"

#define PLEXUS_API_KEY  "plx_your_api_key_here"
#define PLEXUS_SOURCE_ID "esp32-sensor-001"

/* Optional: custom endpoint */
/* #define PLEXUS_ENDPOINT "https://your-domain.com/api/ingest" */

/* ========================================================================= */
/* WiFi connection                                                           */
/* ========================================================================= */

static const char *TAG = "plexus_example";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
#define MAX_RETRY 5

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying WiFi connection...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi...");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected to %s", WIFI_SSID);
        return true;
    } else {
        ESP_LOGE(TAG, "WiFi connection failed");
        return false;
    }
}

/* ========================================================================= */
/* Simulated sensor data                                                     */
/* ========================================================================= */

static float read_temperature(void) {
    /* Simulate temperature reading (20-30°C with some variation) */
    return 25.0f + ((float)(esp_random() % 1000) / 100.0f) - 5.0f;
}

static float read_humidity(void) {
    /* Simulate humidity reading (40-60%) */
    return 50.0f + ((float)(esp_random() % 2000) / 100.0f) - 10.0f;
}

static float read_pressure(void) {
    /* Simulate pressure reading (1000-1020 hPa) */
    return 1010.0f + ((float)(esp_random() % 2000) / 100.0f) - 10.0f;
}

/* ========================================================================= */
/* Main application                                                          */
/* ========================================================================= */

void app_main(void) {
    /* Initialize NVS (required for WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Plexus C SDK Example v%s", plexus_version());

    /* Connect to WiFi */
    if (!wifi_init_sta()) {
        ESP_LOGE(TAG, "Failed to connect to WiFi. Restarting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    /* Initialize time via NTP (optional but recommended) */
    extern void plexus_hal_init_time(const char* ntp_server);
    plexus_hal_init_time("pool.ntp.org");

    /* Initialize Plexus client */
    plexus_client_t* plexus = plexus_init(PLEXUS_API_KEY, PLEXUS_SOURCE_ID);
    if (!plexus) {
        ESP_LOGE(TAG, "Failed to initialize Plexus client");
        return;
    }

#ifdef PLEXUS_ENDPOINT
    plexus_set_endpoint(plexus, PLEXUS_ENDPOINT);
#endif

    ESP_LOGI(TAG, "Starting telemetry loop...");

    /* Main loop - send telemetry every 5 seconds */
    while (1) {
        float temp = read_temperature();
        float humidity = read_humidity();
        float pressure = read_pressure();

        ESP_LOGI(TAG, "Readings: temp=%.2f°C, humidity=%.2f%%, pressure=%.2fhPa",
                 temp, humidity, pressure);

        /* Queue metrics */
        plexus_err_t err;

        err = plexus_send_number(plexus, "temperature", temp);
        if (err != PLEXUS_OK) {
            ESP_LOGW(TAG, "Failed to queue temperature: %s", plexus_strerror(err));
        }

        err = plexus_send_number(plexus, "humidity", humidity);
        if (err != PLEXUS_OK) {
            ESP_LOGW(TAG, "Failed to queue humidity: %s", plexus_strerror(err));
        }

        err = plexus_send_number(plexus, "pressure", pressure);
        if (err != PLEXUS_OK) {
            ESP_LOGW(TAG, "Failed to queue pressure: %s", plexus_strerror(err));
        }

        /* Send with tags example */
#if PLEXUS_ENABLE_TAGS
        const char* tag_keys[] = {"location", "unit"};
        const char* tag_values[] = {"room-1", "celsius"};
        plexus_send_number_tagged(plexus, "room_temp", temp, tag_keys, tag_values, 2);
#endif

        /* Flush all queued metrics */
        ESP_LOGI(TAG, "Flushing %d metrics...", plexus_pending_count(plexus));
        err = plexus_flush(plexus);

        if (err == PLEXUS_OK) {
            ESP_LOGI(TAG, "Telemetry sent successfully");
        } else {
            ESP_LOGE(TAG, "Failed to send telemetry: %s", plexus_strerror(err));
        }

        /* Wait before next reading */
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    /* Cleanup (never reached in this example) */
    plexus_free(plexus);
}
