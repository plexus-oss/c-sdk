/**
 * @file main.c
 * @brief Plexus ESP32 WebSocket Example — Telemetry + Commands
 *
 * Demonstrates the C SDK's WebSocket transport:
 *   - Real-time telemetry (<100ms to dashboard)
 *   - Typed commands from dashboard to device
 *   - Dual transport (WS for real-time + HTTP for persistence)
 *
 * This example registers a "honk" command that toggles a GPIO pin.
 * On the dashboard, a button with a duration slider appears automatically.
 *
 * Hardware:
 *   - ESP32-WROOM-32
 *   - LED or relay on GPIO 13 (configurable below)
 *
 * Boot flow:
 *   1. No config → serial config mode (WiFi + API key from browser)
 *   2. Config exists → WiFi → WebSocket → streaming
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

static const char* TAG = "plexus_ws_example";

/* Pin for command demo (LED, buzzer, or relay) */
#define COMMAND_GPIO GPIO_NUM_13
#define LED_GPIO     GPIO_NUM_2

/* ========================================================================= */
/* LED control (same pattern as flashable firmware)                          */
/* ========================================================================= */

static void led_init(void) {
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    gpio_reset_pin(COMMAND_GPIO);
    gpio_set_direction(COMMAND_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(COMMAND_GPIO, 0);
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
/* Serial config mode (same as flashable firmware)                          */
/* ========================================================================= */

#define UART_BUF_SIZE 1024
#define LINE_BUF_SIZE 256

static void serial_config_mode(void) {
    uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM_0, &uart_cfg);
    uart_driver_install(UART_NUM_0, UART_BUF_SIZE, 0, 0, NULL, 0);

    const char* ready = "PLEXUS:READY\n";
    uart_write_bytes(UART_NUM_0, ready, strlen(ready));
    ESP_LOGI(TAG, "Serial config mode — send PLEXUS:key=value lines");
    ESP_LOGI(TAG, "Required: api_key, source_id, wifi_ssid, wifi_pass, org_id");

    nvs_handle_t nvs;
    if (nvs_open("plexus_cfg", NVS_READWRITE, &nvs) != ESP_OK) return;

    char line_buf[LINE_BUF_SIZE] = {0};
    int line_pos = 0;
    uint8_t byte;

    while (1) {
        int len = uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        if (byte == '\n' || byte == '\r') {
            if (line_pos == 0) continue;
            line_buf[line_pos] = '\0';
            if (line_pos > 0 && line_buf[line_pos - 1] == '\r')
                line_buf[line_pos - 1] = '\0';

            if (strncmp(line_buf, "PLEXUS:", 7) != 0) { line_pos = 0; continue; }
            const char* payload = line_buf + 7;

            if (strcmp(payload, "COMMIT") == 0) {
                /* Validate required keys (including org_id for WebSocket) */
                const char* required[] = {"api_key", "source_id", "wifi_ssid", "wifi_pass", "org_id"};
                bool valid = true;
                for (int i = 0; i < 5; i++) {
                    char tmp[128]; size_t tmp_len = sizeof(tmp);
                    if (nvs_get_str(nvs, required[i], tmp, &tmp_len) != ESP_OK) {
                        char err_msg[64];
                        snprintf(err_msg, sizeof(err_msg), "PLEXUS:ERROR=missing %s\n", required[i]);
                        uart_write_bytes(UART_NUM_0, err_msg, strlen(err_msg));
                        valid = false;
                    }
                }
                if (valid) {
                    nvs_commit(nvs); nvs_close(nvs);
                    uart_write_bytes(UART_NUM_0, "PLEXUS:SAVED\n", 13);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    esp_restart();
                }
            } else {
                char* eq = strchr((char*)payload, '=');
                if (eq && eq != payload) {
                    *eq = '\0';
                    nvs_set_str(nvs, payload, eq + 1);
                    uart_write_bytes(UART_NUM_0, "PLEXUS:OK\n", 10);
                }
            }
            line_pos = 0;
        } else if (line_pos < LINE_BUF_SIZE - 1) {
            line_buf[line_pos++] = (char)byte;
        }
    }
}

/* ========================================================================= */
/* WiFi (same pattern as flashable firmware)                                */
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
        if (s_retry_num < MAX_RETRY) { esp_wifi_connect(); s_retry_num++; }
        else xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
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

    esp_event_handler_instance_t h1, h2;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &h1));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &h2));

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                       WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                       pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

/* ========================================================================= */
/* Command handler: "honk"                                                   */
/* ========================================================================= */

static void honk_handler(const char* cmd_id, const char* params_json, void* user_data) {
    plexus_client_t* px = (plexus_client_t*)user_data;

    /* Default 500ms, could parse duration_ms from params_json */
    int duration_ms = 500;

    ESP_LOGI(TAG, "HONK! duration=%d ms", duration_ms);

    gpio_set_level(COMMAND_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_set_level(COMMAND_GPIO, 0);

    plexus_command_respond(px, cmd_id, "{\"honked\":true}", NULL);
}

/* ========================================================================= */
/* Main                                                                      */
/* ========================================================================= */

void app_main(void) {
    led_init();

    /* NVS init */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "╔═══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Plexus WebSocket Example v%s     ║", plexus_version());
    ESP_LOGI(TAG, "╚═══════════════════════════════════════╝");

    /* Load config */
    plexus_nvs_config_t cfg;
    if (!plexus_nvs_config_read(&cfg) || !cfg.valid) {
        led_start_blink(100);
        serial_config_mode();
        return;
    }

    /* Read org_id from NVS (not in plexus_nvs_config_t) */
    char org_id[64] = {0};
    {
        nvs_handle_t nvs;
        if (nvs_open("plexus_cfg", NVS_READONLY, &nvs) == ESP_OK) {
            size_t len = sizeof(org_id);
            nvs_get_str(nvs, "org_id", org_id, &len);
            nvs_close(nvs);
        }
    }

    if (org_id[0] == '\0') {
        ESP_LOGE(TAG, "org_id not configured — entering serial config mode");
        led_start_blink(100);
        serial_config_mode();
        return;
    }

    /* WiFi */
    led_start_blink(500);
    if (!wifi_connect(cfg.wifi_ssid, cfg.wifi_pass)) {
        ESP_LOGE(TAG, "WiFi failed — restarting in 10s");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }

    /* NTP */
    extern void plexus_hal_init_time(const char* ntp_server);
    plexus_hal_init_time("pool.ntp.org");

    /* Plexus client */
    plexus_client_t* px = plexus_init(cfg.api_key, cfg.source_id);
    if (!px) {
        ESP_LOGE(TAG, "Failed to init Plexus");
        return;
    }

    if (cfg.endpoint[0]) {
        plexus_set_endpoint(px, cfg.endpoint);
    }
    plexus_set_flush_interval(px, 500);  /* 500ms for responsive dashboard */

    /* Enable dual transport: WS for real-time + HTTP for persistence & device creation */
    plexus_set_http_persist(px, true);

    /* Configure WebSocket */
    plexus_set_org_id(px, org_id);

    /* Register command: "honk" with duration_ms parameter */
    plexus_param_t honk_params[] = {
        plexus_param_int("duration_ms", 100, 2000),
    };
    plexus_command_register(px, "honk", "Honk the horn for a specified duration",
                           honk_handler, px, honk_params, 1);

    /* Connect WebSocket */
    plexus_err_t ws_err = plexus_ws_connect(px);
    if (ws_err != PLEXUS_OK) {
        ESP_LOGW(TAG, "WS connect failed: %s — falling back to HTTP", plexus_strerror(ws_err));
    }

    /* Streaming */
    led_stop_blink();
    led_set(true);

    ESP_LOGI(TAG, "Streaming as '%s' via WebSocket", cfg.source_id);

    while (1) {
        /* System metrics */
        plexus_send(px, "free_heap", (float)esp_get_free_heap_size());
        plexus_send(px, "uptime_s", (float)(esp_timer_get_time() / 1000000));

        /* WS state for diagnostics */
        plexus_ws_state_t state = plexus_ws_state(px);
        plexus_send(px, "ws_connected", (float)(state == PLEXUS_WS_CONNECTED ? 1.0 : 0.0));

        /* plexus_tick drives: auto-flush + WS state machine + command dispatch */
        plexus_err_t tick_err = plexus_tick(px);
        if (tick_err != PLEXUS_OK && tick_err != PLEXUS_ERR_NO_DATA) {
            ESP_LOGW(TAG, "Tick: %s", plexus_strerror(tick_err));
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
