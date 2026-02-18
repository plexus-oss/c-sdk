/**
 * @file plexus.c
 * @brief Plexus C SDK core implementation
 */

#include "plexus_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Compile-time configuration validation                                     */
/* ------------------------------------------------------------------------- */

/* _Static_assert is C11; use a fallback for C99 compilers without it */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define PLEXUS_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#elif defined(__GNUC__) || defined(__clang__)
    #define PLEXUS_STATIC_ASSERT(cond, msg) \
        typedef char plexus_assert_##__LINE__[(cond) ? 1 : -1] __attribute__((unused))
#else
    #define PLEXUS_STATIC_ASSERT(cond, msg) /* no-op on old compilers */
#endif

PLEXUS_STATIC_ASSERT(PLEXUS_MAX_METRICS > 0,
    "PLEXUS_MAX_METRICS must be at least 1");
PLEXUS_STATIC_ASSERT(PLEXUS_JSON_BUFFER_SIZE >= 256,
    "PLEXUS_JSON_BUFFER_SIZE must be at least 256 bytes");
PLEXUS_STATIC_ASSERT(PLEXUS_MAX_METRIC_NAME_LEN >= 8,
    "PLEXUS_MAX_METRIC_NAME_LEN must be at least 8");
PLEXUS_STATIC_ASSERT(PLEXUS_MAX_API_KEY_LEN >= 16,
    "PLEXUS_MAX_API_KEY_LEN must be at least 16");
PLEXUS_STATIC_ASSERT(PLEXUS_MAX_SOURCE_ID_LEN >= 4,
    "PLEXUS_MAX_SOURCE_ID_LEN must be at least 4");
PLEXUS_STATIC_ASSERT(PLEXUS_MAX_RETRIES >= 1 && PLEXUS_MAX_RETRIES <= 10,
    "PLEXUS_MAX_RETRIES must be between 1 and 10");
PLEXUS_STATIC_ASSERT(PLEXUS_MAX_ENDPOINT_LEN >= 32,
    "PLEXUS_MAX_ENDPOINT_LEN must be at least 32");

/* ------------------------------------------------------------------------- */
/* CRC32 for persistent buffer integrity (IEEE 802.3 polynomial)             */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_PERSISTENT_BUFFER

/* Header prepended to persistent buffer data */
typedef struct {
    uint32_t crc32;
    uint32_t data_len;
} plexus_persist_header_t;

/**
 * Bitwise CRC32 — no lookup table, saves ~1KB of flash.
 * Uses IEEE 802.3 polynomial (0xEDB88320 reflected).
 */
static uint32_t plexus_crc32(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

#endif /* PLEXUS_ENABLE_PERSISTENT_BUFFER */

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
    return PLEXUS_SDK_VERSION;
}

/* ------------------------------------------------------------------------- */
/* Source ID validation                                                      */
/* ------------------------------------------------------------------------- */

bool plexus_internal_is_url_safe(const char* s) {
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

/**
 * Validate that a metric name contains only printable ASCII characters (0x20-0x7E).
 * Rejects control characters, newlines, tabs, and non-ASCII bytes that could
 * produce invalid JSON or break server-side processing.
 */
static bool is_valid_metric_name(const char* s) {
    if (!s || s[0] == '\0') {
        return false;
    }
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7E) {
            return false;
        }
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
 * Common initialization logic shared by plexus_init and plexus_init_static.
 */
static plexus_client_t* client_init_common(plexus_client_t* client,
                                             const char* api_key,
                                             const char* source_id,
                                             bool heap_allocated) {
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
    client->_heap_allocated = heap_allocated;

#if PLEXUS_ENABLE_COMMANDS
    client->command_handler = NULL;
    client->last_command_poll_ms = plexus_hal_get_tick_ms();
#endif

#if PLEXUS_DEBUG
    plexus_hal_log("Plexus SDK v%s initialized (source: %s, client size: %u bytes)",
                   PLEXUS_SDK_VERSION, source_id, (unsigned)sizeof(plexus_client_t));
#endif

    return client;
}

plexus_client_t* plexus_init(const char* api_key, const char* source_id) {
    if (!api_key || !source_id) {
        return NULL;
    }
    if (!plexus_internal_is_url_safe(source_id)) {
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

    return client_init_common(client, api_key, source_id, true);
}

plexus_client_t* plexus_init_static(void* buf, size_t buf_size,
                                      const char* api_key, const char* source_id) {
    if (!buf || !api_key || !source_id) {
        return NULL;
    }
    if (buf_size < sizeof(plexus_client_t)) {
        return NULL;
    }
    /* Reject misaligned buffers — prevents hard faults on strict-alignment
     * targets (e.g., Cortex-M0). Use PLEXUS_CLIENT_STATIC_BUF() to guarantee
     * correct alignment. */
    if (((uintptr_t)buf % sizeof(void*)) != 0) {
        return NULL;
    }
    if (!plexus_internal_is_url_safe(source_id)) {
        return NULL;
    }
    if (strlen(api_key) >= PLEXUS_MAX_API_KEY_LEN ||
        strlen(source_id) >= PLEXUS_MAX_SOURCE_ID_LEN) {
        return NULL;
    }

    return client_init_common((plexus_client_t*)buf, api_key, source_id, false);
}

void plexus_free(plexus_client_t* client) {
    if (!client) {
        return;
    }
    client->initialized = false;
    if (client->_heap_allocated) {
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
    client->endpoint[sizeof(client->endpoint) - 1] = '\0';
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
 * Check if count-based auto-flush should trigger and flush if so.
 *
 * WARNING: plexus_flush() retries with exponential backoff and may block
 * for up to ~14 seconds. This is intentional — count-based auto-flush
 * prevents buffer overflows, and the caller should be aware that send
 * functions may block when the buffer fills to the flush threshold.
 */
static plexus_err_t maybe_auto_flush(plexus_client_t* client) {
    uint16_t flush_count = client->auto_flush_count > 0
        ? client->auto_flush_count : PLEXUS_AUTO_FLUSH_COUNT;
    if (flush_count > 0 && client->metric_count >= flush_count) {
        return plexus_flush(client);
    }
    return PLEXUS_OK;
}

/**
 * Queue a metric into the client's buffer.
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
    if (!is_valid_metric_name(metric)) {
        return PLEXUS_ERR_INVALID_ARG;
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

    return maybe_auto_flush(client);
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
    if (tag_count > PLEXUS_MAX_TAGS) {
        tag_count = PLEXUS_MAX_TAGS;
    }

    /* Validate and queue metric first. We need to suppress auto-flush
     * until tags are attached, so temporarily disable it. */
    if (!client) {
        return PLEXUS_ERR_NULL_PTR;
    }
    uint16_t saved_flush_count = client->auto_flush_count;
    client->auto_flush_count = 0; /* Suppress auto-flush temporarily */

    plexus_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = PLEXUS_VALUE_NUMBER;
    v.data.number = value;

    plexus_err_t err = add_metric(client, metric, &v, 0);
    client->auto_flush_count = saved_flush_count; /* Restore */

    if (err != PLEXUS_OK) {
        return err;
    }

    /* Attach tags to the just-queued metric */
    plexus_metric_t* m = &client->metrics[client->metric_count - 1];
    m->tag_count = tag_count;
    for (uint8_t i = 0; i < tag_count && tag_keys && tag_values; i++) {
        if (tag_keys[i] && tag_values[i]) {
            strncpy(m->tag_keys[i], tag_keys[i], PLEXUS_MAX_TAG_LEN - 1);
            m->tag_keys[i][PLEXUS_MAX_TAG_LEN - 1] = '\0';
            strncpy(m->tag_values[i], tag_values[i], PLEXUS_MAX_TAG_LEN - 1);
            m->tag_values[i][PLEXUS_MAX_TAG_LEN - 1] = '\0';
        }
    }

    return maybe_auto_flush(client);
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
/* Tick wraparound helper                                                    */
/* ------------------------------------------------------------------------- */

/**
 * Check if a deadline (set relative to a past tick) has passed.
 * Handles uint32_t wraparound correctly using signed comparison.
 */
static bool tick_elapsed(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
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
        if (!tick_elapsed(now, client->rate_limit_until_ms)) {
            return PLEXUS_ERR_RATE_LIMIT;
        }
        /* Cooldown expired */
        client->rate_limit_until_ms = 0;
    }

#if PLEXUS_ENABLE_PERSISTENT_BUFFER
    /* Attempt to send previously persisted data first */
    {
        size_t stored_len = 0;
        plexus_err_t restore_err = plexus_hal_storage_read(
            "plexus_buf", client->json_buffer, PLEXUS_JSON_BUFFER_SIZE, &stored_len);
        if (restore_err == PLEXUS_OK && stored_len > sizeof(plexus_persist_header_t)) {
            /* Validate CRC32 header */
            plexus_persist_header_t header;
            memcpy(&header, client->json_buffer, sizeof(header));
            size_t payload_len = stored_len - sizeof(header);

            if (header.data_len <= payload_len) {
                const char* payload = client->json_buffer + sizeof(header);
                uint32_t actual_crc = plexus_crc32(payload, header.data_len);

                if (actual_crc == header.crc32) {
#if PLEXUS_DEBUG
                    plexus_hal_log("Restoring %u bytes from persistent buffer",
                                   (unsigned)header.data_len);
#endif
                    /* Shift payload to front of buffer for sending */
                    memmove(client->json_buffer, payload, header.data_len);

                    plexus_err_t send_err = plexus_hal_http_post(
                        client->endpoint, client->api_key, PLEXUS_USER_AGENT,
                        client->json_buffer, header.data_len);
                    if (send_err == PLEXUS_OK) {
                        plexus_hal_storage_clear("plexus_buf");
                    }
                } else {
#if PLEXUS_DEBUG
                    plexus_hal_log("Persistent buffer CRC mismatch — discarding corrupt data");
#endif
                    plexus_hal_storage_clear("plexus_buf");
                }
            } else {
                /* Invalid header — clear corrupt data */
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
    /* Prepend CRC32 header to detect flash corruption on restore */
    if ((size_t)json_len + sizeof(plexus_persist_header_t) <= PLEXUS_JSON_BUFFER_SIZE) {
        /* Build header + payload into a temp region.
         * Shift json_buffer contents to make room for the header. */
        memmove(client->json_buffer + sizeof(plexus_persist_header_t),
                client->json_buffer, (size_t)json_len);

        plexus_persist_header_t header;
        header.data_len = (uint32_t)json_len;
        header.crc32 = plexus_crc32(client->json_buffer + sizeof(header),
                                     (size_t)json_len);
        memcpy(client->json_buffer, &header, sizeof(header));

        plexus_hal_storage_write("plexus_buf", client->json_buffer,
                                  sizeof(header) + (size_t)json_len);
#if PLEXUS_DEBUG
        plexus_hal_log("Persisted %d bytes to flash buffer (CRC: 0x%08x)",
                       json_len, (unsigned)header.crc32);
#endif
    }
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
        uint32_t cmd_deadline = client->last_command_poll_ms + PLEXUS_COMMAND_POLL_INTERVAL_MS;
        if (tick_elapsed(now_ms, cmd_deadline)) {
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
            uint32_t flush_deadline = client->last_flush_ms + interval;
            if (tick_elapsed(now_ms, flush_deadline)) {
                return plexus_flush(client);
            }
        }
    }

    return PLEXUS_OK;
}
