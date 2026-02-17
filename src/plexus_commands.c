/**
 * @file plexus_commands.c
 * @brief Command polling and execution for Plexus C SDK
 *
 * When PLEXUS_ENABLE_COMMANDS is set, devices can receive and execute
 * commands from the Plexus server via HTTP polling.
 *
 * Flow:
 * 1. Device calls plexus_poll_commands() (or via plexus_tick())
 * 2. GET /api/commands/poll?sourceId=<slug> returns queued command
 * 3. User callback executes the command and provides output
 * 4. POST /api/commands/<id>/result sends result back
 */

#include "plexus.h"

#if PLEXUS_ENABLE_COMMANDS

#include <string.h>
#include <stdio.h>

/* Forward declarations for JSON helpers (in plexus_json.c) */
int plexus_json_parse_command(const char* json, size_t json_len,
                               plexus_command_t* cmd);
int plexus_json_build_result(char* buf, size_t buf_size,
                              const char* status, int exit_code,
                              const char* output, const char* error);

/* Reuse the global JSON buffer from plexus.c */
extern char s_json_buffer[];

plexus_err_t plexus_register_command_handler(plexus_client_t* client,
                                              plexus_command_handler_t handler) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;

    client->command_handler = (plexus_command_handler_fn)handler;
    return PLEXUS_OK;
}

plexus_err_t plexus_poll_commands(plexus_client_t* client) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;
    if (!client->command_handler) return PLEXUS_OK; /* No handler, nothing to do */

    /* Build poll URL: base_endpoint but replace /api/ingest with /api/commands/poll?sourceId=X */
    char poll_url[512];
    {
        /* Find the base URL (everything before /api/ingest) */
        const char* api_pos = strstr(client->endpoint, "/api/ingest");
        size_t base_len;
        if (api_pos) {
            base_len = (size_t)(api_pos - client->endpoint);
        } else {
            /* Fallback: use endpoint as-is minus trailing path */
            base_len = strlen(client->endpoint);
            /* Strip trailing slash */
            while (base_len > 0 && client->endpoint[base_len - 1] == '/') {
                base_len--;
            }
        }

        int written = snprintf(poll_url, sizeof(poll_url),
            "%.*s/api/commands/poll?sourceId=%s",
            (int)base_len, client->endpoint, client->source_id);

        if (written < 0 || written >= (int)sizeof(poll_url)) {
            return PLEXUS_ERR_HAL;
        }
    }

    /* Poll for commands */
    char response_buf[PLEXUS_JSON_BUFFER_SIZE];
    size_t response_len = 0;

    plexus_err_t err = plexus_hal_http_get(
        poll_url, client->api_key,
        response_buf, sizeof(response_buf), &response_len
    );

    if (err != PLEXUS_OK) {
#if PLEXUS_DEBUG
        plexus_hal_log("Command poll failed: %s", plexus_strerror(err));
#endif
        return err;
    }

    if (response_len == 0) {
        return PLEXUS_OK; /* Empty response */
    }

    /* Parse command from response */
    plexus_command_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    if (plexus_json_parse_command(response_buf, response_len, &cmd) != 0) {
        return PLEXUS_OK; /* No command or parse error — not fatal */
    }

    /* No command ID means empty commands array */
    if (cmd.id[0] == '\0') {
        return PLEXUS_OK;
    }

#if PLEXUS_DEBUG
    plexus_hal_log("Received command: %s (id=%s, timeout=%ds)",
                   cmd.command, cmd.id, cmd.timeout_seconds);
#endif

    /* Execute via user callback */
    char output[PLEXUS_MAX_COMMAND_RESULT_LEN];
    int exit_code = -1;
    memset(output, 0, sizeof(output));

    plexus_command_handler_t handler = (plexus_command_handler_t)client->command_handler;
    plexus_err_t exec_err = handler(&cmd, output, &exit_code);

    /* Determine result status */
    const char* status;
    const char* error_str = NULL;
    if (exec_err != PLEXUS_OK) {
        status = "failed";
        error_str = plexus_strerror(exec_err);
    } else {
        status = (exit_code == 0) ? "completed" : "failed";
    }

    /* Build result JSON */
    int result_len = plexus_json_build_result(
        s_json_buffer, PLEXUS_JSON_BUFFER_SIZE,
        status, exit_code, output, error_str
    );

    if (result_len < 0) {
#if PLEXUS_DEBUG
        plexus_hal_log("Failed to build result JSON");
#endif
        return PLEXUS_ERR_JSON;
    }

    /* Build result URL */
    char result_url[512];
    {
        const char* api_pos = strstr(client->endpoint, "/api/ingest");
        size_t base_len;
        if (api_pos) {
            base_len = (size_t)(api_pos - client->endpoint);
        } else {
            base_len = strlen(client->endpoint);
            while (base_len > 0 && client->endpoint[base_len - 1] == '/') {
                base_len--;
            }
        }

        int written = snprintf(result_url, sizeof(result_url),
            "%.*s/api/commands/%s/result",
            (int)base_len, client->endpoint, cmd.id);

        if (written < 0 || written >= (int)sizeof(result_url)) {
            return PLEXUS_ERR_HAL;
        }
    }

    /* POST result */
    err = plexus_hal_http_post(result_url, client->api_key,
                                s_json_buffer, (size_t)result_len);

#if PLEXUS_DEBUG
    if (err == PLEXUS_OK) {
        plexus_hal_log("Command result posted: %s (exit=%d)", cmd.id, exit_code);
    } else {
        plexus_hal_log("Failed to post command result: %s", plexus_strerror(err));
    }
#endif

    return err;
}

#endif /* PLEXUS_ENABLE_COMMANDS */
