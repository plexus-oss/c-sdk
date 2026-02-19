/**
 * @file plexus_internal.h
 * @brief Private declarations for Plexus C SDK implementation
 *
 * NOT part of the public API. Included only by SDK source files
 * (plexus.c, plexus_json.c, plexus_commands.c) and test code.
 */

#ifndef PLEXUS_INTERNAL_H
#define PLEXUS_INTERNAL_H

#include "plexus.h"

/* Single definition of User-Agent string used by all source files */
#define PLEXUS_USER_AGENT "plexus-c-sdk/" PLEXUS_SDK_VERSION

/* ------------------------------------------------------------------------- */
/* Internal function declarations                                            */
/* ------------------------------------------------------------------------- */

/**
 * Validate that a string contains only URL-safe characters: [a-zA-Z0-9._-]
 * Used for source_id validation and command ID validation to prevent URL injection.
 */
bool plexus_internal_is_url_safe(const char* s);

int plexus_json_serialize(const plexus_client_t* client, char* buf, size_t buf_size);

/* General-purpose JSON parse utilities (used by commands + auto-registration) */
int plexus_json_extract_string(const char* json, size_t json_len,
                                const char* key, char* out, size_t out_size);
int plexus_json_extract_int(const char* json, size_t json_len,
                             const char* key, int default_val);

#if PLEXUS_ENABLE_COMMANDS
int plexus_json_parse_command(const char* json, size_t json_len,
                               plexus_command_t* cmd);
int plexus_json_build_result(char* buf, size_t buf_size,
                              const char* status, int exit_code,
                              const char* output, const char* error);
#endif

#if PLEXUS_ENABLE_HEARTBEAT
int plexus_json_build_heartbeat(const plexus_client_t* client, char* buf, size_t buf_size);
#endif

#if PLEXUS_ENABLE_AUTO_REGISTER
int plexus_json_build_register(char* buf, size_t buf_size,
                                const char* source_id,
                                const char* hostname,
                                const char* platform_name);
#endif

/* Typed command schema serialization is implemented in plexus_typed_commands.c
 * and exposed via the public API (plexus_typed_commands_schema).
 * No additional internal declarations needed. */

#endif /* PLEXUS_INTERNAL_H */
