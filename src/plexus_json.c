/**
 * @file plexus_json.c
 * @brief Minimal JSON builder for Plexus C SDK
 *
 * Zero-dependency JSON serialization targeting small code size.
 * Only generates the specific JSON format needed for the ingest API.
 */

#include "plexus_internal.h"
#include "plexus_ws.h"
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
 *   "sdk": "c/0.5.4",
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

    json_append(&w, "{\"sdk\":\"c/" PLEXUS_SDK_VERSION "\",\"source_id\":");
    json_append_escaped(&w, client->source_id);
    json_append(&w, ",\"points\":[");

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

        /* Session ID (only when a session is active) */
        if (client->session_id[0] != '\0') {
            json_append(&w, ",\"session_id\":");
            json_append_escaped(&w, client->session_id);
        }

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
/* WebSocket JSON serializers                                                */
/* ========================================================================= */

#if PLEXUS_ENABLE_WEBSOCKET

int plexus_json_serialize_ws_auth(const plexus_client_t* client, char* buf, size_t buf_size) {
    if (!client || !buf || buf_size == 0) return -1;

    json_writer_t w;
    json_init(&w, buf, buf_size);

    json_append(&w, "{\"type\":\"device_auth\",\"api_key\":");
    json_append_escaped(&w, client->api_key);
    json_append(&w, ",\"source_id\":");
    json_append_escaped(&w, client->source_id);
    json_append(&w, ",\"platform\":\"c-sdk\",\"agent_version\":\"" PLEXUS_SDK_VERSION "\"");

    /* Command schemas */
    if (client->ws_command_count > 0) {
        json_append(&w, ",\"commands\":[");
        for (uint8_t i = 0; i < client->ws_command_count; i++) {
            const plexus_cmd_reg_t* cmd = &client->ws_commands[i];
            if (i > 0) json_append_char(&w, ',');

            json_append(&w, "{\"name\":");
            json_append_escaped(&w, cmd->name);
            if (cmd->description[0] != '\0') {
                json_append(&w, ",\"description\":");
                json_append_escaped(&w, cmd->description);
            }
            json_append(&w, ",\"params\":[");

            for (uint8_t p = 0; p < cmd->param_count; p++) {
                const plexus_param_t* param = &cmd->params[p];
                if (p > 0) json_append_char(&w, ',');

                json_append(&w, "{\"name\":");
                json_append_escaped(&w, param->name);

                json_append(&w, ",\"type\":");
                switch (param->type) {
                    case PLEXUS_PARAM_FLOAT:  json_append(&w, "\"float\""); break;
                    case PLEXUS_PARAM_INT:    json_append(&w, "\"int\""); break;
                    case PLEXUS_PARAM_STRING: json_append(&w, "\"string\""); break;
                    case PLEXUS_PARAM_BOOL:   json_append(&w, "\"bool\""); break;
                    case PLEXUS_PARAM_ENUM:   json_append(&w, "\"enum\""); break;
                }

                if (!isnan(param->min)) {
                    json_append(&w, ",\"min\":");
                    json_append_number(&w, param->min);
                }
                if (!isnan(param->max)) {
                    json_append(&w, ",\"max\":");
                    json_append_number(&w, param->max);
                }

                json_append(&w, ",\"required\":");
                json_append(&w, param->required ? "true" : "false");

                json_append_char(&w, '}');
            }
            json_append(&w, "]}");
        }
        json_append_char(&w, ']');
    }

    json_append_char(&w, '}');

    return w.error ? -1 : (int)w.pos;
}

int plexus_json_serialize_ws_heartbeat(const plexus_client_t* client, char* buf, size_t buf_size) {
    if (!client || !buf || buf_size == 0) return -1;

    json_writer_t w;
    json_init(&w, buf, buf_size);

    json_append(&w, "{\"type\":\"heartbeat\",\"source_id\":");
    json_append_escaped(&w, client->source_id);
    json_append(&w, ",\"agent_version\":\"" PLEXUS_SDK_VERSION "\"}");

    return w.error ? -1 : (int)w.pos;
}

int plexus_json_serialize_ws_telemetry(const plexus_client_t* client, char* buf, size_t buf_size) {
    if (!client || !buf || buf_size == 0) return -1;

    json_writer_t w;
    json_init(&w, buf, buf_size);

    json_append(&w, "{\"type\":\"telemetry\",\"points\":[");

    for (uint16_t i = 0; i < client->metric_count; i++) {
        const plexus_metric_t* m = &client->metrics[i];
        if (i > 0) json_append_char(&w, ',');

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

        if (m->timestamp_ms > 0) {
            json_append(&w, ",\"timestamp\":");
            json_append_uint64(&w, m->timestamp_ms);
        }

#if PLEXUS_ENABLE_TAGS
        if (m->tag_count > 0) {
            json_append(&w, ",\"tags\":{");
            for (uint8_t t = 0; t < m->tag_count; t++) {
                if (t > 0) json_append_char(&w, ',');
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

    return w.error ? -1 : (int)w.pos;
}

int plexus_json_serialize_command_result(const char* cmd_id, const char* command_name,
                                          const char* result_json, const char* error,
                                          char* buf, size_t buf_size) {
    if (!cmd_id || !buf || buf_size == 0) return -1;

    json_writer_t w;
    json_init(&w, buf, buf_size);

    json_append(&w, "{\"type\":\"command_result\",\"id\":");
    json_append_escaped(&w, cmd_id);

    if (command_name) {
        json_append(&w, ",\"command\":");
        json_append_escaped(&w, command_name);
    }

    if (error) {
        json_append(&w, ",\"event\":\"error\",\"error\":");
        json_append_escaped(&w, error);
    } else {
        json_append(&w, ",\"event\":\"result\"");
        if (result_json) {
            json_append(&w, ",\"result\":");
            json_append(&w, result_json);
        }
    }

    json_append_char(&w, '}');

    return w.error ? -1 : (int)w.pos;
}

/* ========================================================================= */
/* Minimal JSON field extraction                                             */
/*                                                                           */
/* Purpose-built for parsing PartyKit server messages. NOT a general parser. */
/* Handles flat objects with string values. Sufficient for extracting        */
/* "type", "id", "command", "params" from typed_command messages.            */
/* ========================================================================= */

bool plexus_json_find_string(const char* json, const char* key, char* out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) return false;
    out[0] = '\0';

    /* Build search pattern: "key":" */
    char pattern[64];
    int plen = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    if (plen <= 0 || plen >= (int)sizeof(pattern)) return false;

    const char* found = strstr(json, pattern);
    if (!found) return false;

    const char* start = found + plen;
    size_t i = 0;

    /* Copy until closing unescaped quote */
    while (*start && *start != '"' && i < out_size - 1) {
        if (*start == '\\' && *(start + 1)) {
            start++;  /* Skip escape — copy the escaped char */
        }
        out[i++] = *start++;
    }
    out[i] = '\0';

    return i > 0;
}

bool plexus_json_find_value(const char* json, const char* key, char* out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) return false;
    out[0] = '\0';

    /* Build search pattern: "key": */
    char pattern[64];
    int plen = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (plen <= 0 || plen >= (int)sizeof(pattern)) return false;

    const char* found = strstr(json, pattern);
    if (!found) return false;

    const char* start = found + plen;
    /* Skip whitespace */
    while (*start == ' ' || *start == '\t') start++;

    if (*start == '{') {
        /* Copy object — track brace depth */
        int depth = 0;
        size_t i = 0;
        while (*start && i < out_size - 1) {
            out[i++] = *start;
            if (*start == '{') depth++;
            else if (*start == '}') { depth--; if (depth == 0) break; }
            else if (*start == '"') {
                /* Skip string contents to avoid counting braces inside strings */
                start++;
                while (*start && i < out_size - 1) {
                    out[i++] = *start;
                    if (*start == '"' && *(start - 1) != '\\') break;
                    start++;
                }
            }
            start++;
        }
        out[i] = '\0';
        return i > 0;
    } else if (*start == '"') {
        /* String value — delegate to find_string */
        return plexus_json_find_string(json, key, out, out_size);
    } else {
        /* Number, bool, null — copy until comma, brace, or bracket */
        size_t i = 0;
        while (*start && *start != ',' && *start != '}' && *start != ']' &&
               *start != ' ' && *start != '\n' && i < out_size - 1) {
            out[i++] = *start++;
        }
        out[i] = '\0';
        return i > 0;
    }
}

#endif /* PLEXUS_ENABLE_WEBSOCKET */
