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
 *   "sdk": "c/0.2.1",
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

/* ========================================================================= */
/* General-purpose JSON parse utilities                                      */
/* Used by commands, auto-registration, and any feature needing JSON parsing */
/* ========================================================================= */

/**
 * Minimal JSON string extractor.
 * Finds "key":"value" and copies the value. Returns 0 on success.
 */
int plexus_json_extract_string(const char* json, size_t json_len,
                                const char* key, char* out, size_t out_size) {
    /* Build search pattern: "key":" */
    char pattern[80];
    int plen = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    if (plen < 0 || plen >= (int)sizeof(pattern)) return -1;

    const char* pos = strstr(json, pattern);
    if (!pos) return -1;

    const char* value_start = pos + plen;
    const char* value_end = value_start;
    const char* json_end = json + json_len;

    /* Find closing quote (handle escaped quotes) */
    while (value_end < json_end && *value_end != '\0') {
        if (*value_end == '\\' && (value_end + 1) < json_end) {
            value_end += 2; /* Skip escaped char */
            continue;
        }
        if (*value_end == '"') break;
        value_end++;
    }

    /* Unescape common JSON sequences while copying */
    size_t out_pos = 0;
    const char* src = value_start;
    while (src < value_end && out_pos < out_size - 1) {
        if (*src == '\\' && (src + 1) < value_end) {
            char next = *(src + 1);
            switch (next) {
                case '"':  out[out_pos++] = '"';  src += 2; break;
                case '\\': out[out_pos++] = '\\'; src += 2; break;
                case 'n':  out[out_pos++] = '\n'; src += 2; break;
                case 'r':  out[out_pos++] = '\r'; src += 2; break;
                case 't':  out[out_pos++] = '\t'; src += 2; break;
                case '/':  out[out_pos++] = '/';  src += 2; break;
                default:   out[out_pos++] = *src++; break; /* copy backslash as-is */
            }
        } else {
            out[out_pos++] = *src++;
        }
    }
    out[out_pos] = '\0';
    return 0;
}

/**
 * Minimal JSON number extractor with overflow protection.
 * Finds "key":123 and returns the integer value, or default_val on error.
 */
int plexus_json_extract_int(const char* json, size_t json_len,
                             const char* key, int default_val) {
    char pattern[80];
    int plen = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (plen < 0 || plen >= (int)sizeof(pattern)) return default_val;

    const char* pos = strstr(json, pattern);
    if (!pos) return default_val;

    const char* num_start = pos + plen;
    /* Skip whitespace */
    while (*num_start == ' ' || *num_start == '\t') num_start++;

    /* Don't parse strings or objects */
    if (*num_start == '"' || *num_start == '{' || *num_start == '[') return default_val;

    uint32_t val = 0;
    int sign = 1;
    if (*num_start == '-') { sign = -1; num_start++; }

    int digits = 0;
    while (*num_start >= '0' && *num_start <= '9' && digits < 10) {
        uint32_t digit = (uint32_t)(*num_start - '0');
        /* Overflow check before multiply/add (works on both 32/64-bit) */
        if (val > (UINT32_MAX - digit) / 10) return default_val;
        val = val * 10 + digit;
        num_start++;
        digits++;
    }

    if (digits == 0) return default_val;

    /* Check range for signed int result */
    if (sign == 1 && val > 2147483647U) return default_val;
    if (sign == -1 && val > 2147483648U) return default_val;

    (void)json_len;
    return (int)((int32_t)val * sign);
}

/* ========================================================================= */
/* Command JSON parsing and result building (for PLEXUS_ENABLE_COMMANDS)     */
/* ========================================================================= */

#if PLEXUS_ENABLE_COMMANDS

/**
 * Parse a command polling response JSON.
 */
int plexus_json_parse_command(const char* json, size_t json_len,
                               plexus_command_t* cmd) {
    if (!json || !cmd || json_len == 0) return -1;

    /* Check if commands array is empty */
    if (strstr(json, "\"commands\":[]") || strstr(json, "\"commands\": []")) {
        return 0;
    }

    plexus_json_extract_string(json, json_len, "id", cmd->id, sizeof(cmd->id));
    plexus_json_extract_string(json, json_len, "command", cmd->command, sizeof(cmd->command));
    cmd->timeout_seconds = plexus_json_extract_int(json, json_len, "timeout_seconds", 300);

    return 0;
}

/**
 * Build a command result JSON payload.
 */
int plexus_json_build_result(char* buf, size_t buf_size,
                              const char* status, int exit_code,
                              const char* output, const char* error) {
    if (!buf || buf_size == 0 || !status) return -1;

    json_writer_t w;
    json_init(&w, buf, buf_size);

    json_append(&w, "{\"status\":");
    json_append_escaped(&w, status);

    json_append(&w, ",\"exit_code\":");
    {
        char num[16];
        snprintf(num, sizeof(num), "%d", exit_code);
        json_append(&w, num);
    }

    if (output && output[0] != '\0') {
        json_append(&w, ",\"output\":");
        json_append_escaped(&w, output);
    }

    if (error && error[0] != '\0') {
        json_append(&w, ",\"error\":");
        json_append_escaped(&w, error);
    }

    json_append_char(&w, '}');

    if (w.error) return -1;
    return (int)w.pos;
}

#endif /* PLEXUS_ENABLE_COMMANDS */

/* ========================================================================= */
/* Heartbeat JSON builder (for PLEXUS_ENABLE_HEARTBEAT)                      */
/* ========================================================================= */

#if PLEXUS_ENABLE_HEARTBEAT

int plexus_json_build_heartbeat(const plexus_client_t* client, char* buf, size_t buf_size) {
    if (!client || !buf || buf_size == 0) return -1;

    json_writer_t w;
    json_init(&w, buf, buf_size);

    json_append(&w, "{\"sdk\":\"c/" PLEXUS_SDK_VERSION "\"");

    json_append(&w, ",\"source_id\":");
    json_append_escaped(&w, client->source_id);

    if (client->device_type[0] != '\0') {
        json_append(&w, ",\"device_type\":");
        json_append_escaped(&w, client->device_type);
    }

    if (client->firmware_version[0] != '\0') {
        json_append(&w, ",\"firmware_version\":");
        json_append_escaped(&w, client->firmware_version);
    }

    json_append(&w, ",\"uptime_ms\":");
    {
        char num[16];
        snprintf(num, sizeof(num), "%lu", (unsigned long)plexus_hal_get_tick_ms());
        json_append(&w, num);
    }

    json_append(&w, ",\"total_sent\":");
    {
        char num[16];
        snprintf(num, sizeof(num), "%lu", (unsigned long)client->total_sent);
        json_append(&w, num);
    }

    json_append(&w, ",\"total_errors\":");
    {
        char num[16];
        snprintf(num, sizeof(num), "%lu", (unsigned long)client->total_errors);
        json_append(&w, num);
    }

    json_append(&w, ",\"metrics\":[");
    for (uint16_t i = 0; i < client->registered_metric_count; i++) {
        if (i > 0) json_append_char(&w, ',');
        json_append_escaped(&w, client->registered_metrics[i]);
    }
    json_append_char(&w, ']');

#if PLEXUS_ENABLE_SENSOR_DISCOVERY
    /* Append sensors array when sensor discovery is also enabled */
    if (client->detected_sensor_count > 0) {
        json_append(&w, ",\"sensors\":[");
        for (uint8_t s = 0; s < client->detected_sensor_count; s++) {
            const plexus_detected_sensor_t* ds = &client->detected_sensors[s];
            if (!ds->descriptor) continue;

            if (s > 0) json_append_char(&w, ',');
            json_append(&w, "{\"name\":");
            json_append_escaped(&w, ds->descriptor->name);

            if (ds->descriptor->description) {
                json_append(&w, ",\"description\":");
                json_append_escaped(&w, ds->descriptor->description);
            }

            json_append(&w, ",\"metrics\":[");
            for (uint8_t m = 0; m < ds->descriptor->metric_count; m++) {
                if (m > 0) json_append_char(&w, ',');
                json_append_escaped(&w, ds->descriptor->metrics[m]);
            }
            json_append_char(&w, ']');

            json_append(&w, ",\"sample_rate\":");
            json_append_number(&w, (double)ds->descriptor->default_sample_rate_hz);

            json_append_char(&w, '}');
        }
        json_append_char(&w, ']');
    }
#endif /* PLEXUS_ENABLE_SENSOR_DISCOVERY */

    json_append_char(&w, '}');

    if (w.error) return -1;
    return (int)w.pos;
}

#endif /* PLEXUS_ENABLE_HEARTBEAT */

/* ========================================================================= */
/* Registration JSON builder (for PLEXUS_ENABLE_AUTO_REGISTER)               */
/* ========================================================================= */

#if PLEXUS_ENABLE_AUTO_REGISTER

int plexus_json_build_register(char* buf, size_t buf_size,
                                const char* source_id,
                                const char* hostname,
                                const char* platform_name) {
    if (!buf || buf_size == 0 || !source_id) return -1;

    json_writer_t w;
    json_init(&w, buf, buf_size);

    json_append(&w, "{\"name\":");
    json_append_escaped(&w, source_id);

    if (hostname && hostname[0] != '\0') {
        json_append(&w, ",\"hostname\":");
        json_append_escaped(&w, hostname);
    }

    if (platform_name && platform_name[0] != '\0') {
        json_append(&w, ",\"platform\":");
        json_append_escaped(&w, platform_name);
    }

    json_append_char(&w, '}');

    if (w.error) return -1;
    return (int)w.pos;
}

#endif /* PLEXUS_ENABLE_AUTO_REGISTER */
