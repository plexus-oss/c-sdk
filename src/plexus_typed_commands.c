/**
 * @file plexus_typed_commands.c
 * @brief Typed command registration and schema serialization for Plexus C SDK
 *
 * When PLEXUS_ENABLE_TYPED_COMMANDS is set, devices can declare structured
 * commands with typed parameters. The schema is serialized to JSON so the
 * dashboard can auto-generate UI controls (sliders, toggles, dropdowns).
 */

#include "plexus_internal.h"

#if PLEXUS_ENABLE_TYPED_COMMANDS

#include <string.h>
#include <stdio.h>

/* ========================================================================= */
/* Registration                                                              */
/* ========================================================================= */

plexus_err_t plexus_register_typed_command(plexus_client_t* client,
                                            const plexus_typed_command_t* command) {
    if (!client || !command) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;

    if (command->name[0] == '\0') return PLEXUS_ERR_INVALID_ARG;
    if (command->param_count > PLEXUS_MAX_COMMAND_PARAMS) return PLEXUS_ERR_INVALID_ARG;

    if (client->typed_command_count >= PLEXUS_MAX_TYPED_COMMANDS) {
        return PLEXUS_ERR_BUFFER_FULL;
    }

    /* Check for duplicate name */
    for (uint8_t i = 0; i < client->typed_command_count; i++) {
        if (strcmp(client->typed_commands[i].name, command->name) == 0) {
            return PLEXUS_ERR_INVALID_ARG;
        }
    }

    /* Copy the full command descriptor */
    memcpy(&client->typed_commands[client->typed_command_count],
           command, sizeof(plexus_typed_command_t));
    client->typed_command_count++;

#if PLEXUS_DEBUG
    plexus_hal_log("Registered typed command: %s (%u params)",
                   command->name, (unsigned)command->param_count);
#endif

    return PLEXUS_OK;
}

/* ========================================================================= */
/* Schema JSON serialization                                                 */
/*                                                                           */
/* Uses the same manual json_writer pattern as plexus_json.c.                */
/* We duplicate the minimal writer helpers here so that this translation     */
/* unit is self-contained and doesn't export internal symbols.               */
/* ========================================================================= */

typedef struct {
    char* buf;
    size_t size;
    size_t pos;
    bool error;
} tc_json_writer_t;

static void tc_json_init(tc_json_writer_t* w, char* buf, size_t size) {
    w->buf = buf;
    w->size = size;
    w->pos = 0;
    w->error = false;
    if (size > 0) {
        buf[0] = '\0';
    }
}

static void tc_json_append(tc_json_writer_t* w, const char* str) {
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

static void tc_json_append_char(tc_json_writer_t* w, char c) {
    if (w->error) return;
    if (w->pos + 1 >= w->size) {
        w->error = true;
        return;
    }
    w->buf[w->pos++] = c;
    w->buf[w->pos] = '\0';
}

static void tc_json_append_escaped(tc_json_writer_t* w, const char* str) {
    if (w->error || !str) return;
    tc_json_append_char(w, '"');
    while (*str && !w->error) {
        char c = *str++;
        switch (c) {
            case '"':  tc_json_append(w, "\\\""); break;
            case '\\': tc_json_append(w, "\\\\"); break;
            case '\n': tc_json_append(w, "\\n"); break;
            case '\r': tc_json_append(w, "\\r"); break;
            case '\t': tc_json_append(w, "\\t"); break;
            default:
                if ((unsigned char)c < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)c);
                    tc_json_append(w, esc);
                } else {
                    tc_json_append_char(w, c);
                }
                break;
        }
    }
    tc_json_append_char(w, '"');
}

static void tc_json_append_number(tc_json_writer_t* w, double value) {
    if (w->error) return;
    char num_buf[32];
    int len = snprintf(num_buf, sizeof(num_buf), "%.10g", value);
    if (len > 0 && len < (int)sizeof(num_buf)) {
        tc_json_append(w, num_buf);
    }
}

static const char* param_type_str(plexus_param_type_t type) {
    switch (type) {
        case PLEXUS_PARAM_FLOAT:  return "float";
        case PLEXUS_PARAM_INT:    return "int";
        case PLEXUS_PARAM_STRING: return "string";
        case PLEXUS_PARAM_BOOL:   return "bool";
        case PLEXUS_PARAM_ENUM:   return "enum";
        default:                  return "unknown";
    }
}

int plexus_typed_commands_schema(const plexus_client_t* client,
                                 char* buf, size_t buf_size) {
    if (!client || !buf || buf_size == 0) return -1;

    tc_json_writer_t w;
    tc_json_init(&w, buf, buf_size);

    tc_json_append_char(&w, '[');

    for (uint8_t ci = 0; ci < client->typed_command_count; ci++) {
        const plexus_typed_command_t* cmd = &client->typed_commands[ci];

        if (ci > 0) tc_json_append_char(&w, ',');

        tc_json_append(&w, "{\"name\":");
        tc_json_append_escaped(&w, cmd->name);

        if (cmd->description[0] != '\0') {
            tc_json_append(&w, ",\"description\":");
            tc_json_append_escaped(&w, cmd->description);
        }

        tc_json_append(&w, ",\"params\":[");

        for (uint8_t pi = 0; pi < cmd->param_count; pi++) {
            const plexus_param_desc_t* p = &cmd->params[pi];

            if (pi > 0) tc_json_append_char(&w, ',');

            tc_json_append(&w, "{\"name\":");
            tc_json_append_escaped(&w, p->name);

            tc_json_append(&w, ",\"type\":");
            tc_json_append_escaped(&w, param_type_str(p->type));

            if (p->description[0] != '\0') {
                tc_json_append(&w, ",\"description\":");
                tc_json_append_escaped(&w, p->description);
            }

            if (p->unit[0] != '\0') {
                tc_json_append(&w, ",\"unit\":");
                tc_json_append_escaped(&w, p->unit);
            }

            /* Min/max/step for numeric types */
            if (p->type == PLEXUS_PARAM_FLOAT || p->type == PLEXUS_PARAM_INT) {
                if (p->min_val != 0.0 || p->max_val != 0.0) {
                    tc_json_append(&w, ",\"min\":");
                    tc_json_append_number(&w, p->min_val);
                    tc_json_append(&w, ",\"max\":");
                    tc_json_append_number(&w, p->max_val);
                }
                if (p->step != 0.0) {
                    tc_json_append(&w, ",\"step\":");
                    tc_json_append_number(&w, p->step);
                }
            }

            if (p->has_default) {
                tc_json_append(&w, ",\"default\":");
                if (p->type == PLEXUS_PARAM_BOOL) {
                    tc_json_append(&w, p->default_val != 0.0 ? "true" : "false");
                } else {
                    tc_json_append_number(&w, p->default_val);
                }
            }

            tc_json_append(&w, ",\"required\":");
            tc_json_append(&w, p->required ? "true" : "false");

            /* Enum choices */
            if (p->type == PLEXUS_PARAM_ENUM && p->choice_count > 0) {
                tc_json_append(&w, ",\"choices\":[");
                for (uint8_t ei = 0; ei < p->choice_count; ei++) {
                    if (!p->choices[ei]) break;
                    if (ei > 0) tc_json_append_char(&w, ',');
                    tc_json_append_escaped(&w, p->choices[ei]);
                }
                tc_json_append_char(&w, ']');
            }

            tc_json_append_char(&w, '}');
        }

        tc_json_append(&w, "]}");
    }

    tc_json_append_char(&w, ']');

    if (w.error) return -1;
    return (int)w.pos;
}

#endif /* PLEXUS_ENABLE_TYPED_COMMANDS */
