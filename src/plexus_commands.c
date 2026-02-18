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

#include "plexus_internal.h"

#if PLEXUS_ENABLE_COMMANDS

#include <string.h>
#include <stdio.h>

plexus_err_t plexus_register_command_handler(plexus_client_t* client,
                                              plexus_command_handler_t handler) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;

    client->command_handler = handler;
    return PLEXUS_OK;
}

plexus_err_t plexus_poll_commands(plexus_client_t* client) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;
    if (!client->command_handler) return PLEXUS_OK;

    /* Build poll URL.
     * source_id is validated at init time to contain only [a-zA-Z0-9._-]
     * so it is safe to embed directly in a URL query parameter. */
    char poll_url[512];
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

        int written = snprintf(poll_url, sizeof(poll_url),
            "%.*s/api/commands/poll?sourceId=%s",
            (int)base_len, client->endpoint, client->source_id);

        if (written < 0 || written >= (int)sizeof(poll_url)) {
            return PLEXUS_ERR_HAL;
        }
    }

    /* Poll for commands using client->json_buffer.
     * This is safe because:
     *   1. The SDK is single-threaded per client
     *   2. We fully parse the response into `cmd` (a stack struct) before
     *      the buffer is reused for building the result JSON below */
    size_t response_len = 0;

    plexus_err_t err = plexus_hal_http_get(
        poll_url, client->api_key, PLEXUS_USER_AGENT,
        client->json_buffer, PLEXUS_JSON_BUFFER_SIZE, &response_len
    );

    if (err != PLEXUS_OK) {
#if PLEXUS_DEBUG
        plexus_hal_log("Command poll failed: %s", plexus_strerror(err));
#endif
        return err;
    }

    if (response_len == 0) {
        return PLEXUS_OK;
    }

    /* Parse command from response into stack struct before buffer is reused */
    plexus_command_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    if (plexus_json_parse_command(client->json_buffer, response_len, &cmd) != 0) {
        return PLEXUS_OK;
    }

    if (cmd.id[0] == '\0') {
        return PLEXUS_OK;
    }

    /* Validate command ID before embedding in URL to prevent path injection.
     * A malicious server could send id="../../admin" to redirect the result POST. */
    if (!plexus_internal_is_url_safe(cmd.id)) {
#if PLEXUS_DEBUG
        plexus_hal_log("Rejected command with unsafe id: %.32s", cmd.id);
#endif
        return PLEXUS_ERR_INVALID_ARG;
    }

#if PLEXUS_DEBUG
    plexus_hal_log("Received command: %s (id=%s, timeout=%ds)",
                   cmd.command, cmd.id, cmd.timeout_seconds);
#endif

    /* Execute via user callback */
    char output[PLEXUS_MAX_COMMAND_RESULT_LEN];
    int exit_code = -1;
    memset(output, 0, sizeof(output));

    plexus_err_t exec_err = client->command_handler(&cmd, output, &exit_code);

    const char* status;
    const char* error_str = NULL;
    if (exec_err != PLEXUS_OK) {
        status = "failed";
        error_str = plexus_strerror(exec_err);
    } else {
        status = (exit_code == 0) ? "completed" : "failed";
    }

    /* Build result JSON into client buffer */
    int result_len = plexus_json_build_result(
        client->json_buffer, PLEXUS_JSON_BUFFER_SIZE,
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
    err = plexus_hal_http_post(result_url, client->api_key, PLEXUS_USER_AGENT,
                                client->json_buffer, (size_t)result_len);

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
