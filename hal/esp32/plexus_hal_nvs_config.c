/**
 * @file plexus_hal_nvs_config.c
 * @brief Read Plexus device configuration from a dedicated NVS partition
 *
 * Reads from NVS namespace "plexus_cfg" which is populated by the
 * browser-based flasher. The config partition is flashed as a separate
 * binary alongside bootloader + app.
 */

#include "plexus_hal_nvs_config.h"

#ifdef ESP_PLATFORM

#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "plexus_cfg";
static const char* NVS_NAMESPACE = "plexus_cfg";

/**
 * Read a string from NVS into a fixed-size buffer.
 * Returns true if the key exists and was read successfully.
 */
static bool read_str(nvs_handle_t handle, const char* key, char* buf, size_t buf_size) {
    size_t len = buf_size;
    esp_err_t err = nvs_get_str(handle, key, buf, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Key '%s' not found in NVS", key);
        buf[0] = '\0';
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read '%s': %s", key, esp_err_to_name(err));
        buf[0] = '\0';
        return false;
    }
    return true;
}

bool plexus_nvs_config_read(plexus_nvs_config_t* cfg) {
    if (!cfg) {
        return false;
    }

    memset(cfg, 0, sizeof(*cfg));

    /* Initialize NVS (may already be initialized — that's fine) */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, reinitializing");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return false;
    }

    /* Also init the custom partition if it exists */
    err = nvs_flash_init_partition("plexus_cfg");
    if (err != ESP_OK && err != ESP_ERR_NVS_NO_FREE_PAGES &&
        err != ESP_ERR_NOT_FOUND) {
        /* Partition may not exist (manual flash) — fall back to default NVS */
        ESP_LOGW(TAG, "plexus_cfg partition init: %s, trying default NVS", esp_err_to_name(err));
    }

    /* Try opening from the dedicated partition first, fall back to default */
    nvs_handle_t handle;
    err = nvs_open_from_partition("plexus_cfg", NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        /* Fall back to default NVS partition */
        err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "No config namespace found: %s", esp_err_to_name(err));
            return true;  /* NVS readable, but no config present */
        }
    }

    bool has_key = read_str(handle, "api_key", cfg->api_key, sizeof(cfg->api_key));
    bool has_src = read_str(handle, "source_id", cfg->source_id, sizeof(cfg->source_id));
    read_str(handle, "endpoint", cfg->endpoint, sizeof(cfg->endpoint));
    read_str(handle, "wifi_ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid));
    read_str(handle, "wifi_pass", cfg->wifi_pass, sizeof(cfg->wifi_pass));

    nvs_close(handle);

    cfg->valid = has_key && has_src;

    if (cfg->valid) {
        ESP_LOGI(TAG, "Config loaded: source_id=%s, endpoint=%s",
                 cfg->source_id,
                 cfg->endpoint[0] ? cfg->endpoint : "(default)");
    } else {
        ESP_LOGW(TAG, "Incomplete config: api_key=%s, source_id=%s",
                 has_key ? "found" : "MISSING",
                 has_src ? "found" : "MISSING");
    }

    return true;
}

#endif /* ESP_PLATFORM */
