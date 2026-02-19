/**
 * @file plexus_register.c
 * @brief Auto-registration for Plexus C SDK
 *
 * When PLEXUS_ENABLE_AUTO_REGISTER is set, devices can self-register
 * with the Plexus server to create a source entry. The server-assigned
 * source_id is persisted for subsequent boots. Authentication uses
 * the API key directly — no device token is stored.
 *
 * Flow:
 * 1. POST /api/sources/register → receive source_id
 * 2. Store source_id to flash via HAL storage
 * 3. On subsequent boots: load source_id from flash, skip registration
 */

#include "plexus_internal.h"

#if PLEXUS_ENABLE_AUTO_REGISTER

#include <string.h>
#include <stdio.h>

/* Storage key for persistent source_id */
#define PLEXUS_SID_KEY   "plexus_sid"

plexus_err_t plexus_set_device_identity(plexus_client_t* client,
                                         const char* hostname,
                                         const char* platform_name) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;

    if (hostname) {
        strncpy(client->hostname, hostname, PLEXUS_MAX_METADATA_LEN - 1);
        client->hostname[PLEXUS_MAX_METADATA_LEN - 1] = '\0';
    }
    if (platform_name) {
        strncpy(client->platform_name, platform_name, PLEXUS_MAX_METADATA_LEN - 1);
        client->platform_name[PLEXUS_MAX_METADATA_LEN - 1] = '\0';
    }

    return PLEXUS_OK;
}

plexus_err_t plexus_register_device(plexus_client_t* client) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;

    /* No-op if already registered */
    if (client->registered) return PLEXUS_OK;

    /* Build registration URL: replace /api/ingest with /api/sources/register */
    char reg_url[512];
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

        int written = snprintf(reg_url, sizeof(reg_url),
            "%.*s/api/sources/register", (int)base_len, client->endpoint);
        if (written < 0 || written >= (int)sizeof(reg_url)) {
            return PLEXUS_ERR_HAL;
        }
    }

    /* Build registration JSON in first half of json_buffer */
    int body_len = plexus_json_build_register(
        client->json_buffer, PLEXUS_JSON_BUFFER_SIZE / 2,
        client->source_id, client->hostname, client->platform_name);
    if (body_len < 0) {
        return PLEXUS_ERR_JSON;
    }

    /* Use second half of json_buffer for response */
    char* response_buf = client->json_buffer + (PLEXUS_JSON_BUFFER_SIZE / 2);
    size_t response_buf_size = PLEXUS_JSON_BUFFER_SIZE / 2;
    size_t response_len = 0;

    plexus_err_t err = plexus_hal_http_post_response(
        reg_url, client->api_key, PLEXUS_USER_AGENT,
        client->json_buffer, (size_t)body_len,
        response_buf, response_buf_size, &response_len);

    if (err != PLEXUS_OK) {
#if PLEXUS_DEBUG
        plexus_hal_log("Registration failed: %s", plexus_strerror(err));
#endif
        return err;
    }

    if (response_len == 0) {
        return PLEXUS_ERR_NETWORK;
    }

    /* Parse source_id from response (server may have slugified it) */
    char new_source_id[PLEXUS_MAX_SOURCE_ID_LEN];
    new_source_id[0] = '\0';
    plexus_json_extract_string(response_buf, response_len,
                                "source_id", new_source_id, sizeof(new_source_id));

    /* Update source_id if server returned one */
    if (new_source_id[0] != '\0' && plexus_internal_is_url_safe(new_source_id)) {
        strncpy(client->source_id, new_source_id, PLEXUS_MAX_SOURCE_ID_LEN - 1);
        client->source_id[PLEXUS_MAX_SOURCE_ID_LEN - 1] = '\0';
    }

    client->registered = true;

    /* Persist source_id to flash */
#if PLEXUS_ENABLE_PERSISTENT_BUFFER
    plexus_hal_storage_write(PLEXUS_SID_KEY, client->source_id,
                              strlen(client->source_id) + 1);
#endif

#if PLEXUS_DEBUG
    plexus_hal_log("Registered as %s", client->source_id);
#endif

    return PLEXUS_OK;
}

bool plexus_is_registered(const plexus_client_t* client) {
    if (!client || !client->initialized) return false;
    return client->registered;
}

#endif /* PLEXUS_ENABLE_AUTO_REGISTER */
