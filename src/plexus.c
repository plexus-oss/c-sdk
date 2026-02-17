/**
 * @file plexus.c
 * @brief Plexus C SDK core implementation
 */

#include "plexus.h"
#include <string.h>
#include <stdlib.h>

#define PLEXUS_VERSION "1.0.0"

/* Forward declaration for JSON serializer */
int plexus_json_serialize(const plexus_client_t* client, char* buf, size_t buf_size);

/* JSON buffer to minimize stack usage (shared with plexus_commands.c) */
char s_json_buffer[PLEXUS_JSON_BUFFER_SIZE];

/* ------------------------------------------------------------------------- */
/* Error messages                                                            */
/* ------------------------------------------------------------------------- */

static const char* s_error_messages[] = {
    "Success",
    "Null pointer",
    "Buffer full",
    "String too long",
    "No data to flush",
    "Network error",
    "Authentication failed",
    "Rate limited",
    "Server error",
    "JSON serialization error",
    "Client not initialized",
    "HAL error",
};

const char* plexus_strerror(plexus_err_t err) {
    if (err < 0 || err > PLEXUS_ERR_HAL) {
        return "Unknown error";
    }
    return s_error_messages[err];
}

const char* plexus_version(void) {
    return PLEXUS_VERSION;
}

/* ------------------------------------------------------------------------- */
/* Client lifecycle                                                          */
/* ------------------------------------------------------------------------- */

plexus_client_t* plexus_init(const char* api_key, const char* source_id) {
    if (!api_key || !source_id) {
        return NULL;
    }

    if (strlen(api_key) >= PLEXUS_MAX_API_KEY_LEN ||
        strlen(source_id) >= PLEXUS_MAX_SOURCE_ID_LEN) {
        return NULL;
    }

    plexus_client_t* client = (plexus_client_t*)malloc(sizeof(plexus_client_t));
    if (!client) {
        return NULL;
    }

    memset(client, 0, sizeof(plexus_client_t));

    strncpy(client->api_key, api_key, PLEXUS_MAX_API_KEY_LEN - 1);
    strncpy(client->source_id, source_id, PLEXUS_MAX_SOURCE_ID_LEN - 1);
    strncpy(client->endpoint, PLEXUS_DEFAULT_ENDPOINT, sizeof(client->endpoint) - 1);

    client->metric_count = 0;
    client->last_flush_ms = plexus_hal_get_tick_ms();  /* Initialize to current tick */
    client->total_sent = 0;
    client->total_errors = 0;
    client->initialized = true;

#if PLEXUS_ENABLE_COMMANDS
    client->command_handler = NULL;
    client->last_command_poll_ms = plexus_hal_get_tick_ms();
#endif

#if PLEXUS_DEBUG
    plexus_hal_log("Plexus SDK v%s initialized (source: %s)", PLEXUS_VERSION, source_id);
#endif

    return client;
}

void plexus_free(plexus_client_t* client) {
    if (client) {
        /* Attempt to flush remaining metrics */
        if (client->metric_count > 0) {
            plexus_flush(client);
        }
        client->initialized = false;
        free(client);
    }
}

plexus_err_t plexus_set_endpoint(plexus_client_t* client, const char* endpoint) {
    if (!client || !endpoint) {
        return PLEXUS_ERR_NULL_PTR;
    }
    if (!client->initialized) {
        return PLEXUS_ERR_NOT_INITIALIZED;
    }
    if (strlen(endpoint) >= sizeof(client->endpoint)) {
        return PLEXUS_ERR_STRING_TOO_LONG;
    }

    strncpy(client->endpoint, endpoint, sizeof(client->endpoint) - 1);
    return PLEXUS_OK;
}

/* ------------------------------------------------------------------------- */
/* Send metrics                                                              */
/* ------------------------------------------------------------------------- */

static plexus_err_t add_metric(plexus_client_t* client, const char* metric,
                                plexus_value_t* value, uint64_t timestamp_ms) {
    if (!client || !metric || !value) {
        return PLEXUS_ERR_NULL_PTR;
    }
    if (!client->initialized) {
        return PLEXUS_ERR_NOT_INITIALIZED;
    }
    if (strlen(metric) >= PLEXUS_MAX_METRIC_NAME_LEN) {
        return PLEXUS_ERR_STRING_TOO_LONG;
    }
    if (client->metric_count >= PLEXUS_MAX_METRICS) {
        return PLEXUS_ERR_BUFFER_FULL;
    }

    plexus_metric_t* m = &client->metrics[client->metric_count];
    memset(m, 0, sizeof(plexus_metric_t));

    strncpy(m->name, metric, PLEXUS_MAX_METRIC_NAME_LEN - 1);
    memcpy(&m->value, value, sizeof(plexus_value_t));

    if (timestamp_ms > 0) {
        m->timestamp_ms = timestamp_ms;
    } else {
        m->timestamp_ms = plexus_hal_get_time_ms();
    }

    client->metric_count++;

#if PLEXUS_DEBUG
    plexus_hal_log("Queued metric: %s (total: %d)", metric, client->metric_count);
#endif

    /* Auto-flush if buffer reaches threshold */
#if PLEXUS_AUTO_FLUSH_COUNT > 0
    if (client->metric_count >= PLEXUS_AUTO_FLUSH_COUNT) {
        return plexus_flush(client);
    }
#endif

    /* Auto-flush if time interval has elapsed */
#if PLEXUS_AUTO_FLUSH_INTERVAL_MS > 0
    {
        uint32_t now_ms = plexus_hal_get_tick_ms();
        uint32_t elapsed = now_ms - client->last_flush_ms;
        /* Handle tick overflow (now_ms < last_flush_ms) */
        if (elapsed >= PLEXUS_AUTO_FLUSH_INTERVAL_MS || now_ms < client->last_flush_ms) {
            return plexus_flush(client);
        }
    }
#endif

    return PLEXUS_OK;
}

plexus_err_t plexus_send_number(plexus_client_t* client, const char* metric, double value) {
    plexus_value_t v = { .type = PLEXUS_VALUE_NUMBER, .data.number = value };
    return add_metric(client, metric, &v, 0);
}

plexus_err_t plexus_send_number_ts(plexus_client_t* client, const char* metric,
                                    double value, uint64_t timestamp_ms) {
    plexus_value_t v = { .type = PLEXUS_VALUE_NUMBER, .data.number = value };
    return add_metric(client, metric, &v, timestamp_ms);
}

#if PLEXUS_ENABLE_STRING_VALUES
plexus_err_t plexus_send_string(plexus_client_t* client, const char* metric, const char* value) {
    if (!value) {
        return PLEXUS_ERR_NULL_PTR;
    }
    if (strlen(value) >= PLEXUS_MAX_STRING_VALUE_LEN) {
        return PLEXUS_ERR_STRING_TOO_LONG;
    }

    plexus_value_t v = { .type = PLEXUS_VALUE_STRING };
    strncpy(v.data.string, value, PLEXUS_MAX_STRING_VALUE_LEN - 1);
    return add_metric(client, metric, &v, 0);
}
#endif

#if PLEXUS_ENABLE_BOOL_VALUES
plexus_err_t plexus_send_bool(plexus_client_t* client, const char* metric, bool value) {
    plexus_value_t v = { .type = PLEXUS_VALUE_BOOL, .data.boolean = value };
    return add_metric(client, metric, &v, 0);
}
#endif

#if PLEXUS_ENABLE_TAGS
plexus_err_t plexus_send_number_tagged(plexus_client_t* client, const char* metric,
                                        double value, const char** tag_keys,
                                        const char** tag_values, uint8_t tag_count) {
    if (!client || !metric) {
        return PLEXUS_ERR_NULL_PTR;
    }
    if (!client->initialized) {
        return PLEXUS_ERR_NOT_INITIALIZED;
    }
    if (client->metric_count >= PLEXUS_MAX_METRICS) {
        return PLEXUS_ERR_BUFFER_FULL;
    }
    if (tag_count > 4) {
        tag_count = 4;  /* Limit to 4 tags */
    }

    plexus_value_t v = { .type = PLEXUS_VALUE_NUMBER, .data.number = value };
    plexus_err_t err = add_metric(client, metric, &v, 0);
    if (err != PLEXUS_OK) {
        return err;
    }

    /* Add tags to the just-added metric */
    plexus_metric_t* m = &client->metrics[client->metric_count - 1];
    m->tag_count = tag_count;

    for (uint8_t i = 0; i < tag_count && tag_keys && tag_values; i++) {
        if (tag_keys[i] && tag_values[i]) {
            strncpy(m->tag_keys[i], tag_keys[i], 31);
            strncpy(m->tag_values[i], tag_values[i], 31);
        }
    }

    return PLEXUS_OK;
}
#endif

/* ------------------------------------------------------------------------- */
/* Flush & network                                                           */
/* ------------------------------------------------------------------------- */

plexus_err_t plexus_flush(plexus_client_t* client) {
    if (!client) {
        return PLEXUS_ERR_NULL_PTR;
    }
    if (!client->initialized) {
        return PLEXUS_ERR_NOT_INITIALIZED;
    }

#if PLEXUS_ENABLE_PERSISTENT_BUFFER
    /* Attempt to send previously persisted data first */
    {
        size_t stored_len = 0;
        plexus_err_t restore_err = plexus_hal_storage_read(
            "plexus_buf", s_json_buffer, PLEXUS_JSON_BUFFER_SIZE, &stored_len);
        if (restore_err == PLEXUS_OK && stored_len > 0) {
#if PLEXUS_DEBUG
            plexus_hal_log("Restoring %zu bytes from persistent buffer", stored_len);
#endif
            plexus_err_t send_err = plexus_hal_http_post(
                client->endpoint, client->api_key, s_json_buffer, stored_len);
            if (send_err == PLEXUS_OK) {
                plexus_hal_storage_clear("plexus_buf");
            }
            /* If send failed, leave it in storage for next attempt */
        }
    }
#endif /* PLEXUS_ENABLE_PERSISTENT_BUFFER */

    if (client->metric_count == 0) {
        return PLEXUS_ERR_NO_DATA;
    }

    /* Serialize to JSON */
    int json_len = plexus_json_serialize(client, s_json_buffer, PLEXUS_JSON_BUFFER_SIZE);
    if (json_len < 0) {
        client->total_errors++;
        return PLEXUS_ERR_JSON;
    }

#if PLEXUS_DEBUG
    plexus_hal_log("Sending %d metrics (%d bytes)", client->metric_count, json_len);
#endif

    /* Send with retries */
    plexus_err_t err = PLEXUS_ERR_NETWORK;
    for (int retry = 0; retry < PLEXUS_MAX_RETRIES; retry++) {
        err = plexus_hal_http_post(client->endpoint, client->api_key,
                                    s_json_buffer, (size_t)json_len);

        if (err == PLEXUS_OK) {
            /* Success - clear buffer */
            client->total_sent += client->metric_count;
            client->metric_count = 0;
            client->last_flush_ms = plexus_hal_get_tick_ms();
            return PLEXUS_OK;
        }

        /* Don't retry on auth or rate limit errors */
        if (err == PLEXUS_ERR_AUTH || err == PLEXUS_ERR_RATE_LIMIT) {
            break;
        }

#if PLEXUS_DEBUG
        plexus_hal_log("Retry %d/%d after error: %s", retry + 1, PLEXUS_MAX_RETRIES,
                       plexus_strerror(err));
#endif

        /* Wait before retry (HAL should provide delay, or we just continue) */
    }

#if PLEXUS_ENABLE_PERSISTENT_BUFFER
    /* All retries failed — persist to flash so data survives reboot */
    plexus_hal_storage_write("plexus_buf", s_json_buffer, (size_t)json_len);
#if PLEXUS_DEBUG
    plexus_hal_log("Persisted %d bytes to flash buffer", json_len);
#endif
#endif /* PLEXUS_ENABLE_PERSISTENT_BUFFER */

    client->total_errors++;
    return err;
}

uint16_t plexus_pending_count(const plexus_client_t* client) {
    if (!client || !client->initialized) {
        return 0;
    }
    return client->metric_count;
}

void plexus_clear(plexus_client_t* client) {
    if (client && client->initialized) {
        client->metric_count = 0;
    }
}

plexus_err_t plexus_tick(plexus_client_t* client) {
    if (!client) {
        return PLEXUS_ERR_NULL_PTR;
    }
    if (!client->initialized) {
        return PLEXUS_ERR_NOT_INITIALIZED;
    }

#if PLEXUS_ENABLE_COMMANDS
    /* Check if command polling interval has elapsed */
    if (client->command_handler) {
        uint32_t now_ms = plexus_hal_get_tick_ms();
        uint32_t cmd_elapsed = now_ms - client->last_command_poll_ms;

        if (cmd_elapsed >= PLEXUS_COMMAND_POLL_INTERVAL_MS || now_ms < client->last_command_poll_ms) {
            client->last_command_poll_ms = now_ms;
            plexus_poll_commands(client);
        }
    }
#endif

    /* Check if we have data and time interval has elapsed */
    if (client->metric_count == 0) {
        return PLEXUS_ERR_NO_DATA;
    }

#if PLEXUS_AUTO_FLUSH_INTERVAL_MS > 0
    {
        uint32_t now_ms = plexus_hal_get_tick_ms();
        uint32_t elapsed = now_ms - client->last_flush_ms;

        /* Flush if interval elapsed or tick counter wrapped */
        if (elapsed >= PLEXUS_AUTO_FLUSH_INTERVAL_MS || now_ms < client->last_flush_ms) {
            return plexus_flush(client);
        }
    }
#endif

    return PLEXUS_OK;
}
