/**
 * @file plexus_json.c
 * @brief Minimal JSON builder for Plexus C SDK
 *
 * Zero-dependency JSON serialization targeting small code size.
 * Only generates the specific JSON format needed for the ingest API.
 */

#include "plexus_internal.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Internal buffer management */
typedef struct {
    char* buf;
    size_t size;
    size_t pos;
    bool error;
} json_writer_t;

static void json_init(json_writer_t* w, char* buf, size_t size) {
    w->buf = buf;
    w->size = size;
    w->pos = 0;
    w->error = false;
    if (size > 0) {
        buf[0] = '\0';
    }
}

static void json_append(json_writer_t* w, const char* str) {
    if (w->error) return;

    size_t len = strlen(str);
    if (w->pos + len >= w->size) {
        w->error = true;
        return;
    }

    memcpy(w->buf + w->pos, str, len);
    w->pos += len;
    w->buf[w->pos] = '\0';
}

static void json_append_char(json_writer_t* w, char c) {
    if (w->error) return;

    if (w->pos + 1 >= w->size) {
        w->error = true;
        return;
    }

    w->buf[w->pos++] = c;
    w->buf[w->pos] = '\0';
}

/* Escape JSON string characters per RFC 8259 */
static void json_append_escaped(json_writer_t* w, const char* str) {
    if (w->error || !str) return;

    json_append_char(w, '"');

    while (*str && !w->error) {
        char c = *str++;
        switch (c) {
            case '"':  json_append(w, "\\\""); break;
            case '\\': json_append(w, "\\\\"); break;
            case '\b': json_append(w, "\\b"); break;
            case '\f': json_append(w, "\\f"); break;
            case '\n': json_append(w, "\\n"); break;
            case '\r': json_append(w, "\\r"); break;
            case '\t': json_append(w, "\\t"); break;
            default:
                if ((unsigned char)c < 0x20) {
                    /* Escape control characters as \u00XX */
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)c);
                    json_append(w, esc);
                } else {
                    json_append_char(w, c);
                }
                break;
        }
    }

    json_append_char(w, '"');
}

/**
 * Format double with reasonable precision.
 *
 * Uses %g format which automatically picks the shorter of %f and %e notation,
 * handles very small and very large values correctly, and strips trailing zeros.
 */
static void json_append_number(json_writer_t* w, double value) {
    if (w->error) return;

    char num_buf[32];

    /* Handle special IEEE 754 cases — JSON has no NaN/Infinity */
    if (isnan(value)) {
        json_append(w, "null");
        return;
    }
    if (isinf(value)) {
        json_append(w, "null");
        return;
    }

    /* %g uses the shorter of %f/%e, strips trailing zeros, and handles
     * very small values (1e-7) that %.6f would round to 0. */
    int len = snprintf(num_buf, sizeof(num_buf), "%.10g", value);
    if (len > 0 && len < (int)sizeof(num_buf)) {
        json_append(w, num_buf);
    }
}

static void json_append_uint64(json_writer_t* w, uint64_t value) {
    if (w->error) return;

    char num_buf[24];
    snprintf(num_buf, sizeof(num_buf), "%llu", (unsigned long long)value);
    json_append(w, num_buf);
}

/**
 * Serialize metrics to JSON format for ingest API.
 *
 * Output format:
 * {
 *   "sdk": "c/0.5.0",
 *   "points": [
 *     {
 *       "metric": "temperature",
 *       "value": 72.5,
 *       "timestamp": 1699900000123,
 *       "source_id": "device-001",
 *       "tags": {"location": "sensor-1"}
 *     }
 *   ]
 * }
 */
int plexus_json_serialize(const plexus_client_t* client, char* buf, size_t buf_size) {
    if (!client || !buf || buf_size == 0) {
        return -1;
    }

    json_writer_t w;
    json_init(&w, buf, buf_size);

    json_append(&w, "{\"sdk\":\"c/" PLEXUS_SDK_VERSION "\",\"points\":[");

    for (uint16_t i = 0; i < client->metric_count; i++) {
        const plexus_metric_t* m = &client->metrics[i];

        if (i > 0) {
            json_append_char(&w, ',');
        }

        json_append(&w, "{\"metric\":");
        json_append_escaped(&w, m->name);

        json_append(&w, ",\"value\":");
        switch (m->value.type) {
            case PLEXUS_VALUE_NUMBER:
                json_append_number(&w, m->value.data.number);
                break;
#if PLEXUS_ENABLE_STRING_VALUES
            case PLEXUS_VALUE_STRING:
                json_append_escaped(&w, m->value.data.string);
                break;
#endif
#if PLEXUS_ENABLE_BOOL_VALUES
            case PLEXUS_VALUE_BOOL:
                json_append(&w, m->value.data.boolean ? "true" : "false");
                break;
#endif
            default:
                json_append(&w, "null");
                break;
        }

        /* Timestamp (if set) */
        if (m->timestamp_ms > 0) {
            json_append(&w, ",\"timestamp\":");
            json_append_uint64(&w, m->timestamp_ms);
        }

        /* Source ID */
        json_append(&w, ",\"source_id\":");
        json_append_escaped(&w, client->source_id);

#if PLEXUS_ENABLE_TAGS
        /* Tags */
        if (m->tag_count > 0) {
            json_append(&w, ",\"tags\":{");
            for (uint8_t t = 0; t < m->tag_count; t++) {
                if (t > 0) {
                    json_append_char(&w, ',');
                }
                json_append_escaped(&w, m->tag_keys[t]);
                json_append_char(&w, ':');
                json_append_escaped(&w, m->tag_values[t]);
            }
            json_append_char(&w, '}');
        }
#endif

        json_append_char(&w, '}');
    }

    json_append(&w, "]}");

    if (w.error) {
        return -1;
    }

    return (int)w.pos;
}
