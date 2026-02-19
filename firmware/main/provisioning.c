/**
 * @file provisioning.c
 * @brief Serial provisioning implementation
 *
 * Listens on UART0 for a JSON provisioning packet from the dashboard,
 * validates fields, stores credentials in encrypted NVS, and reboots.
 */

#include "provisioning.h"
#include "plexus_firmware_config.h"
#include "plexus.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include <string.h>
#include <stdio.h>

static const char* TAG = "provisioning";
static const char* NVS_NS = "plexus_prov";

/* ── NVS helpers ──────────────────────────────────────────────────────────── */

static esp_err_t nvs_read_str(const char* key, char* buf, size_t buf_len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t len = buf_len;
    err = nvs_get_str(handle, key, buf, &len);
    nvs_close(handle);
    return err;
}

static esp_err_t nvs_write_str(const char* key, const char* value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

bool provisioning_has_api_key(void) {
    char buf[8];
    return nvs_read_str(PROVISIONING_NVS_KEY_API_KEY, buf, sizeof(buf)) == ESP_OK;
}

esp_err_t provisioning_load_api_key(char* buf, size_t buf_len) {
    return nvs_read_str(PROVISIONING_NVS_KEY_API_KEY, buf, buf_len);
}

esp_err_t provisioning_load_endpoint(char* buf, size_t buf_len) {
    esp_err_t err = nvs_read_str(PROVISIONING_NVS_KEY_ENDPOINT, buf, buf_len);
    if (err != ESP_OK) {
        strncpy(buf, PLEXUS_DEFAULT_ENDPOINT, buf_len - 1);
        buf[buf_len - 1] = '\0';
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t provisioning_load_wifi_ssid(char* buf, size_t buf_len) {
    return nvs_read_str(PROVISIONING_NVS_KEY_WIFI_SSID, buf, buf_len);
}

esp_err_t provisioning_load_wifi_pass(char* buf, size_t buf_len) {
    return nvs_read_str(PROVISIONING_NVS_KEY_WIFI_PASS, buf, buf_len);
}

/* ── Minimal JSON string field extractor ──────────────────────────────────── */

/**
 * Extract a string value for the given key from a JSON string.
 * Handles escaped quotes within values.
 * Returns true if key found, false otherwise.
 */
static bool json_extract_string(const char* json, const char* key,
                                 char* out, size_t out_len) {
    /* Build search pattern: "key":" */
    char pattern[64];
    int n = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    if (n < 0 || (size_t)n >= sizeof(pattern)) return false;

    const char* start = strstr(json, pattern);
    if (!start) return false;

    start += strlen(pattern);

    size_t i = 0;
    while (*start && *start != '"' && i < out_len - 1) {
        if (*start == '\\' && *(start + 1)) {
            start++; /* skip backslash, take next char */
        }
        out[i++] = *start++;
    }
    out[i] = '\0';

    return i > 0;
}

/* ── UART serial line reader ──────────────────────────────────────────────── */

static int uart_read_line(char* buf, size_t buf_len, uint32_t timeout_ms) {
    size_t pos = 0;
    uint32_t elapsed = 0;
    const uint32_t poll_ms = 10;

    while (pos < buf_len - 1 && elapsed < timeout_ms) {
        uint8_t byte;
        int len = uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(poll_ms));
        if (len > 0) {
            if (byte == '\n' || byte == '\r') {
                if (pos > 0) {
                    buf[pos] = '\0';
                    return (int)pos;
                }
                continue; /* skip leading newlines */
            }
            buf[pos++] = (char)byte;
            elapsed = 0; /* reset timeout on data received */
        } else {
            elapsed += poll_ms;
        }
    }

    buf[pos] = '\0';
    return (pos > 0) ? (int)pos : -1;
}

/* ── LED blink for provisioning mode indicator ────────────────────────────── */

#define PROV_LED_GPIO GPIO_NUM_2 /* Built-in LED on most ESP32 DevKits */

static void led_blink_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PROV_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

static void led_toggle(void) {
    static int level = 0;
    level = !level;
    gpio_set_level(PROV_LED_GPIO, level);
}

/* ── Build sensor response ────────────────────────────────────────────────── */

static void build_sensor_response(char* buf, size_t buf_len) {
    /* Do a quick I2C scan to report detected sensors in the response */
    plexus_client_t* tmp = plexus_init("probe", "probe");
    int offset = 0;
    bool first = true;

    offset += snprintf(buf + offset, buf_len - offset,
        "{\"status\":\"ok\",\"firmware_version\":\"%s\",\"sensors\":[",
        "0.2.1");

    if (tmp) {
        plexus_hal_i2c_init(0);
        plexus_scan_sensors(tmp);

        uint8_t count = plexus_detected_sensor_count(tmp);
        for (uint8_t i = 0; i < count && (size_t)offset < buf_len - 40; i++) {
            const plexus_detected_sensor_t* s = plexus_detected_sensor(tmp, i);
            if (!first) buf[offset++] = ',';
            first = false;
            offset += snprintf(buf + offset, buf_len - offset,
                "\"%s\"", s->descriptor->name);
        }
        plexus_free(tmp);
    }

    /* Always report ESP32 system metrics */
    if (!first) buf[offset++] = ',';
    first = false;
    offset += snprintf(buf + offset, buf_len - offset, "\"ESP32 System\"");

    /* Probe for ADC channels */
    /* ADC auto-detect is non-destructive; skip here to keep probe fast */

    /* Report GPS and CAN as capabilities (actual detection happens in main) */
    if ((size_t)offset < buf_len - 40) {
        offset += snprintf(buf + offset, buf_len - offset,
            "],\"capabilities\":[\"system_metrics\",\"adc\",\"gps\",\"canbus\"]}\n");
    } else {
        snprintf(buf + offset, buf_len - offset, "]}\n");
    }
}

/* ── Main provisioning loop ───────────────────────────────────────────────── */

void provisioning_start_serial(void) {
    ESP_LOGI(TAG, "Entering provisioning mode — waiting for credentials on UART0");

    /* Configure UART0 for provisioning */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);

    led_blink_init();

    char line[PROVISIONING_MAX_LINE_LEN];
    char api_key[PROVISIONING_MAX_FIELD_LEN];
    char endpoint[PROVISIONING_MAX_FIELD_LEN];
    char wifi_ssid[64];
    char wifi_pass[64];

    while (1) {
        led_toggle();

        int len = uart_read_line(line, sizeof(line), 500);
        if (len <= 0) continue;

        ESP_LOGI(TAG, "Received %d bytes", len);

        /* Validate: must contain api_key and wifi_ssid at minimum */
        if (!json_extract_string(line, "api_key", api_key, sizeof(api_key))) {
            ESP_LOGW(TAG, "Missing api_key field");
            uart_write_bytes(UART_NUM_0,
                "{\"status\":\"error\",\"message\":\"missing api_key\"}\n", 47);
            continue;
        }

        if (!json_extract_string(line, "wifi_ssid", wifi_ssid, sizeof(wifi_ssid))) {
            ESP_LOGW(TAG, "Missing wifi_ssid field");
            uart_write_bytes(UART_NUM_0,
                "{\"status\":\"error\",\"message\":\"missing wifi_ssid\"}\n", 50);
            continue;
        }

        /* Optional fields */
        if (!json_extract_string(line, "endpoint", endpoint, sizeof(endpoint))) {
            strncpy(endpoint, PLEXUS_DEFAULT_ENDPOINT, sizeof(endpoint) - 1);
            endpoint[sizeof(endpoint) - 1] = '\0';
        }

        if (!json_extract_string(line, "wifi_pass", wifi_pass, sizeof(wifi_pass))) {
            wifi_pass[0] = '\0'; /* Open network */
        }

        /* Validate API key format */
        if (strncmp(api_key, "plx_", 4) != 0 || strlen(api_key) < 10) {
            ESP_LOGW(TAG, "Invalid API key format");
            uart_write_bytes(UART_NUM_0,
                "{\"status\":\"error\",\"message\":\"invalid api_key format\"}\n", 54);
            continue;
        }

        /* Store credentials in encrypted NVS */
        ESP_LOGI(TAG, "Storing credentials in NVS");

        if (nvs_write_str(PROVISIONING_NVS_KEY_API_KEY, api_key) != ESP_OK ||
            nvs_write_str(PROVISIONING_NVS_KEY_ENDPOINT, endpoint) != ESP_OK ||
            nvs_write_str(PROVISIONING_NVS_KEY_WIFI_SSID, wifi_ssid) != ESP_OK ||
            nvs_write_str(PROVISIONING_NVS_KEY_WIFI_PASS, wifi_pass) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store credentials");
            uart_write_bytes(UART_NUM_0,
                "{\"status\":\"error\",\"message\":\"nvs write failed\"}\n", 50);
            continue;
        }

        ESP_LOGI(TAG, "Provisioning complete");

        /* Send response with firmware version and detected sensors */
        char response[512];
        build_sensor_response(response, sizeof(response));
        uart_write_bytes(UART_NUM_0, response, strlen(response));
        uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(1000));

        /* Give the dashboard time to read the response */
        vTaskDelay(pdMS_TO_TICKS(500));

        /* Reboot into operational mode */
        ESP_LOGI(TAG, "Rebooting into operational mode...");
        esp_restart();
    }
}
