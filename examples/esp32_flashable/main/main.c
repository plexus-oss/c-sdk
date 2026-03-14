/**
 * @file main.c
 * @brief Browser-flashable ESP32 firmware for Plexus
 *
 * Two boot paths:
 *   1. Config exists in NVS → connect WiFi → stream telemetry
 *   2. No config → enter serial config mode → receive config from browser
 *      over UART → write to NVS → reboot into path 1
 *
 * Serial config protocol (text lines at 115200 baud):
 *   Browser sends:  PLEXUS:key=value\n  (one per line)
 *   Required keys:  api_key, source_id, wifi_ssid, wifi_pass
 *   Optional keys:  endpoint
 *   Finish with:    PLEXUS:COMMIT\n
 *   Device replies: PLEXUS:OK\n  after each line
 *                   PLEXUS:READY\n  when entering config mode
 *                   PLEXUS:SAVED\n  after commit (then reboots)
 *
 * LED status (GPIO 2):
 *   Fast blink (100ms) = waiting for serial config
 *   Slow blink (500ms) = connecting to WiFi
 *   Solid ON           = streaming telemetry
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
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "plexus.h"
#include "plexus_hal_nvs_config.h"

static const char* TAG = "plexus_flash";

#define LED_GPIO GPIO_NUM_2
#define UART_NUM UART_NUM_0
#define SERIAL_CONFIG_TIMEOUT_MS 60000
#define SERIAL_BUF_SIZE 256

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

static TaskHandle_t s_blink_task = NULL;

static void led_blink_task(void* param) {
    int period_ms = (int)(intptr_t)param;
    while (1) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(period_ms));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

static void led_start_blink(int period_ms) {
    if (s_blink_task) { vTaskDelete(s_blink_task); s_blink_task = NULL; }
    xTaskCreate(led_blink_task, "blink", 1024, (void*)(intptr_t)period_ms, 1, &s_blink_task);
}

static void led_stop_blink(void) {
    if (s_blink_task) { vTaskDelete(s_blink_task); s_blink_task = NULL; }
}

/* ========================================================================= */
/* Serial config mode                                                        */
/* ========================================================================= */

/** Write a config key-value pair to NVS */
static bool nvs_write_config(const char* key, const char* value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("plexus_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set '%s' failed: %s", key, esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    nvs_commit(handle);
    nvs_close(handle);
    return true;
}

/**
 * Enter serial config mode. Listens on UART0 for PLEXUS:key=value lines.
 * Returns true if config was saved and device should reboot.
 */
static bool serial_config_mode(void) {
    ESP_LOGI(TAG, "Entering serial config mode (waiting %ds)...", SERIAL_CONFIG_TIMEOUT_MS / 1000);

    /* Signal readiness to the browser */
    printf("PLEXUS:READY\n");
    fflush(stdout);

    char line[SERIAL_BUF_SIZE];
    int line_pos = 0;
    bool has_api_key = false;
    bool has_source_id = false;
    bool has_wifi_ssid = false;
    bool has_wifi_pass = false;
    int64_t start_ms = esp_timer_get_time() / 1000;

    while ((esp_timer_get_time() / 1000 - start_ms) < SERIAL_CONFIG_TIMEOUT_MS) {
        uint8_t byte;
        int len = uart_read_bytes(UART_NUM, &byte, 1, pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        if (byte == '\n' || byte == '\r') {
            if (line_pos == 0) continue;
            line[line_pos] = '\0';
            line_pos = 0;

            /* Parse PLEXUS:key=value or PLEXUS:COMMIT */
            if (strncmp(line, "PLEXUS:", 7) != 0) continue;

            const char* payload = line + 7;

            if (strcmp(payload, "COMMIT") == 0) {
                if (has_api_key && has_source_id && has_wifi_ssid && has_wifi_pass) {
                    printf("PLEXUS:SAVED\n");
                    fflush(stdout);
                    ESP_LOGI(TAG, "Config saved! Rebooting...");
                    vTaskDelay(pdMS_TO_TICKS(500));
                    return true;
                } else {
                    printf("PLEXUS:ERROR:missing required keys\n");
                    fflush(stdout);
                    ESP_LOGW(TAG, "COMMIT with missing keys: api_key=%d source_id=%d wifi_ssid=%d wifi_pass=%d",
                             has_api_key, has_source_id, has_wifi_ssid, has_wifi_pass);
                    continue;
                }
            }

            /* Parse key=value */
            const char* eq = strchr(payload, '=');
            if (!eq) {
                printf("PLEXUS:ERROR:bad format\n");
                fflush(stdout);
                continue;
            }

            /* Extract key and value */
            size_t key_len = eq - payload;
            if (key_len >= 32) { continue; }  /* key too long */

            char key[32] = {0};
            strncpy(key, payload, key_len);
            const char* value = eq + 1;

            /* Validate known keys */
            bool valid_key = false;
            if (strcmp(key, "api_key") == 0)    { has_api_key = true; valid_key = true; }
            if (strcmp(key, "source_id") == 0)  { has_source_id = true; valid_key = true; }
            if (strcmp(key, "wifi_ssid") == 0)  { has_wifi_ssid = true; valid_key = true; }
            if (strcmp(key, "wifi_pass") == 0)  { has_wifi_pass = true; valid_key = true; }
            if (strcmp(key, "endpoint") == 0)   { valid_key = true; }

            if (!valid_key) {
                printf("PLEXUS:ERROR:unknown key '%s'\n", key);
                fflush(stdout);
                continue;
            }

            if (nvs_write_config(key, value)) {
                printf("PLEXUS:OK\n");
                fflush(stdout);
                ESP_LOGI(TAG, "Set %s = %s", key,
                         strcmp(key, "wifi_pass") == 0 ? "****" : value);
            } else {
                printf("PLEXUS:ERROR:nvs write failed\n");
                fflush(stdout);
            }
        } else if (line_pos < SERIAL_BUF_SIZE - 1) {
            line[line_pos++] = (char)byte;
        }
    }

    ESP_LOGW(TAG, "Serial config timeout — no config received");
    return false;
}

/* ========================================================================= */
/* WiFi connection                                                           */
/* ========================================================================= */

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;
#define MAX_RETRY 10

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

    esp_event_handler_instance_t instance_any_id, instance_got_ip;
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

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

/* ========================================================================= */
/* Telemetry                                                                 */
/* ========================================================================= */

static int16_t read_wifi_rssi(void) {
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) ? ap_info.rssi : 0;
}

/* ========================================================================= */
/* Main                                                                      */
/* ========================================================================= */

void app_main(void) {
    led_init();

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Install UART driver for serial config mode */
    uart_driver_install(UART_NUM, SERIAL_BUF_SIZE * 2, 0, 0, NULL, 0);

    ESP_LOGI(TAG, "╔═══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Plexus Flashable Firmware v%s     ║", plexus_version());
    ESP_LOGI(TAG, "╚═══════════════════════════════════════╝");

    /* Read configuration from NVS */
    plexus_nvs_config_t cfg;
    if (!plexus_nvs_config_read(&cfg) || !cfg.valid) {
        ESP_LOGW(TAG, "No config found — entering serial config mode");
        led_start_blink(100);  /* Fast blink = waiting for config */

        if (serial_config_mode()) {
            esp_restart();  /* Config saved — reboot into normal mode */
        }

        /* Timeout — no config received, just keep blinking */
        ESP_LOGE(TAG, "No config received. Flash again with the Plexus web UI.");
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    ESP_LOGI(TAG, "Config loaded: source=%s", cfg.source_id);

    /* Connect to WiFi */
    led_start_blink(500);

    if (!wifi_connect(cfg.wifi_ssid, cfg.wifi_pass)) {
        ESP_LOGE(TAG, "WiFi failed — restarting in 10 seconds...");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }

    /* Initialize NTP */
    extern void plexus_hal_init_time(const char* ntp_server);
    plexus_hal_init_time("pool.ntp.org");

    /* Initialize Plexus client */
    plexus_client_t* plexus = plexus_init(cfg.api_key, cfg.source_id);
    if (!plexus) {
        ESP_LOGE(TAG, "Failed to initialize Plexus client");
        led_start_blink(100);
        return;
    }

    if (cfg.endpoint[0]) {
        plexus_set_endpoint(plexus, cfg.endpoint);
    }

    plexus_set_flush_interval(plexus, 5000);

    /* Solid LED = streaming */
    led_stop_blink();
    led_set(true);

    ESP_LOGI(TAG, "Streaming telemetry as '%s'...", cfg.source_id);

    while (1) {
        uint32_t free_heap = esp_get_free_heap_size();
        int64_t uptime_us = esp_timer_get_time();
        int16_t rssi = read_wifi_rssi();

        plexus_send(plexus, "free_heap", (float)free_heap);
        plexus_send(plexus, "uptime_s", (float)(uptime_us / 1000000));
        plexus_send(plexus, "wifi_rssi", (float)rssi);

        plexus_err_t err = plexus_tick(plexus);
        if (err != PLEXUS_OK) {
            ESP_LOGW(TAG, "Tick: %s", plexus_strerror(err));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
