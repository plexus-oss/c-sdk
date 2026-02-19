/**
 * @file plexus_hal_esp32.c
 * @brief ESP-IDF HAL implementation for Plexus C SDK
 *
 * Requires ESP-IDF v5.0+ with esp_http_client component
 */

#include "plexus.h"

#ifdef ESP_PLATFORM  /* Only compile for ESP-IDF */

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>
#include <stdarg.h>
#include <string.h>

static const char* TAG = "plexus";

plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* user_agent,
                                   const char* body, size_t body_len) {
    if (!url || !api_key || !body) {
        return PLEXUS_ERR_NULL_PTR;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = PLEXUS_HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
        .buffer_size = 512,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return PLEXUS_ERR_HAL;
    }

    /* Set headers */
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "x-api-key", api_key);
    if (user_agent) {
        esp_http_client_set_header(client, "User-Agent", user_agent);
    }

    /* Set POST data */
    esp_http_client_set_post_field(client, body, (int)body_len);

    /* Perform request */
    esp_err_t err = esp_http_client_perform(client);
    plexus_err_t result;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        result = PLEXUS_ERR_NETWORK;
    } else {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGD(TAG, "HTTP status: %d", status);

        if (status >= 200 && status < 300) {
            result = PLEXUS_OK;
        } else if (status == 401) {
            result = PLEXUS_ERR_AUTH;
        } else if (status == 429) {
            result = PLEXUS_ERR_RATE_LIMIT;
        } else if (status >= 500) {
            result = PLEXUS_ERR_SERVER;
        } else {
            result = PLEXUS_ERR_NETWORK;
        }
    }

    esp_http_client_cleanup(client);
    return result;
}

/* HTTP response handler - collects body into user buffer */
typedef struct {
    char* buf;
    size_t buf_size;
    size_t pos;
    bool truncated;
} http_get_ctx_t;

static esp_err_t http_get_event_handler(esp_http_client_event_t *evt) {
    http_get_ctx_t* ctx = (http_get_ctx_t*)evt->user_data;

    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (ctx->truncated) {
                break; /* Already overflowed, discard rest */
            }
            if (ctx->pos + evt->data_len < ctx->buf_size) {
                memcpy(ctx->buf + ctx->pos, evt->data, evt->data_len);
                ctx->pos += evt->data_len;
                ctx->buf[ctx->pos] = '\0';
            } else {
                /* Copy what fits, mark as truncated */
                size_t remaining = ctx->buf_size - 1 - ctx->pos;
                if (remaining > 0) {
                    memcpy(ctx->buf + ctx->pos, evt->data, remaining);
                    ctx->pos += remaining;
                    ctx->buf[ctx->pos] = '\0';
                }
                ctx->truncated = true;
                ESP_LOGW("plexus", "HTTP GET response truncated (buffer full)");
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

#if PLEXUS_ENABLE_AUTO_REGISTER

plexus_err_t plexus_hal_http_post_response(
    const char* url, const char* api_key, const char* user_agent,
    const char* body, size_t body_len,
    char* response_buf, size_t response_buf_size, size_t* response_len) {
    if (!url || !api_key || !body || !response_buf || !response_len) {
        return PLEXUS_ERR_NULL_PTR;
    }

    *response_len = 0;

    http_get_ctx_t ctx = {
        .buf = response_buf,
        .buf_size = response_buf_size,
        .pos = 0,
        .truncated = false,
    };

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = PLEXUS_HTTP_TIMEOUT_MS,
        .event_handler = http_get_event_handler,
        .user_data = &ctx,
        .keep_alive_enable = true,
        .buffer_size = 512,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return PLEXUS_ERR_HAL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "x-api-key", api_key);
    if (user_agent) {
        esp_http_client_set_header(client, "User-Agent", user_agent);
    }
    esp_http_client_set_post_field(client, body, (int)body_len);

    esp_err_t err = esp_http_client_perform(client);
    plexus_err_t result;

    if (err != ESP_OK) {
        result = PLEXUS_ERR_NETWORK;
    } else {
        int status = esp_http_client_get_status_code(client);
        if (status >= 200 && status < 300) {
            result = PLEXUS_OK;
            *response_len = ctx.pos;
        } else if (status == 401) {
            result = PLEXUS_ERR_AUTH;
        } else if (status == 429) {
            result = PLEXUS_ERR_RATE_LIMIT;
        } else if (status >= 500) {
            result = PLEXUS_ERR_SERVER;
        } else {
            result = PLEXUS_ERR_NETWORK;
        }
    }

    esp_http_client_cleanup(client);
    return result;
}

#endif /* PLEXUS_ENABLE_AUTO_REGISTER */

#if PLEXUS_ENABLE_COMMANDS

plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
                                  const char* user_agent,
                                  char* response_buf, size_t buf_size,
                                  size_t* response_len) {
    if (!url || !api_key || !response_buf || !response_len) {
        return PLEXUS_ERR_NULL_PTR;
    }

    *response_len = 0;

    http_get_ctx_t ctx = {
        .buf = response_buf,
        .buf_size = buf_size,
        .pos = 0,
        .truncated = false,
    };

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = PLEXUS_HTTP_TIMEOUT_MS,
        .event_handler = http_get_event_handler,
        .user_data = &ctx,
        .keep_alive_enable = true,
        .buffer_size = 512,
        .buffer_size_tx = 256,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return PLEXUS_ERR_HAL;
    }

    esp_http_client_set_header(client, "x-api-key", api_key);
    if (user_agent) {
        esp_http_client_set_header(client, "User-Agent", user_agent);
    }

    esp_err_t err = esp_http_client_perform(client);
    plexus_err_t result;

    if (err != ESP_OK) {
        result = PLEXUS_ERR_NETWORK;
    } else {
        int status = esp_http_client_get_status_code(client);
        if (status >= 200 && status < 300) {
            result = PLEXUS_OK;
            *response_len = ctx.pos;
        } else if (status == 401) {
            result = PLEXUS_ERR_AUTH;
        } else if (status == 429) {
            result = PLEXUS_ERR_RATE_LIMIT;
        } else if (status >= 500) {
            result = PLEXUS_ERR_SERVER;
        } else {
            result = PLEXUS_ERR_NETWORK;
        }
    }

    esp_http_client_cleanup(client);
    return result;
}

#endif /* PLEXUS_ENABLE_COMMANDS */

uint64_t plexus_hal_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

uint32_t plexus_hal_get_tick_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void plexus_hal_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void plexus_hal_log(const char* fmt, ...) {
#if PLEXUS_DEBUG
    va_list args;
    va_start(args, fmt);
    esp_log_writev(ESP_LOG_INFO, TAG, fmt, args);
    va_end(args);
#else
    (void)fmt;
#endif
}

/* Optional: Initialize SNTP for accurate timestamps */
void plexus_hal_init_time(const char* ntp_server) {
    if (ntp_server == NULL) {
        ntp_server = "pool.ntp.org";
    }

    ESP_LOGI(TAG, "Initializing SNTP with server: %s", ntp_server);

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_init();

    /* Wait for time sync (with timeout) */
    int retry = 0;
    const int retry_max = 15;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_max) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_max);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (retry >= retry_max) {
        ESP_LOGW(TAG, "Time sync failed, timestamps may be inaccurate");
    } else {
        ESP_LOGI(TAG, "Time synchronized");
    }
}

/* ========================================================================= */
/* Thread safety: FreeRTOS recursive mutex                                   */
/* ========================================================================= */

#if PLEXUS_ENABLE_THREAD_SAFE

#include "freertos/semphr.h"

void* plexus_hal_mutex_create(void) {
    return (void*)xSemaphoreCreateRecursiveMutex();
}

void plexus_hal_mutex_lock(void* mutex) {
    if (mutex) {
        xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex, portMAX_DELAY);
    }
}

void plexus_hal_mutex_unlock(void* mutex) {
    if (mutex) {
        xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex);
    }
}

void plexus_hal_mutex_destroy(void* mutex) {
    if (mutex) {
        vSemaphoreDelete((SemaphoreHandle_t)mutex);
    }
}

#endif /* PLEXUS_ENABLE_THREAD_SAFE */

#endif /* ESP_PLATFORM */
