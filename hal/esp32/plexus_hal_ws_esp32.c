/**
 * @file plexus_hal_ws_esp32.c
 * @brief ESP-IDF WebSocket HAL implementation for Plexus C SDK
 *
 * Uses the esp_websocket_client component (included in ESP-IDF v5.0+).
 * Bridges the async ESP-IDF event model to the SDK's callback interface.
 */

#include "plexus.h"

#if defined(ESP_PLATFORM) && PLEXUS_ENABLE_WEBSOCKET

#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_event.h"
#include <string.h>

static const char* TAG = "plexus_ws";

/* Bridge struct — maps ESP event handler to SDK callback */
typedef struct {
    esp_websocket_client_handle_t client;
    plexus_ws_event_cb_t callback;
    void* user_data;
    bool connected;
} ws_bridge_t;

/* Single bridge instance (one WS client at a time) */
static ws_bridge_t s_bridge = {0};

/* ========================================================================= */
/* ESP-IDF event handler → SDK callback                                      */
/* ========================================================================= */

static void ws_event_handler(void* handler_args, esp_event_base_t base,
                              int32_t event_id, void* event_data) {
    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;
    (void)handler_args;
    (void)base;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            s_bridge.connected = true;
            if (s_bridge.callback) {
                s_bridge.callback(PLEXUS_WS_EVENT_CONNECTED, NULL, 0, s_bridge.user_data);
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected");
            s_bridge.connected = false;
            if (s_bridge.callback) {
                s_bridge.callback(PLEXUS_WS_EVENT_DISCONNECTED, NULL, 0, s_bridge.user_data);
            }
            break;

        case WEBSOCKET_EVENT_DATA:
            /* Only handle text frames with data */
            if (data->op_code == 0x01 && data->data_len > 0 && data->data_ptr) {
                if (s_bridge.callback) {
                    s_bridge.callback(PLEXUS_WS_EVENT_DATA,
                                      data->data_ptr, (size_t)data->data_len,
                                      s_bridge.user_data);
                }
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGW(TAG, "WebSocket error");
            if (s_bridge.callback) {
                s_bridge.callback(PLEXUS_WS_EVENT_ERROR, NULL, 0, s_bridge.user_data);
            }
            break;

        default:
            break;
    }
}

/* ========================================================================= */
/* HAL interface implementation                                              */
/* ========================================================================= */

void* plexus_hal_ws_connect(const char* url, plexus_ws_event_cb_t callback,
                             void* user_data) {
    if (!url || !callback) {
        return NULL;
    }

    /* Close existing connection if any */
    if (s_bridge.client) {
        esp_websocket_client_stop(s_bridge.client);
        esp_websocket_client_destroy(s_bridge.client);
        s_bridge.client = NULL;
    }

    s_bridge.callback = callback;
    s_bridge.user_data = user_data;
    s_bridge.connected = false;

    esp_websocket_client_config_t config = {
        .uri = url,
        .buffer_size = 2048,
        .reconnect_timeout_ms = 0,      /* SDK handles reconnect, not ESP-IDF */
        .network_timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init WebSocket client");
        return NULL;
    }

    /* Register event handler */
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                   ws_event_handler, NULL);

    /* Start connection (async — event handler will fire CONNECTED) */
    esp_err_t err = esp_websocket_client_start(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(client);
        return NULL;
    }

    s_bridge.client = client;
    ESP_LOGI(TAG, "Connecting to %s", url);
    return (void*)client;
}

plexus_err_t plexus_hal_ws_send(void* ws_handle, const char* data, size_t data_len) {
    if (!ws_handle || !data) {
        return PLEXUS_ERR_NULL_PTR;
    }

    esp_websocket_client_handle_t client = (esp_websocket_client_handle_t)ws_handle;

    if (!esp_websocket_client_is_connected(client)) {
        return PLEXUS_ERR_WS_NOT_CONNECTED;
    }

    int sent = esp_websocket_client_send_text(client, data, (int)data_len,
                                                pdMS_TO_TICKS(5000));
    if (sent < 0) {
        ESP_LOGW(TAG, "WebSocket send failed");
        return PLEXUS_ERR_NETWORK;
    }

    return PLEXUS_OK;
}

void plexus_hal_ws_close(void* ws_handle) {
    if (!ws_handle) return;

    esp_websocket_client_handle_t client = (esp_websocket_client_handle_t)ws_handle;
    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);

    if (s_bridge.client == client) {
        s_bridge.client = NULL;
        s_bridge.connected = false;
    }
}

bool plexus_hal_ws_is_connected(void* ws_handle) {
    if (!ws_handle) return false;
    return esp_websocket_client_is_connected((esp_websocket_client_handle_t)ws_handle);
}

#endif /* ESP_PLATFORM && PLEXUS_ENABLE_WEBSOCKET */
