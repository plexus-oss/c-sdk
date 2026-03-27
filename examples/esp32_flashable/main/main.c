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
#include "esp_partition.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "plexus.h"
#include "plexus_hal_nvs_config.h"
#include "i2c_scan.h"

static const char* TAG = "plexus_flash";

#define LED_GPIO GPIO_NUM_2

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

#define UART_BUF_SIZE 1024
#define LINE_BUF_SIZE 256

static void serial_config_mode(void) {
    /* Install UART driver for config input */
    uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_BITS_8,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCONTROL_DISABLE,
    };
    uart_param_config(UART_NUM_0, &uart_cfg);
    uart_driver_install(UART_NUM_0, UART_BUF_SIZE, 0, 0, NULL, 0);

    /* Signal readiness */
    const char* ready = "PLEXUS:READY\n";
    uart_write_bytes(UART_NUM_0, ready, strlen(ready));
    ESP_LOGI(TAG, "Entering serial config mode — send PLEXUS:key=value lines");

    /* Open NVS for writing */
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("plexus_cfg", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }

    char line_buf[LINE_BUF_SIZE] = {0};
    int line_pos = 0;
    uint8_t byte;

    while (1) {
        int len = uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        if (byte == '\n' || byte == '\r') {
            if (line_pos == 0) continue;  /* Skip empty lines */
            line_buf[line_pos] = '\0';

            /* Strip trailing \r if present */
            if (line_pos > 0 && line_buf[line_pos - 1] == '\r') {
                line_buf[line_pos - 1] = '\0';
            }

            /* Must start with PLEXUS: prefix */
            if (strncmp(line_buf, "PLEXUS:", 7) != 0) {
                line_pos = 0;
                continue;
            }

            const char* payload = line_buf + 7;

            if (strcmp(payload, "COMMIT") == 0) {
                /* Validate required keys */
                const char* required[] = {"api_key", "source_id", "wifi_ssid", "wifi_pass"};
                bool valid = true;
                for (int i = 0; i < 4; i++) {
                    char tmp[128];
                    size_t tmp_len = sizeof(tmp);
                    if (nvs_get_str(nvs, required[i], tmp, &tmp_len) != ESP_OK) {
                        char err_msg[64];
                        snprintf(err_msg, sizeof(err_msg), "PLEXUS:ERROR=missing %s\n", required[i]);
                        uart_write_bytes(UART_NUM_0, err_msg, strlen(err_msg));
                        ESP_LOGW(TAG, "Missing required key: %s", required[i]);
                        valid = false;
                    }
                }

                if (valid) {
                    nvs_commit(nvs);
                    nvs_close(nvs);
                    const char* saved = "PLEXUS:SAVED\n";
                    uart_write_bytes(UART_NUM_0, saved, strlen(saved));
                    ESP_LOGI(TAG, "Config saved — rebooting");
                    vTaskDelay(pdMS_TO_TICKS(200));
                    esp_restart();
                }
            } else {
                /* Parse key=value */
                char* eq = strchr((char*)payload, '=');
                if (eq && eq != payload) {
                    *eq = '\0';
                    const char* key = payload;
                    const char* val = eq + 1;
                    nvs_set_str(nvs, key, val);
                    ESP_LOGI(TAG, "Set %s = %s",
                             key, strcmp(key, "wifi_pass") == 0 ? "****" : val);
                    const char* ok = "PLEXUS:OK\n";
                    uart_write_bytes(UART_NUM_0, ok, strlen(ok));
                } else {
                    const char* err_msg = "PLEXUS:ERROR=invalid format\n";
                    uart_write_bytes(UART_NUM_0, err_msg, strlen(err_msg));
                }
            }

            line_pos = 0;
        } else if (line_pos < LINE_BUF_SIZE - 1) {
            line_buf[line_pos++] = (char)byte;
        }
    }
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

    ESP_LOGI(TAG, "╔═══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Plexus Flashable Firmware v%s     ║", plexus_version());
    ESP_LOGI(TAG, "╚═══════════════════════════════════════╝");

    /*
     * Config loading — two sources, checked in order:
     *
     * 1. Plain text in plexus_cfg flash partition (from browser flash).
     *    MUST be checked FIRST — before NVS init touches the partition.
     *    If found: import to NVS, erase raw partition, reboot.
     *
     * 2. NVS namespace "plexus_cfg" (from previous import or manual config).
     */

    /* Step 1: Check for plain-text config in raw flash (before NVS touches it) */
    {
        const esp_partition_t* part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "plexus_cfg");

        if (part) {
            char raw[1024] = {0};
            if (esp_partition_read(part, 0, raw, sizeof(raw) - 1) == ESP_OK &&
                raw[0] != '\xff' && raw[0] != '\0' &&
                strchr(raw, '=') != NULL) {  /* Sanity: must contain at least one '=' */

                ESP_LOGI(TAG, "Found plain-text config in flash — importing to NVS");

                nvs_handle_t nvs;
                if (nvs_open("plexus_cfg", NVS_READWRITE, &nvs) == ESP_OK) {
                    char* saveptr = NULL;
                    char* line = strtok_r(raw, "\n", &saveptr);
                    int count = 0;
                    while (line) {
                        char* eq = strchr(line, '=');
                        if (eq && eq != line) {
                            *eq = '\0';
                            const char* key = line;
                            const char* val = eq + 1;
                            size_t val_len = strlen(val);
                            if (val_len > 0 && val[val_len - 1] == '\r') {
                                ((char*)val)[val_len - 1] = '\0';
                            }
                            nvs_set_str(nvs, key, val);
                            ESP_LOGI(TAG, "  %s = %s", key,
                                     strcmp(key, "wifi_pass") == 0 ? "****" : val);
                            count++;
                        }
                        line = strtok_r(NULL, "\n", &saveptr);
                    }
                    nvs_commit(nvs);
                    nvs_close(nvs);

                    if (count > 0) {
                        esp_partition_erase_range(part, 0, part->size);
                        ESP_LOGI(TAG, "Imported %d keys — rebooting with new config", count);
                        vTaskDelay(pdMS_TO_TICKS(200));
                        esp_restart();
                    }
                }
            }
        }
    }

    /* Step 2: Read config from NVS */
    plexus_nvs_config_t cfg;
    if (!plexus_nvs_config_read(&cfg) || !cfg.valid) {
        ESP_LOGI(TAG, "No config found — entering serial config mode");
        led_start_blink(100);
        serial_config_mode();  /* Blocks until config received and reboots */
        return;
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

    /* ================================================================= */
    /* I2C sensor auto-detection                                        */
    /* ================================================================= */

    i2c_scan_result_t i2c_result = {0};

    if (i2c_scan_init(21, 22)) {  /* Default ESP32 I2C pins */
        ESP_LOGI(TAG, "Scanning I2C bus...");
        i2c_scan_detect(&i2c_result);

        if (i2c_result.devices_str[0]) {
            plexus_send_string(plexus, "i2c_devices", i2c_result.devices_str);
        }
    } else {
        ESP_LOGW(TAG, "I2C init failed — skipping sensor scan");
    }

    /* ================================================================= */
    /* ADC initialization                                                */
    /* ================================================================= */

    adc_oneshot_unit_handle_t adc_handle = NULL;
    adc_cali_handle_t adc_cali = NULL;
    bool adc_ready = false;

    {
        adc_oneshot_unit_init_cfg_t adc_cfg = {
            .unit_id = ADC_UNIT_1,
        };
        if (adc_oneshot_new_unit(&adc_cfg, &adc_handle) == ESP_OK) {
            /* Configure 6 ADC1 channels (GPIO 32-37) */
            adc_oneshot_chan_cfg_t chan_cfg = {
                .atten = ADC_ATTEN_DB_12,
                .bitwidth = ADC_BITWIDTH_12,
            };
            for (int ch = ADC_CHANNEL_4; ch <= ADC_CHANNEL_9; ch++) {
                adc_oneshot_config_channel(adc_handle, ch, &chan_cfg);
            }

            /* Try to create calibration handle for mV conversion */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
            adc_cali_curve_fitting_config_t cali_cfg = {
                .unit_id = ADC_UNIT_1,
                .atten = ADC_ATTEN_DB_12,
                .bitwidth = ADC_BITWIDTH_12,
            };
            adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
            adc_cali_line_fitting_config_t cali_cfg = {
                .unit_id = ADC_UNIT_1,
                .atten = ADC_ATTEN_DB_12,
                .bitwidth = ADC_BITWIDTH_12,
            };
            adc_cali_create_scheme_line_fitting(&cali_cfg, &adc_cali);
#endif
            adc_ready = true;
            ESP_LOGI(TAG, "ADC initialized (6 channels on GPIO 32-37)");
        }
    }

    /* ================================================================= */
    /* Solid LED = streaming                                             */
    /* ================================================================= */

    led_stop_blink();
    led_set(true);

    ESP_LOGI(TAG, "Streaming telemetry as '%s'...", cfg.source_id);
    if (i2c_result.count > 0) {
        ESP_LOGI(TAG, "  I2C sensors: %d detected", i2c_result.count);
    }

    while (1) {
        /* System metrics */
        uint32_t free_heap = esp_get_free_heap_size();
        int64_t uptime_us = esp_timer_get_time();
        int16_t rssi = read_wifi_rssi();

        plexus_send(plexus, "free_heap", (float)free_heap);
        plexus_send(plexus, "uptime_s", (float)(uptime_us / 1000000));
        plexus_send(plexus, "wifi_rssi", (float)rssi);

        /* I2C sensor metrics */
        if (i2c_result.count > 0) {
            i2c_scan_read_all(&i2c_result);
            i2c_scan_send(plexus, &i2c_result);
        }

        /* ADC metrics (only send channels above noise floor) */
        if (adc_ready) {
            const char* adc_names[] = {"adc_ch0", "adc_ch1", "adc_ch2",
                                       "adc_ch3", "adc_ch4", "adc_ch5"};
            for (int ch = 0; ch < 6; ch++) {
                int raw = 0;
                if (adc_oneshot_read(adc_handle, ADC_CHANNEL_4 + ch, &raw) == ESP_OK) {
                    int mv = raw;  /* Fallback: raw value */
                    if (adc_cali) {
                        adc_cali_raw_to_voltage(adc_cali, raw, &mv);
                    }
                    if (mv > 50) {  /* Skip channels at noise floor */
                        plexus_send(plexus, adc_names[ch], (float)mv);
                    }
                }
            }
        }

        plexus_err_t tick_err = plexus_tick(plexus);
        if (tick_err != PLEXUS_OK) {
            ESP_LOGW(TAG, "Tick: %s", plexus_strerror(tick_err));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
