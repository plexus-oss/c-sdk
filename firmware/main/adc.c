/**
 * @file adc.c
 * @brief ESP32 built-in ADC reading implementation
 */

#include "adc.h"
#include "plexus_firmware_config.h"
#include "plexus.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

static const char* TAG = "adc";

typedef struct {
    int channel;
    char metric_name[48];
    adc_atten_t attenuation;
    bool active;
} adc_channel_config_t;

static adc_channel_config_t s_channels[ADC_MAX_CHANNELS];
static int s_channel_count = 0;
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;

static void ensure_adc_init(void) {
    if (s_adc_handle) return;

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC init failed: %s", esp_err_to_name(err));
        return;
    }

    /* Try to create calibration handle for voltage conversion */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali_handle);
#endif
}

bool adc_add_channel(int channel, const char* metric_name, int attenuation) {
    if (s_channel_count >= ADC_MAX_CHANNELS) return false;
    if (channel < 0 || channel > 7) return false;

    ensure_adc_init();
    if (!s_adc_handle) return false;

    adc_atten_t atten = ADC_ATTEN_DB_12; /* Default: 0-3.3V range */
    if (attenuation >= 0 && attenuation <= 3) {
        atten = (adc_atten_t)attenuation;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t err = adc_oneshot_config_channel(s_adc_handle, (adc_channel_t)channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure ADC channel %d", channel);
        return false;
    }

    adc_channel_config_t* cfg = &s_channels[s_channel_count];
    cfg->channel = channel;
    cfg->attenuation = atten;
    cfg->active = true;
    strncpy(cfg->metric_name, metric_name, sizeof(cfg->metric_name) - 1);
    cfg->metric_name[sizeof(cfg->metric_name) - 1] = '\0';

    s_channel_count++;
    ESP_LOGI(TAG, "ADC channel %d → %s", channel, metric_name);
    return true;
}

int adc_auto_detect(void) {
    ensure_adc_init();
    if (!s_adc_handle) return 0;

    int found = 0;
    char name[48];

    for (int ch = 0; ch < 8; ch++) {
        adc_oneshot_chan_cfg_t cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };

        if (adc_oneshot_config_channel(s_adc_handle, (adc_channel_t)ch, &cfg) != ESP_OK) {
            continue;
        }

        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, (adc_channel_t)ch, &raw) != ESP_OK) {
            continue;
        }

        /* Skip channels at rail (0 or 4095) — likely not connected */
        if (raw > 50 && raw < 4000) {
            snprintf(name, sizeof(name), "adc.ch%d", ch);
            adc_add_channel(ch, name, 3);
            found++;
        }
    }

    ESP_LOGI(TAG, "Auto-detected %d active ADC channels", found);
    return found;
}

void adc_register_metrics(plexus_client_t* client) {
    if (!client) return;

#if PLEXUS_ENABLE_HEARTBEAT
    for (int i = 0; i < s_channel_count; i++) {
        if (s_channels[i].active) {
            plexus_register_metric(client, s_channels[i].metric_name);
        }
    }
#endif
}

void adc_read_all(plexus_client_t* client) {
    if (!client || !s_adc_handle) return;

    for (int i = 0; i < s_channel_count; i++) {
        if (!s_channels[i].active) continue;

        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, (adc_channel_t)s_channels[i].channel, &raw) != ESP_OK) {
            continue;
        }

        /* Try calibrated voltage conversion */
        if (s_cali_handle) {
            int voltage_mv = 0;
            if (adc_cali_raw_to_voltage(s_cali_handle, raw, &voltage_mv) == ESP_OK) {
                plexus_send_number(client, s_channels[i].metric_name,
                                   (double)voltage_mv / 1000.0);
                continue;
            }
        }

        /* Fallback: approximate voltage from raw (12-bit, 3.3V range at 11dB) */
        double voltage = (double)raw / 4095.0 * 3.3;
        plexus_send_number(client, s_channels[i].metric_name, voltage);
    }
}
