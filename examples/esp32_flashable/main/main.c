/**
 * @file main.c
 * @brief Browser-flashable ESP32 firmware for Plexus
 *
 * This firmware is designed to be flashed via the browser using ESP Web Tools.
 * All configuration (API key, source ID, WiFi credentials) is read from a
 * dedicated NVS partition ("plexus_cfg") that the browser generates and
 * flashes alongside this binary.
 *
 * Telemetry sent:
 *   - cpu_temp_c     Internal temperature sensor (ESP32 only, not S2/S3/C3)
 *   - free_heap      Free heap memory in bytes
 *   - uptime_s       Seconds since boot
 *   - wifi_rssi      WiFi signal strength in dBm
 *
 * LED status (GPIO 2 — built-in LED on most ESP32 dev boards):
 *   - Fast blink (100ms) = no config found in NVS
 *   - Slow blink (500ms) = connecting to WiFi / Plexus
 *   - Solid ON           = streaming telemetry
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "plexus.h"
#include "plexus_hal_nvs_config.h"

static const char* TAG = "plexus_flash";

/* LED on GPIO 2 (built-in blue LED on most ESP32 boards) */
#define LED_GPIO GPIO_NUM_2

/* WiFi event group */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;
#define MAX_RETRY 10

/* ========================================================================= */
/* LED control                                                               */
/* ========================================================================= */

static void led_init(void) {
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);
}

static void led_set(bool on) {
    gpio_set_level(LED_GPIO, on ? 1 : 0);
}

static void led_blink_task(void* param) {
    int period_ms = (int)(intptr_t)param;
    while (1) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(period_ms));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

static TaskHandle_t s_blink_task = NULL;

static void led_start_blink(int period_ms) {
    if (s_blink_task) {
        vTaskDelete(s_blink_task);
        s_blink_task = NULL;
    }
    xTaskCreate(led_blink_task, "blink", 1024, (void*)(intptr_t)period_ms,
                1, &s_blink_task);
}

static void led_stop_blink(void) {
    if (s_blink_task) {
        vTaskDelete(s_blink_task);
        s_blink_task = NULL;
    }
}

/* ========================================================================= */
/* WiFi connection                                                           */
/* ========================================================================= */

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d)...", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_connect(const char* ssid, const char* password) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi '%s'...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                       WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                       pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected to %s", ssid);
        return true;
    } else {
        ESP_LOGE(TAG, "WiFi connection failed after %d retries", MAX_RETRY);
        return false;
    }
}

/* ========================================================================= */
/* Telemetry                                                                 */
/* ========================================================================= */

static int8_t read_internal_temp(void) {
    /* ESP32 internal temperature sensor — not available on all variants */
#if CONFIG_IDF_TARGET_ESP32
    extern uint8_t temprature_sens_read(void);  /* ROM function */
    uint8_t raw = temprature_sens_read();
    return (int8_t)((raw - 128) / 1.6f);  /* Approximate °C */
#else
    return 0;
#endif
}

static int16_t read_wifi_rssi(void) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

/* ========================================================================= */
/* Main                                                                      */
/* ========================================================================= */

void app_main(void) {
    led_init();

    ESP_LOGI(TAG, "╔═══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Plexus Flashable Firmware v%s     ║", plexus_version());
    ESP_LOGI(TAG, "╚═══════════════════════════════════════╝");

    /* Read configuration from NVS */
    plexus_nvs_config_t cfg;
    if (!plexus_nvs_config_read(&cfg) || !cfg.valid) {
        ESP_LOGE(TAG, "No valid config found in NVS!");
        ESP_LOGE(TAG, "Flash this device using the Plexus web interface.");
        led_start_blink(100);  /* Fast blink = no config */
        return;  /* Stop here — device needs to be reflashed */
    }

    ESP_LOGI(TAG, "Config loaded: source=%s", cfg.source_id);

    /* Connect to WiFi */
    led_start_blink(500);  /* Slow blink = connecting */

    if (!wifi_connect(cfg.wifi_ssid, cfg.wifi_pass)) {
        ESP_LOGE(TAG, "WiFi failed — restarting in 10 seconds...");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }

    /* Initialize time via NTP */
    extern void plexus_hal_init_time(const char* ntp_server);
    plexus_hal_init_time("pool.ntp.org");

    /* Initialize Plexus client */
    plexus_client_t* plexus = plexus_init(cfg.api_key, cfg.source_id);
    if (!plexus) {
        ESP_LOGE(TAG, "Failed to initialize Plexus client");
        led_start_blink(100);
        return;
    }

    /* Use custom endpoint if configured */
    if (cfg.endpoint[0]) {
        plexus_set_endpoint(plexus, cfg.endpoint);
    }

    /* Auto-flush every 5 seconds */
    plexus_set_flush_interval(plexus, 5000);

    /* Solid LED = streaming */
    led_stop_blink();
    led_set(true);

    ESP_LOGI(TAG, "Streaming telemetry as '%s'...", cfg.source_id);

    /* Main telemetry loop */
    while (1) {
        int8_t temp = read_internal_temp();
        uint32_t free_heap = esp_get_free_heap_size();
        int64_t uptime_us = esp_timer_get_time();
        int16_t rssi = read_wifi_rssi();

        plexus_send(plexus, "cpu_temp_c", (float)temp);
        plexus_send(plexus, "free_heap", (float)free_heap);
        plexus_send(plexus, "uptime_s", (float)(uptime_us / 1000000));
        plexus_send(plexus, "wifi_rssi", (float)rssi);

        plexus_err_t err = plexus_tick(plexus);
        if (err != PLEXUS_OK) {
            ESP_LOGW(TAG, "Tick: %s", plexus_strerror(err));
            /* Blink briefly on error, then go back to solid */
            led_set(false);
            vTaskDelay(pdMS_TO_TICKS(100));
            led_set(true);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    plexus_free(plexus);
}
