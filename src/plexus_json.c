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

/* Escape JSON string characters */
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
                    /* Control characters - skip or replace with space */
                    json_append_char(w, ' ');
                } else {
                    json_append_char(w, c);
                }
                break;
        }
    }

    json_append_char(w, '"');
}

/* Format double with reasonable precision */
static void json_append_number(json_writer_t* w, double value) {
    if (w->error) return;

    char num_buf[32];
    int len;

    /* Handle special cases */
    if (value != value) {  /* NaN */
        json_append(w, "null");
        return;
    }
    if (value > 1e308 || value < -1e308) {  /* Infinity */
        json_append(w, "null");
        return;
    }

    /* Format with up to 6 decimal places, removing trailing zeros */
    len = snprintf(num_buf, sizeof(num_buf), "%.6f", value);
    if (len > 0 && len < (int)sizeof(num_buf)) {
        /* Remove trailing zeros after decimal point */
        char* dot = strchr(num_buf, '.');
        if (dot) {
            char* end = num_buf + len - 1;
            while (end > dot && *end == '0') {
                *end-- = '\0';
            }
            /* Remove trailing decimal point */
            if (end == dot) {
                *end = '\0';
            }
        }
        json_append(w, num_buf);
    }
}

static void json_append_uint64(json_writer_t* w, uint64_t value) {
    if (w->error) return;

    char num_buf[24];
    /* Output raw milliseconds as integer */
    snprintf(num_buf, sizeof(num_buf), "%llu", (unsigned long long)value);
    json_append(w, num_buf);
}

/**
 * Serialize metrics to JSON format for ingest API
 *
 * Output format:
 * {
 *   "points": [
 *     {
 *       "metric": "temperature",
 *       "value": 72.5,
 *       "timestamp": 1699900000.123,
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

    json_append(&w, "{\"points\":[");

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
/* Command JSON parsing and result building (for PLEXUS_ENABLE_COMMANDS)     */
/* ========================================================================= */

#if PLEXUS_ENABLE_COMMANDS

/**
 * Minimal JSON string extractor
 * Finds "key":"value" in a JSON string and copies the value.
 * Returns 0 on success, -1 if key not found.
 */
static int json_extract_string(const char* json, size_t json_len,
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

    size_t value_len = (size_t)(value_end - value_start);
    if (value_len >= out_size) value_len = out_size - 1;

    memcpy(out, value_start, value_len);
    out[value_len] = '\0';
    return 0;
}

/**
 * Minimal JSON number extractor
 * Finds "key":123 in a JSON string and returns the integer value.
 * Returns the value, or default_val if key not found.
 */
static int json_extract_int(const char* json, size_t json_len,
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

    int val = 0;
    int sign = 1;
    if (*num_start == '-') { sign = -1; num_start++; }

    while (*num_start >= '0' && *num_start <= '9') {
        val = val * 10 + (*num_start - '0');
        num_start++;
    }

    (void)json_len;
    return val * sign;
}

/**
 * Parse a command polling response JSON
 *
 * Expected format:
 * {"commands":[{"id":"uuid","command":"shell cmd","timeout_seconds":300}]}
 * or: {"commands":[]}
 *
 * @return 0 on success (cmd populated or empty), -1 on parse error
 */
int plexus_json_parse_command(const char* json, size_t json_len,
                               plexus_command_t* cmd) {
    if (!json || !cmd || json_len == 0) return -1;

    /* Check if commands array is empty */
    if (strstr(json, "\"commands\":[]") || strstr(json, "\"commands\": []")) {
        return 0; /* No commands — cmd.id stays empty */
    }

    /* Extract fields from the first command object */
    json_extract_string(json, json_len, "id", cmd->id, sizeof(cmd->id));
    json_extract_string(json, json_len, "command", cmd->command, sizeof(cmd->command));
    cmd->timeout_seconds = json_extract_int(json, json_len, "timeout_seconds", 300);

    return 0;
}

/**
 * Build a command result JSON payload
 *
 * Output format:
 * {"status":"completed","exit_code":0,"output":"...","error":"..."}
 *
 * @return Length of JSON, or -1 on error
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
