/**
 * @file canbus.c
 * @brief ESP32 CAN bus (TWAI) implementation
 *
 * Receives CAN frames via the TWAI peripheral and emits telemetry.
 * Each unique CAN ID becomes a metric (e.g., "can.0x123").
 */

#include "canbus.h"
#include "plexus_firmware_config.h"
#include "plexus.h"

#include "driver/twai.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

static const char* TAG = "canbus";

static bool s_initialized = false;
static uint32_t s_frames_received = 0;

/* Track unique CAN IDs we've seen for heartbeat registration */
typedef struct {
    uint32_t id;
    char metric_name[24]; /* "can.0x1FF" */
    bool seen;
} can_id_entry_t;

static can_id_entry_t s_tracked_ids[CAN_MAX_TRACKED_IDS];
static int s_tracked_count = 0;

static const char* find_or_create_metric_name(uint32_t id) {
    /* Search existing entries */
    for (int i = 0; i < s_tracked_count; i++) {
        if (s_tracked_ids[i].id == id) {
            return s_tracked_ids[i].metric_name;
        }
    }

    /* Create new entry */
    if (s_tracked_count >= CAN_MAX_TRACKED_IDS) return NULL;

    can_id_entry_t* entry = &s_tracked_ids[s_tracked_count];
    entry->id = id;
    entry->seen = true;
    snprintf(entry->metric_name, sizeof(entry->metric_name), "can.0x%03lX", (unsigned long)id);
    s_tracked_count++;

    return entry->metric_name;
}

bool canbus_init(int tx_pin, int rx_pin, uint32_t bitrate) {
    twai_timing_config_t timing;

    switch (bitrate) {
        case 125000:  timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();  break;
        case 250000:  timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();  break;
        case 500000:  timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();  break;
        case 1000000: timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();    break;
        default:
            ESP_LOGE(TAG, "Unsupported bitrate: %lu", (unsigned long)bitrate);
            return false;
    }

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)tx_pin, (gpio_num_t)rx_pin, TWAI_MODE_NORMAL);

    /* Accept all frames (no filter) */
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &timing, &f_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI install failed: %s", esp_err_to_name(err));
        return false;
    }

    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        return false;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "CAN bus initialized (TX=%d, RX=%d, %lu bps)",
             tx_pin, rx_pin, (unsigned long)bitrate);
    return true;
}

bool canbus_init_default(void) {
    return canbus_init(5, 4, 500000);
}

void canbus_register_metrics(plexus_client_t* client) {
    if (!client) return;

#if PLEXUS_ENABLE_HEARTBEAT
    plexus_register_metric(client, "can.frames_total");
    plexus_register_metric(client, "can.bus_errors");
    /* Individual CAN IDs are registered dynamically as they appear */
#endif
}

void canbus_read(plexus_client_t* client) {
    if (!client || !s_initialized) return;

    /* Read up to 16 frames per tick (non-blocking) */
    for (int i = 0; i < 16; i++) {
        twai_message_t msg;
        esp_err_t err = twai_receive(&msg, 0); /* 0 = don't wait */

        if (err == ESP_ERR_TIMEOUT) break; /* No more frames */
        if (err != ESP_OK) continue;

        s_frames_received++;

        uint32_t can_id = msg.identifier;
        const char* metric_name = find_or_create_metric_name(can_id);
        if (!metric_name) continue; /* Tracking table full */

        /* Interpret data based on DLC */
        if (msg.data_length_code >= 4) {
            /* First 4 bytes as float (common in automotive/industrial) */
            float fval;
            memcpy(&fval, msg.data, sizeof(float));

            /* Sanity check — skip NaN/Inf */
            if (fval == fval && fval < 1e10f && fval > -1e10f) {
                plexus_send_number(client, metric_name, (double)fval);
            }
        } else if (msg.data_length_code >= 2) {
            /* 2 bytes as int16 */
            int16_t ival = (int16_t)((msg.data[0] << 8) | msg.data[1]);
            plexus_send_number(client, metric_name, (double)ival);
        } else if (msg.data_length_code == 1) {
            plexus_send_number(client, metric_name, (double)msg.data[0]);
        }

#if PLEXUS_ENABLE_STRING_VALUES
        /* Also send raw hex representation */
        char hex_name[32];
        snprintf(hex_name, sizeof(hex_name), "can.raw.0x%03lX", (unsigned long)can_id);
        char hex_str[24];
        int pos = 0;
        for (int b = 0; b < msg.data_length_code && pos < 22; b++) {
            pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02X", msg.data[b]);
        }
        plexus_send_string(client, hex_name, hex_str);
#endif
    }

    /* Periodically send bus stats */
    plexus_send_number(client, "can.frames_total", (double)s_frames_received);

    twai_status_info_t status;
    if (twai_get_status_info(&status) == ESP_OK) {
        plexus_send_number(client, "can.bus_errors",
                           (double)(status.tx_error_counter + status.rx_error_counter));
    }
}

bool canbus_send(uint32_t id, const uint8_t* data, uint8_t len, bool extended) {
    if (!s_initialized || !data || len > 8) return false;

    twai_message_t msg = {0};
    msg.identifier = id;
    msg.data_length_code = len;
    msg.extd = extended ? 1 : 0;
    memcpy(msg.data, data, len);

    return (twai_transmit(&msg, pdMS_TO_TICKS(100)) == ESP_OK);
}

uint32_t canbus_frames_received(void) {
    return s_frames_received;
}
