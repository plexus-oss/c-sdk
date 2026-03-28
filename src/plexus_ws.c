/**
 * @file plexus_ws.c
 * @brief WebSocket state machine for Plexus C SDK
 *
 * Implements the cooperative tick-based WebSocket lifecycle:
 * DISCONNECTED → CONNECTING → AUTHENTICATING → CONNECTED → RECONNECTING
 *
 * The HAL layer (esp_websocket_client on ESP32) handles async I/O.
 * This module processes events synchronously when plexus_tick() is called.
 */

#include "plexus_internal.h"
#include "plexus_ws.h"

#if PLEXUS_ENABLE_WEBSOCKET

#include <string.h>
#include <stdio.h>

/* ========================================================================= */
/* Memory barrier helpers for SPSC ring buffer                               */
/*                                                                           */
/* The command queue is written by the HAL callback (producer, runs in the   */
/* platform's event task) and read by plexus_ws_tick (consumer, runs in the  */
/* app task). On multi-core targets or with write buffers, we need release/  */
/* acquire barriers to ensure the consumer sees the data the producer wrote. */
/* ========================================================================= */

#if defined(__GNUC__) || defined(__clang__)
    /* GCC/Clang atomic builtins — work on ARM, Xtensa, x86 */
    #define PLEXUS_STORE_RELEASE(ptr, val) __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
    #define PLEXUS_LOAD_ACQUIRE(ptr)       __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#else
    /* Fallback: volatile read/write (correct on single-core, best-effort on multi-core) */
    #define PLEXUS_STORE_RELEASE(ptr, val) (*(volatile typeof(*(ptr))*)(ptr) = (val))
    #define PLEXUS_LOAD_ACQUIRE(ptr)       (*(volatile typeof(*(ptr))*)(ptr))
#endif

/* Reuse tick_elapsed from plexus.c — check if a deadline has passed */
static bool ws_tick_elapsed(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

/* Simple jitter: ±25% of the base value */
static uint32_t apply_jitter(uint32_t base_ms) {
    /* Use tick counter as a cheap pseudo-random source */
    uint32_t tick = plexus_hal_get_tick_ms();
    uint32_t jitter_range = base_ms / 4;  /* 25% */
    if (jitter_range == 0) return base_ms;
    uint32_t jitter = tick % (jitter_range * 2);  /* 0 to 50% */
    return base_ms - jitter_range + jitter;  /* -25% to +25% */
}

/* ========================================================================= */
/* Event flags — set by HAL callback, consumed by tick                       */
/* ========================================================================= */

/*
 * Event flags are stored directly in the client struct (ws_evt_* fields)
 * to avoid global state and support multiple clients. The HAL callback
 * writes them (from ESP event task), plexus_ws_tick() reads and clears
 * them (from app task). volatile for cross-task visibility.
 */

/* ========================================================================= */
/* HAL event callback                                                        */
/* ========================================================================= */

void plexus_ws_event_handler(plexus_ws_event_t event,
                              const char* data, size_t data_len,
                              void* user_data) {
    plexus_client_t* client = (plexus_client_t*)user_data;
    (void)data_len;

    switch (event) {
        case PLEXUS_WS_EVENT_CONNECTED:
            client->ws_evt_connected = true;
            break;

        case PLEXUS_WS_EVENT_DISCONNECTED:
            client->ws_evt_disconnected = true;
            break;

        case PLEXUS_WS_EVENT_ERROR:
            client->ws_evt_error = true;
            break;

        case PLEXUS_WS_EVENT_DATA:
            if (!data || data_len == 0) break;

            /* Parse message type and route */
            {
                char msg_type[32] = {0};
                if (!plexus_json_find_string(data, "type", msg_type, sizeof(msg_type))) {
                    break;
                }

                if (strcmp(msg_type, "authenticated") == 0) {
                    /* Auth success — transition handled by tick */
                    client->ws_evt_authenticated = true;

                } else if (strcmp(msg_type, "typed_command") == 0) {
                    /* Enqueue command into SPSC ring buffer (producer side) */
                    uint8_t head = client->ws_cmd_head;
                    uint8_t next_head = (head + 1) % PLEXUS_COMMAND_QUEUE_SIZE;
                    if (next_head != PLEXUS_LOAD_ACQUIRE(&client->ws_cmd_tail)) {
                        plexus_cmd_msg_t* slot = &client->ws_cmd_queue[head];

                        plexus_json_find_string(data, "id", slot->id, sizeof(slot->id));
                        plexus_json_find_string(data, "command", slot->command, sizeof(slot->command));
                        plexus_json_find_value(data, "params", slot->params_json, sizeof(slot->params_json));

                        /* Release: ensure slot data is visible before head advances */
                        PLEXUS_STORE_RELEASE(&client->ws_cmd_head, next_head);
                    }
#if PLEXUS_DEBUG
                    else {
                        plexus_hal_log("plexus_ws: command queue full, dropping command");
                    }
#endif

                } else if (strcmp(msg_type, "error") == 0) {
#if PLEXUS_DEBUG
                    char detail[128] = {0};
                    plexus_json_find_string(data, "detail", detail, sizeof(detail));
                    plexus_hal_log("plexus_ws: server error: %s", detail);
#endif
                }
            }
            break;
    }
}

/* ========================================================================= */
/* State initialization                                                      */
/* ========================================================================= */

void plexus_ws_init_state(plexus_client_t* client) {
    client->ws_state = PLEXUS_WS_DISCONNECTED;
    client->ws_handle = NULL;
    client->ws_endpoint[0] = '\0';
    client->org_id[0] = '\0';
    client->ws_reconnect_backoff_ms = 0;
    client->ws_reconnect_deadline = 0;
    client->ws_stable_since = 0;
    client->ws_reconnect_count = 0;
    client->ws_last_heartbeat_ms = 0;
    client->ws_cmd_head = 0;
    client->ws_cmd_tail = 0;
    client->ws_command_count = 0;
    client->ws_telemetry_enabled = true;
    client->http_persist_enabled = false;
    client->ws_evt_connected = false;
    client->ws_evt_disconnected = false;
    client->ws_evt_error = false;
    client->ws_evt_authenticated = false;
}

void plexus_ws_cleanup(plexus_client_t* client) {
    if (client->ws_handle) {
        plexus_hal_ws_close(client->ws_handle);
        client->ws_handle = NULL;
    }
    client->ws_state = PLEXUS_WS_DISCONNECTED;
}

/* ========================================================================= */
/* Connection helpers                                                        */
/* ========================================================================= */

static plexus_err_t ws_build_url(plexus_client_t* client) {
    /* Build: wss://realtime.plexusrt.dev/party/{org_id} */
    if (client->ws_endpoint[0] == '\0') {
        int len = snprintf(client->ws_endpoint, sizeof(client->ws_endpoint),
                           "%s%s", PLEXUS_WS_ENDPOINT, client->org_id);
        if (len < 0 || len >= (int)sizeof(client->ws_endpoint)) {
            return PLEXUS_ERR_STRING_TOO_LONG;
        }
    }
    return PLEXUS_OK;
}

static void ws_start_connect(plexus_client_t* client) {
    client->ws_evt_connected = false;
    client->ws_evt_disconnected = false;
    client->ws_evt_error = false;
    client->ws_evt_authenticated = false;

    client->ws_handle = plexus_hal_ws_connect(
        client->ws_endpoint,
        plexus_ws_event_handler,
        client
    );

    if (client->ws_handle) {
        client->ws_state = PLEXUS_WS_CONNECTING;
#if PLEXUS_DEBUG
        plexus_hal_log("plexus_ws: connecting to %s", client->ws_endpoint);
#endif
    } else {
        /* HAL connect failed immediately — go to reconnect */
        client->ws_state = PLEXUS_WS_RECONNECTING;
        client->ws_reconnect_deadline = plexus_hal_get_tick_ms() +
            apply_jitter(PLEXUS_WS_RECONNECT_BASE_MS);
    }
}

static void ws_send_auth(plexus_client_t* client) {
    int len = plexus_json_serialize_ws_auth(client, client->json_buffer,
                                             sizeof(client->json_buffer));
    if (len > 0) {
        plexus_hal_ws_send(client->ws_handle, client->json_buffer, (size_t)len);
        client->ws_state = PLEXUS_WS_AUTHENTICATING;
        /* Set auth timeout deadline */
        client->ws_reconnect_deadline = plexus_hal_get_tick_ms() +
            PLEXUS_WS_AUTH_TIMEOUT_MS;
#if PLEXUS_DEBUG
        plexus_hal_log("plexus_ws: auth sent, waiting for response");
#endif
    }
}

static void ws_send_heartbeat(plexus_client_t* client) {
    int len = plexus_json_serialize_ws_heartbeat(client, client->json_buffer,
                                                  sizeof(client->json_buffer));
    if (len > 0) {
        plexus_hal_ws_send(client->ws_handle, client->json_buffer, (size_t)len);
    }
    client->ws_last_heartbeat_ms = plexus_hal_get_tick_ms();
}

static void ws_enter_reconnect(plexus_client_t* client) {
    if (client->ws_handle) {
        plexus_hal_ws_close(client->ws_handle);
        client->ws_handle = NULL;
    }

    client->ws_state = PLEXUS_WS_RECONNECTING;
    client->ws_reconnect_count++;

    /* Exponential backoff: base * 2^count, capped at max */
    uint32_t backoff = PLEXUS_WS_RECONNECT_BASE_MS;
    for (uint16_t i = 0; i < client->ws_reconnect_count && backoff < PLEXUS_WS_RECONNECT_MAX_MS; i++) {
        backoff *= 2;
    }
    if (backoff > PLEXUS_WS_RECONNECT_MAX_MS) {
        backoff = PLEXUS_WS_RECONNECT_MAX_MS;
    }

    client->ws_reconnect_backoff_ms = backoff;
    client->ws_reconnect_deadline = plexus_hal_get_tick_ms() + apply_jitter(backoff);

#if PLEXUS_DEBUG
    plexus_hal_log("plexus_ws: reconnecting in %lu ms (attempt %u)",
                   (unsigned long)backoff, (unsigned)client->ws_reconnect_count);
#endif
}

/* ========================================================================= */
/* Command dispatch                                                          */
/* ========================================================================= */

static void ws_dispatch_commands(plexus_client_t* client) {
    /* SPSC ring buffer consumer — acquire head to see producer's writes */
    while (client->ws_cmd_tail != PLEXUS_LOAD_ACQUIRE(&client->ws_cmd_head)) {
        plexus_cmd_msg_t* msg = &client->ws_cmd_queue[client->ws_cmd_tail];

        /* Find registered handler */
        bool found = false;
        for (uint8_t i = 0; i < client->ws_command_count; i++) {
            if (strcmp(client->ws_commands[i].name, msg->command) == 0) {
                /* Send ACK */
                {
                    char ack_buf[192];
                    int ack_len = snprintf(ack_buf, sizeof(ack_buf),
                        "{\"type\":\"command_result\",\"id\":\"%.30s\",\"event\":\"ack\",\"command\":\"%.30s\"}",
                        msg->id, msg->command);
                    if (ack_len > 0 && ack_len < (int)sizeof(ack_buf)) {
                        plexus_hal_ws_send(client->ws_handle, ack_buf, (size_t)ack_len);
                    }
                }

                /* Store command name for plexus_command_respond() */
                strncpy(client->ws_last_cmd_name, msg->command,
                        PLEXUS_MAX_COMMAND_NAME_LEN - 1);
                client->ws_last_cmd_name[PLEXUS_MAX_COMMAND_NAME_LEN - 1] = '\0';

                /* Call handler */
                client->ws_commands[i].handler(
                    msg->id, msg->params_json, client->ws_commands[i].user_data);

                found = true;
                break;
            }
        }

        if (!found) {
            /* Auto-respond with error for unknown commands */
            strncpy(client->ws_last_cmd_name, msg->command,
                    PLEXUS_MAX_COMMAND_NAME_LEN - 1);
            client->ws_last_cmd_name[PLEXUS_MAX_COMMAND_NAME_LEN - 1] = '\0';
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg), "Unknown command: %.64s", msg->command);
            plexus_command_respond(client, msg->id, NULL, err_msg);
        }

        /* Release: advance tail after we're done reading the slot */
        PLEXUS_STORE_RELEASE(&client->ws_cmd_tail,
                              (client->ws_cmd_tail + 1) % PLEXUS_COMMAND_QUEUE_SIZE);
    }
}

/* ========================================================================= */
/* Main state machine tick                                                   */
/* ========================================================================= */

void plexus_ws_tick(plexus_client_t* client) {
    uint32_t now = plexus_hal_get_tick_ms();

    switch (client->ws_state) {
        case PLEXUS_WS_DISCONNECTED:
            /* Nothing to do — user must call plexus_ws_connect() */
            break;

        case PLEXUS_WS_CONNECTING:
            if (client->ws_evt_connected) {
                client->ws_evt_connected = false;
                /* TCP connected — send auth */
                ws_send_auth(client);
            }
            if (client->ws_evt_disconnected || client->ws_evt_error) {
                client->ws_evt_disconnected = false;
                client->ws_evt_error = false;
                ws_enter_reconnect(client);
            }
            break;

        case PLEXUS_WS_AUTHENTICATING:
            if (client->ws_evt_authenticated) {
                client->ws_evt_authenticated = false;
                client->ws_state = PLEXUS_WS_CONNECTED;
                client->ws_stable_since = now;
                client->ws_last_heartbeat_ms = now;
                /* Reset reconnect count after stable connection */
                client->ws_reconnect_count = 0;
                client->ws_reconnect_backoff_ms = 0;
#if PLEXUS_DEBUG
                plexus_hal_log("plexus_ws: authenticated, connected");
#endif
            }
            if (client->ws_evt_disconnected || client->ws_evt_error) {
                client->ws_evt_disconnected = false;
                client->ws_evt_error = false;
                ws_enter_reconnect(client);
            }
            /* Auth timeout */
            if (ws_tick_elapsed(now, client->ws_reconnect_deadline)) {
#if PLEXUS_DEBUG
                plexus_hal_log("plexus_ws: auth timeout");
#endif
                ws_enter_reconnect(client);
            }
            break;

        case PLEXUS_WS_CONNECTED:
            /* Check for disconnection */
            if (client->ws_evt_disconnected || client->ws_evt_error) {
                client->ws_evt_disconnected = false;
                client->ws_evt_error = false;
                ws_enter_reconnect(client);
                break;
            }

            /* Heartbeat */
            if (ws_tick_elapsed(now, client->ws_last_heartbeat_ms +
                                     PLEXUS_WS_HEARTBEAT_INTERVAL_MS)) {
                ws_send_heartbeat(client);
            }

            /* Dispatch queued commands */
            ws_dispatch_commands(client);
            break;

        case PLEXUS_WS_RECONNECTING:
            /* Wait for backoff to expire */
            if (ws_tick_elapsed(now, client->ws_reconnect_deadline)) {
                ws_start_connect(client);
            }
            break;
    }
}

/* ========================================================================= */
/* Telemetry send over WebSocket                                             */
/* ========================================================================= */

plexus_err_t plexus_ws_send_telemetry(plexus_client_t* client) {
    if (client->ws_state != PLEXUS_WS_CONNECTED) {
        return PLEXUS_ERR_WS_NOT_CONNECTED;
    }

    int len = plexus_json_serialize_ws_telemetry(client, client->json_buffer,
                                                  sizeof(client->json_buffer));
    if (len <= 0) {
        return PLEXUS_ERR_JSON;
    }

    plexus_err_t err = plexus_hal_ws_send(client->ws_handle,
                                           client->json_buffer, (size_t)len);
    if (err != PLEXUS_OK) {
        return err;
    }

    return PLEXUS_OK;
}

/* ========================================================================= */
/* Public API implementations                                                */
/* ========================================================================= */

plexus_err_t plexus_set_org_id(plexus_client_t* client, const char* org_id) {
    if (!client || !org_id) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;
    if (strlen(org_id) >= PLEXUS_MAX_ORG_ID_LEN) return PLEXUS_ERR_STRING_TOO_LONG;

    strncpy(client->org_id, org_id, PLEXUS_MAX_ORG_ID_LEN - 1);
    client->org_id[PLEXUS_MAX_ORG_ID_LEN - 1] = '\0';
    /* Reset endpoint so it gets rebuilt with new org_id */
    client->ws_endpoint[0] = '\0';
    return PLEXUS_OK;
}

plexus_err_t plexus_ws_connect(plexus_client_t* client) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;
    if (client->org_id[0] == '\0') return PLEXUS_ERR_INVALID_ARG;

    /* Build WS URL if not set */
    plexus_err_t err = ws_build_url(client);
    if (err != PLEXUS_OK) return err;

    /* Start connection */
    ws_start_connect(client);
    return PLEXUS_OK;
}

plexus_err_t plexus_ws_disconnect(plexus_client_t* client) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;

    plexus_ws_cleanup(client);
    client->ws_evt_connected = false;
    client->ws_evt_disconnected = false;
    client->ws_evt_error = false;
    client->ws_evt_authenticated = false;
    return PLEXUS_OK;
}

plexus_ws_state_t plexus_ws_state(const plexus_client_t* client) {
    if (!client || !client->initialized) return PLEXUS_WS_DISCONNECTED;
    return client->ws_state;
}

plexus_err_t plexus_set_ws_telemetry(plexus_client_t* client, bool enabled) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;
    client->ws_telemetry_enabled = enabled;
    return PLEXUS_OK;
}

plexus_err_t plexus_set_http_persist(plexus_client_t* client, bool enabled) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;
    client->http_persist_enabled = enabled;
    return PLEXUS_OK;
}

/* --- Command registration --- */

plexus_err_t plexus_command_register(plexus_client_t* client, const char* name,
                                      const char* description,
                                      plexus_command_handler_t handler,
                                      void* user_data,
                                      const plexus_param_t* params,
                                      uint8_t param_count) {
    if (!client || !name || !handler) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;
    if (client->ws_command_count >= PLEXUS_MAX_COMMANDS) return PLEXUS_ERR_COMMAND_FULL;
    if (strlen(name) >= PLEXUS_MAX_COMMAND_NAME_LEN) return PLEXUS_ERR_STRING_TOO_LONG;
    if (param_count > PLEXUS_MAX_COMMAND_PARAMS) return PLEXUS_ERR_INVALID_ARG;

    plexus_cmd_reg_t* reg = &client->ws_commands[client->ws_command_count];
    strncpy(reg->name, name, PLEXUS_MAX_COMMAND_NAME_LEN - 1);
    reg->name[PLEXUS_MAX_COMMAND_NAME_LEN - 1] = '\0';
    reg->description[0] = '\0';
    if (description) {
        strncpy(reg->description, description, sizeof(reg->description) - 1);
        reg->description[sizeof(reg->description) - 1] = '\0';
    }
    reg->handler = handler;
    reg->user_data = user_data;
    reg->param_count = param_count;

    if (params && param_count > 0) {
        memcpy(reg->params, params, sizeof(plexus_param_t) * param_count);
    }

    client->ws_command_count++;
    return PLEXUS_OK;
}

plexus_err_t plexus_command_respond(plexus_client_t* client, const char* cmd_id,
                                     const char* result_json, const char* error) {
    if (!client || !cmd_id) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;
    if (client->ws_state != PLEXUS_WS_CONNECTED) return PLEXUS_ERR_WS_NOT_CONNECTED;

    int len = plexus_json_serialize_command_result(
        cmd_id, client->ws_last_cmd_name,
        result_json, error,
        client->json_buffer, sizeof(client->json_buffer));
    if (len <= 0) return PLEXUS_ERR_JSON;

    return plexus_hal_ws_send(client->ws_handle, client->json_buffer, (size_t)len);
}

/* --- Param helpers --- */

plexus_param_t plexus_param_float(const char* name, double min, double max) {
    plexus_param_t p = {0};
    if (name) {
        strncpy(p.name, name, PLEXUS_MAX_COMMAND_NAME_LEN - 1);
    }
    p.type = PLEXUS_PARAM_FLOAT;
    p.min = min;
    p.max = max;
    p.default_val = 0.0 / 0.0;  /* NAN = required */
    p.required = true;
    return p;
}

plexus_param_t plexus_param_int(const char* name, double min, double max) {
    plexus_param_t p = {0};
    if (name) {
        strncpy(p.name, name, PLEXUS_MAX_COMMAND_NAME_LEN - 1);
    }
    p.type = PLEXUS_PARAM_INT;
    p.min = min;
    p.max = max;
    p.default_val = 0.0 / 0.0;  /* NAN */
    p.required = true;
    return p;
}

plexus_param_t plexus_param_bool(const char* name) {
    plexus_param_t p = {0};
    if (name) {
        strncpy(p.name, name, PLEXUS_MAX_COMMAND_NAME_LEN - 1);
    }
    p.type = PLEXUS_PARAM_BOOL;
    p.min = 0;
    p.max = 0;
    p.default_val = 0.0 / 0.0;  /* NAN */
    p.required = true;
    return p;
}

#endif /* PLEXUS_ENABLE_WEBSOCKET */
