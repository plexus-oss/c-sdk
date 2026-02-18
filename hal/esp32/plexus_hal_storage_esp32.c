/**
 * @file plexus_hal_storage_esp32.c
 * @brief ESP32 NVS (Non-Volatile Storage) implementation of persistent buffer HAL
 *
 * Uses the ESP-IDF NVS library to persist unsent telemetry data across
 * power cycles and reboots.
 *
 * Requires: ESP-IDF NVS Flash component (nvs_flash)
 */

#include "plexus.h"

#if defined(ESP_PLATFORM) && PLEXUS_ENABLE_PERSISTENT_BUFFER

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "plexus_storage";
static const char* NVS_NAMESPACE = "plexus";
static bool s_nvs_initialized = false;

static plexus_err_t ensure_nvs_init(void) {
    if (s_nvs_initialized) {
        return PLEXUS_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated or version changed — erase and retry */
        ESP_LOGW(TAG, "NVS needs erase, reinitializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return PLEXUS_ERR_HAL;
    }

    s_nvs_initialized = true;
    return PLEXUS_OK;
}

plexus_err_t plexus_hal_storage_write(const char* key, const void* data, size_t len) {
    if (!key || !data) {
        return PLEXUS_ERR_NULL_PTR;
    }

    plexus_err_t init_err = ensure_nvs_init();
    if (init_err != PLEXUS_OK) {
        return init_err;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return PLEXUS_ERR_HAL;
    }

    err = nvs_set_blob(handle, key, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return PLEXUS_ERR_HAL;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        return PLEXUS_ERR_HAL;
    }

#if PLEXUS_DEBUG
    ESP_LOGI(TAG, "Wrote %zu bytes to NVS key '%s'", len, key);
#endif
    return PLEXUS_OK;
}

plexus_err_t plexus_hal_storage_read(const char* key, void* data, size_t max_len, size_t* out_len) {
    if (!key || !data || !out_len) {
        return PLEXUS_ERR_NULL_PTR;
    }

    *out_len = 0;

    plexus_err_t init_err = ensure_nvs_init();
    if (init_err != PLEXUS_OK) {
        return init_err;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        /* No namespace yet — treat as empty (not an error) */
        return PLEXUS_OK;
    }

    size_t required_size = 0;
    err = nvs_get_blob(handle, key, NULL, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        /* Key not found is not an error — just means no stored data */
        return PLEXUS_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS size query failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return PLEXUS_ERR_HAL;
    }

    if (required_size > max_len) {
        ESP_LOGW(TAG, "Stored data (%zu) exceeds buffer (%zu), truncating", required_size, max_len);
        required_size = max_len;
    }

    err = nvs_get_blob(handle, key, data, &required_size);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS read failed: %s", esp_err_to_name(err));
        return PLEXUS_ERR_HAL;
    }

    *out_len = required_size;
#if PLEXUS_DEBUG
    ESP_LOGI(TAG, "Read %zu bytes from NVS key '%s'", required_size, key);
#endif
    return PLEXUS_OK;
}

plexus_err_t plexus_hal_storage_clear(const char* key) {
    if (!key) {
        return PLEXUS_ERR_NULL_PTR;
    }

    plexus_err_t init_err = ensure_nvs_init();
    if (init_err != PLEXUS_OK) {
        return init_err;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return PLEXUS_ERR_HAL;
    }

    err = nvs_erase_key(handle, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return PLEXUS_ERR_HAL;
    }

    nvs_commit(handle);
    nvs_close(handle);

#if PLEXUS_DEBUG
    ESP_LOGI(TAG, "Cleared NVS key '%s'", key);
#endif
    return PLEXUS_OK;
}

#endif /* ESP_PLATFORM && PLEXUS_ENABLE_PERSISTENT_BUFFER */
