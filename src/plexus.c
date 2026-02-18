/**
 * @file plexus.c
 * @brief Plexus C SDK core implementation
 */

#include "plexus_internal.h"
#include <string.h>
#include <stdlib.h>

#define PLEXUS_VERSION "0.1.1"

#define PLEXUS_USER_AGENT "plexus-c-sdk/" PLEXUS_VERSION

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
    "Invalid argument",
};

const char* plexus_strerror(plexus_err_t err) {
    if ((unsigned int)err >= PLEXUS_ERR__COUNT) {
        return "Unknown error";
    }
    return s_error_messages[err];
}

const char* plexus_version(void) {
    return PLEXUS_VERSION;
}

/* ------------------------------------------------------------------------- */
/* Source ID validation                                                      */
/* ------------------------------------------------------------------------- */

/**
 * Validate that source_id contains only URL-safe characters: [a-zA-Z0-9._-]
 * This prevents URL injection in command poll URLs and ensures clean payloads.
 */
static bool is_valid_source_id(const char* s) {
    if (!s || s[0] == '\0') {
        return false;
    }
    for (; *s; s++) {
        char c = *s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            continue;
        }
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* Client lifecycle                                                          */
/* ------------------------------------------------------------------------- */

size_t plexus_client_size(void) {
    return sizeof(plexus_client_t);
}

/**
 * Common initialization logic shared by plexus_init and plexus_init_static
 */
static plexus_client_t* client_init_common(plexus_client_t* client,
                                             const char* api_key,
                                             const char* source_id) {
    memset(client, 0, sizeof(plexus_client_t));

    strncpy(client->api_key, api_key, PLEXUS_MAX_API_KEY_LEN - 1);
    strncpy(client->source_id, source_id, PLEXUS_MAX_SOURCE_ID_LEN - 1);
    strncpy(client->endpoint, PLEXUS_DEFAULT_ENDPOINT, sizeof(client->endpoint) - 1);

    client->metric_count = 0;
    client->last_flush_ms = plexus_hal_get_tick_ms();
    client->total_sent = 0;
    client->total_errors = 0;
    client->retry_backoff_ms = 0;
    client->rate_limit_until_ms = 0;
    client->initialized = true;

#if PLEXUS_ENABLE_COMMANDS
    client->command_handler = NULL;
    client->last_command_poll_ms = plexus_hal_get_tick_ms();
#endif

#if PLEXUS_DEBUG
    plexus_hal_log("Plexus SDK v%s initialized (source: %s, client size: %u bytes)",
                   PLEXUS_VERSION, source_id, (unsigned)sizeof(plexus_client_t));
#endif

    return client;
}

plexus_client_t* plexus_init(const char* api_key, const char* source_id) {
    if (!api_key || !source_id) {
        return NULL;
    }
    if (!is_valid_source_id(source_id)) {
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

    return client_init_common(client, api_key, source_id);
}

plexus_client_t* plexus_init_static(void* buf, size_t buf_size,
                                      const char* api_key, const char* source_id) {
    if (!buf || !api_key || !source_id) {
        return NULL;
    }
    if (buf_size < sizeof(plexus_client_t)) {
        return NULL;
    }
    if (!is_valid_source_id(source_id)) {
        return NULL;
    }
    if (strlen(api_key) >= PLEXUS_MAX_API_KEY_LEN ||
        strlen(source_id) >= PLEXUS_MAX_SOURCE_ID_LEN) {
        return NULL;
    }

    return client_init_common((plexus_client_t*)buf, api_key, source_id);
}

void plexus_free(plexus_client_t* client) {
    if (client) {
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

plexus_err_t plexus_set_flush_interval(plexus_client_t* client, uint32_t interval_ms) {
    if (!client) {
        return PLEXUS_ERR_NULL_PTR;
    }
    if (!client->initialized) {
        return PLEXUS_ERR_NOT_INITIALIZED;
    }
    client->flush_interval_ms = interval_ms;
    return PLEXUS_OK;
}

plexus_err_t plexus_set_flush_count(plexus_client_t* client, uint16_t count) {
    if (!client) {
        return PLEXUS_ERR_NULL_PTR;
    }
    if (!client->initialized) {
        return PLEXUS_ERR_NOT_INITIALIZED;
    }
    client->auto_flush_count = count;
    return PLEXUS_OK;
}

/* ------------------------------------------------------------------------- */
/* Send metrics                                                              */
/* ------------------------------------------------------------------------- */

/**
 * Queue a metric. Auto-flush only triggers a non-blocking flush (no retries)
 * so that send functions never block for seconds.
 */
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

    /* Count-based auto-flush: single attempt, no retries */
    {
        uint16_t flush_count = client->auto_flush_count > 0
            ? client->auto_flush_count : PLEXUS_AUTO_FLUSH_COUNT;
        if (flush_count > 0 && client->metric_count >= flush_count) {
            return plexus_flush(client);
        }
    }

    return PLEXUS_OK;
}

plexus_err_t plexus_send_number(plexus_client_t* client, const char* metric, double value) {
    plexus_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = PLEXUS_VALUE_NUMBER;
    v.data.number = value;
    return add_metric(client, metric, &v, 0);
}

plexus_err_t plexus_send_number_ts(plexus_client_t* client, const char* metric,
                                    double value, uint64_t timestamp_ms) {
    plexus_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = PLEXUS_VALUE_NUMBER;
    v.data.number = value;
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

    plexus_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = PLEXUS_VALUE_STRING;
    strncpy(v.data.string, value, PLEXUS_MAX_STRING_VALUE_LEN - 1);
    return add_metric(client, metric, &v, 0);
}
#endif

#if PLEXUS_ENABLE_BOOL_VALUES
plexus_err_t plexus_send_bool(plexus_client_t* client, const char* metric, bool value) {
    plexus_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = PLEXUS_VALUE_BOOL;
    v.data.boolean = value;
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
    if (strlen(metric) >= PLEXUS_MAX_METRIC_NAME_LEN) {
        return PLEXUS_ERR_STRING_TOO_LONG;
    }
    if (client->metric_count >= PLEXUS_MAX_METRICS) {
        return PLEXUS_ERR_BUFFER_FULL;
    }
    if (tag_count > 4) {
        tag_count = 4;
    }

    plexus_metric_t* m = &client->metrics[client->metric_count];
    memset(m, 0, sizeof(plexus_metric_t));

    strncpy(m->name, metric, PLEXUS_MAX_METRIC_NAME_LEN - 1);
    m->value.type = PLEXUS_VALUE_NUMBER;
    m->value.data.number = value;
    m->timestamp_ms = plexus_hal_get_time_ms();

    m->tag_count = tag_count;
    for (uint8_t i = 0; i < tag_count && tag_keys && tag_values; i++) {
        if (tag_keys[i] && tag_values[i]) {
            strncpy(m->tag_keys[i], tag_keys[i], 31);
            m->tag_keys[i][31] = '\0';
            strncpy(m->tag_values[i], tag_values[i], 31);
            m->tag_values[i][31] = '\0';
        }
    }

    client->metric_count++;

#if PLEXUS_DEBUG
    plexus_hal_log("Queued metric: %s (total: %d, tags: %d)",
                   metric, client->metric_count, tag_count);
#endif

    /* Count-based auto-flush */
    {
        uint16_t flush_count = client->auto_flush_count > 0
            ? client->auto_flush_count : PLEXUS_AUTO_FLUSH_COUNT;
        if (flush_count > 0 && client->metric_count >= flush_count) {
            return plexus_flush(client);
        }
    }

    return PLEXUS_OK;
}
#endif

/* ------------------------------------------------------------------------- */
/* Backoff helpers                                                           */
/* ------------------------------------------------------------------------- */

/**
 * Simple xorshift32 PRNG for jitter — no need for a full rand() dependency.
 */
static uint32_t backoff_rand(uint32_t seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

/**
 * Compute next backoff delay with exponential growth and jitter.
 * Returns the delay in ms and updates the client's backoff state.
 */
static uint32_t compute_backoff(plexus_client_t* client) {
    if (client->retry_backoff_ms == 0) {
        client->retry_backoff_ms = PLEXUS_RETRY_BASE_MS;
    } else {
        client->retry_backoff_ms *= 2;
        if (client->retry_backoff_ms > PLEXUS_RETRY_MAX_MS) {
            client->retry_backoff_ms = PLEXUS_RETRY_MAX_MS;
        }
    }

    /* Add ±25% jitter */
    uint32_t jitter_range = client->retry_backoff_ms / 4;
    if (jitter_range > 0) {
        uint32_t seed = plexus_hal_get_tick_ms() ^ client->retry_backoff_ms;
        uint32_t jitter = backoff_rand(seed) % (jitter_range * 2);
        return client->retry_backoff_ms - jitter_range + jitter;
    }

    return client->retry_backoff_ms;
}

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

    /* Respect rate limit cooldown */
    if (client->rate_limit_until_ms > 0) {
        uint32_t now = plexus_hal_get_tick_ms();
        if (now < client->rate_limit_until_ms &&
            (client->rate_limit_until_ms - now) < PLEXUS_RATE_LIMIT_COOLDOWN_MS * 2) {
            /* Still in cooldown period */
            return PLEXUS_ERR_RATE_LIMIT;
        }
        /* Cooldown expired or tick wrapped */
        client->rate_limit_until_ms = 0;
    }

#if PLEXUS_ENABLE_PERSISTENT_BUFFER
    /* Attempt to send previously persisted data first */
    {
        size_t stored_len = 0;
        plexus_err_t restore_err = plexus_hal_storage_read(
            "plexus_buf", client->json_buffer, PLEXUS_JSON_BUFFER_SIZE, &stored_len);
        if (restore_err == PLEXUS_OK && stored_len > 0) {
#if PLEXUS_DEBUG
            plexus_hal_log("Restoring %zu bytes from persistent buffer", stored_len);
#endif
            plexus_err_t send_err = plexus_hal_http_post(
                client->endpoint, client->api_key, PLEXUS_USER_AGENT,
                client->json_buffer, stored_len);
            if (send_err == PLEXUS_OK) {
                plexus_hal_storage_clear("plexus_buf");
            }
        }
    }
#endif

    if (client->metric_count == 0) {
        return PLEXUS_ERR_NO_DATA;
    }

    /* Serialize to JSON */
    int json_len = plexus_json_serialize(client, client->json_buffer, PLEXUS_JSON_BUFFER_SIZE);
    if (json_len < 0) {
        client->total_errors++;
        return PLEXUS_ERR_JSON;
    }

#if PLEXUS_DEBUG
    plexus_hal_log("Sending %d metrics (%d bytes)", client->metric_count, json_len);
#endif

    /* Send with retries and exponential backoff */
    plexus_err_t err = PLEXUS_ERR_NETWORK;
    client->retry_backoff_ms = 0; /* Reset backoff for this flush attempt */

    for (int retry = 0; retry < PLEXUS_MAX_RETRIES; retry++) {
        if (retry > 0) {
            uint32_t delay = compute_backoff(client);
            plexus_hal_delay_ms(delay);
        }

        err = plexus_hal_http_post(client->endpoint, client->api_key,
                                    PLEXUS_USER_AGENT,
                                    client->json_buffer, (size_t)json_len);

        if (err == PLEXUS_OK) {
            client->total_sent += client->metric_count;
            client->metric_count = 0;
            client->last_flush_ms = plexus_hal_get_tick_ms();
            client->retry_backoff_ms = 0;
            return PLEXUS_OK;
        }

        /* Don't retry on auth errors */
        if (err == PLEXUS_ERR_AUTH) {
            break;
        }

        /* On rate limit, enter cooldown and stop immediately */
        if (err == PLEXUS_ERR_RATE_LIMIT) {
            client->rate_limit_until_ms =
                plexus_hal_get_tick_ms() + PLEXUS_RATE_LIMIT_COOLDOWN_MS;
#if PLEXUS_DEBUG
            plexus_hal_log("Rate limited — cooling down for %d ms",
                           PLEXUS_RATE_LIMIT_COOLDOWN_MS);
#endif
            break;
        }

#if PLEXUS_DEBUG
        plexus_hal_log("Retry %d/%d after error: %s (backoff: %lu ms)",
                       retry + 1, PLEXUS_MAX_RETRIES, plexus_strerror(err),
                       (unsigned long)client->retry_backoff_ms);
#endif
    }

#if PLEXUS_ENABLE_PERSISTENT_BUFFER
    plexus_hal_storage_write("plexus_buf", client->json_buffer, (size_t)json_len);
#if PLEXUS_DEBUG
    plexus_hal_log("Persisted %d bytes to flash buffer", json_len);
#endif
#endif

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

uint32_t plexus_total_sent(const plexus_client_t* client) {
    if (!client || !client->initialized) {
        return 0;
    }
    return client->total_sent;
}

uint32_t plexus_total_errors(const plexus_client_t* client) {
    if (!client || !client->initialized) {
        return 0;
    }
    return client->total_errors;
}

plexus_err_t plexus_tick(plexus_client_t* client) {
    if (!client) {
        return PLEXUS_ERR_NULL_PTR;
    }
    if (!client->initialized) {
        return PLEXUS_ERR_NOT_INITIALIZED;
    }

#if PLEXUS_ENABLE_COMMANDS
    if (client->command_handler) {
        uint32_t now_ms = plexus_hal_get_tick_ms();
        uint32_t cmd_elapsed = now_ms - client->last_command_poll_ms;

        if (cmd_elapsed >= PLEXUS_COMMAND_POLL_INTERVAL_MS || now_ms < client->last_command_poll_ms) {
            client->last_command_poll_ms = now_ms;
            plexus_poll_commands(client);
        }
    }
#endif

    /* Nothing to flush — return OK (idle is not an error) */
    if (client->metric_count == 0) {
        return PLEXUS_OK;
    }

    /* Time-based auto-flush */
    {
        uint32_t interval = client->flush_interval_ms > 0
            ? client->flush_interval_ms : PLEXUS_AUTO_FLUSH_INTERVAL_MS;
        if (interval > 0) {
            uint32_t now_ms = plexus_hal_get_tick_ms();
            uint32_t elapsed = now_ms - client->last_flush_ms;

            if (elapsed >= interval || now_ms < client->last_flush_ms) {
                return plexus_flush(client);
            }
        }
    }

    return PLEXUS_OK;
}
