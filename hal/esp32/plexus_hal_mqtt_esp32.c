/**
 * @file plexus_hal_mqtt_esp32.c
 * @brief ESP-IDF MQTT HAL implementation for Plexus C SDK
 *
 * Uses the esp_mqtt_client component from ESP-IDF.
 * Requires ESP-IDF v5.0+ with MQTT component enabled.
 */

#include "plexus.h"

#if defined(ESP_PLATFORM) && PLEXUS_ENABLE_MQTT

#include "mqtt_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "plexus_mqtt";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static volatile bool s_connected = false;

/* Receive buffer for commands */
#if PLEXUS_ENABLE_COMMANDS
static char s_recv_buf[PLEXUS_JSON_BUFFER_SIZE];
static size_t s_recv_len = 0;
static SemaphoreHandle_t s_recv_sem = NULL;
#endif

static void mqtt_event_handler(void* handler_args, esp_event_base_t base,
                                int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void)handler_args;
    (void)base;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            s_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            s_connected = false;
            break;
#if PLEXUS_ENABLE_COMMANDS
        case MQTT_EVENT_DATA:
            if (event->data_len > 0 && (size_t)event->data_len < sizeof(s_recv_buf)) {
                memcpy(s_recv_buf, event->data, event->data_len);
                s_recv_buf[event->data_len] = '\0';
                s_recv_len = (size_t)event->data_len;
                if (s_recv_sem) {
                    xSemaphoreGive(s_recv_sem);
                }
            }
            break;
#endif
        default:
            break;
    }
}

plexus_err_t plexus_hal_mqtt_connect(const char* broker_uri, const char* api_key,
                                      const char* source_id) {
    if (s_mqtt_client) {
        /* Already initialized, just reconnect if needed */
        if (!s_connected) {
            esp_mqtt_client_reconnect(s_mqtt_client);
            /* Wait briefly for connection */
            for (int i = 0; i < 50 && !s_connected; i++) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        return s_connected ? PLEXUS_OK : PLEXUS_ERR_TRANSPORT;
    }

#if PLEXUS_ENABLE_COMMANDS
    s_recv_sem = xSemaphoreCreateBinary();
#endif

    esp_mqtt_client_config_t cfg = {
        .broker.uri = broker_uri,
        .credentials.client_id = source_id,
        .credentials.username = api_key,
        .session.keepalive = PLEXUS_MQTT_KEEP_ALIVE_S,
    };

    s_mqtt_client = esp_mqtt_client_init(&cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return PLEXUS_ERR_HAL;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                    mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    /* Wait for connection (up to 5 seconds) */
    for (int i = 0; i < 50 && !s_connected; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return s_connected ? PLEXUS_OK : PLEXUS_ERR_TRANSPORT;
}

plexus_err_t plexus_hal_mqtt_publish(const char* topic, const char* payload,
                                      size_t payload_len, int qos) {
    if (!s_mqtt_client || !s_connected) {
        return PLEXUS_ERR_TRANSPORT;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload,
                                          (int)payload_len, qos, 0);
    if (msg_id < 0) {
        return PLEXUS_ERR_TRANSPORT;
    }

    return PLEXUS_OK;
}

bool plexus_hal_mqtt_is_connected(void) {
    return s_connected;
}

void plexus_hal_mqtt_disconnect(void) {
    if (s_mqtt_client) {
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        s_connected = false;
    }
#if PLEXUS_ENABLE_COMMANDS
    if (s_recv_sem) {
        vSemaphoreDelete(s_recv_sem);
        s_recv_sem = NULL;
    }
#endif
}

#if PLEXUS_ENABLE_COMMANDS

plexus_err_t plexus_hal_mqtt_subscribe(const char* topic, int qos) {
    if (!s_mqtt_client || !s_connected) {
        return PLEXUS_ERR_TRANSPORT;
    }

    int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic, qos);
    return (msg_id >= 0) ? PLEXUS_OK : PLEXUS_ERR_TRANSPORT;
}

plexus_err_t plexus_hal_mqtt_receive(char* buf, size_t buf_size, size_t* msg_len) {
    if (!msg_len) return PLEXUS_ERR_NULL_PTR;
    *msg_len = 0;

    if (!s_recv_sem) return PLEXUS_OK;

    /* Wait up to 100ms for a message */
    if (xSemaphoreTake(s_recv_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
        size_t copy_len = s_recv_len < buf_size - 1 ? s_recv_len : buf_size - 1;
        memcpy(buf, s_recv_buf, copy_len);
        buf[copy_len] = '\0';
        *msg_len = copy_len;
        s_recv_len = 0;
    }

    return PLEXUS_OK;
}

#endif /* PLEXUS_ENABLE_COMMANDS */

#endif /* ESP_PLATFORM && PLEXUS_ENABLE_MQTT */
