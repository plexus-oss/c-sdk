/**
 * @file plexus_ws.h
 * @brief Internal WebSocket declarations for Plexus C SDK
 *
 * NOT part of the public API. Included only by SDK source files.
 */

#ifndef PLEXUS_WS_H
#define PLEXUS_WS_H

#include "plexus.h"

#if PLEXUS_ENABLE_WEBSOCKET

/**
 * Drive the WebSocket state machine.
 * Called from plexus_tick(). Handles:
 *   - Connection initiation and reconnection
 *   - Auth handshake completion
 *   - Heartbeat sending
 *   - Incoming command dispatch
 */
void plexus_ws_tick(plexus_client_t* client);

/**
 * Send telemetry points over WebSocket.
 * Called from plexus_flush() when WS is connected.
 *
 * @return PLEXUS_OK on success, PLEXUS_ERR_WS_NOT_CONNECTED if not connected
 */
plexus_err_t plexus_ws_send_telemetry(plexus_client_t* client);

/**
 * Initialize WebSocket state in client struct.
 * Called from client_init_common().
 */
void plexus_ws_init_state(plexus_client_t* client);

/**
 * Clean up WebSocket resources.
 * Called from plexus_free().
 */
void plexus_ws_cleanup(plexus_client_t* client);

/**
 * Internal HAL event callback — enqueues events for processing by tick.
 * Called from the platform's WS transport context.
 */
void plexus_ws_event_handler(plexus_ws_event_t event,
                              const char* data, size_t data_len,
                              void* user_data);

/* --- JSON serializers for WebSocket messages --- */

int plexus_json_serialize_ws_auth(const plexus_client_t* client, char* buf, size_t buf_size);
int plexus_json_serialize_ws_heartbeat(const plexus_client_t* client, char* buf, size_t buf_size);
int plexus_json_serialize_ws_telemetry(const plexus_client_t* client, char* buf, size_t buf_size);
int plexus_json_serialize_command_result(const char* cmd_id, const char* command_name,
                                          const char* result_json, const char* error,
                                          char* buf, size_t buf_size);

/* --- Minimal JSON field extraction --- */

/**
 * Extract a string field value from a JSON object.
 * Handles simple flat objects only (no nesting).
 *
 * @return true if field found and extracted
 */
bool plexus_json_find_string(const char* json, const char* key, char* out, size_t out_size);

/**
 * Extract a raw object/value field from a JSON object.
 * Returns the raw JSON substring (e.g., the {...} for an object field).
 *
 * @return true if field found and extracted
 */
bool plexus_json_find_value(const char* json, const char* key, char* out, size_t out_size);

#endif /* PLEXUS_ENABLE_WEBSOCKET */

#endif /* PLEXUS_WS_H */
