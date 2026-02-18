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

int plexus_json_serialize(const plexus_client_t* client, char* buf, size_t buf_size);

#if PLEXUS_ENABLE_COMMANDS
int plexus_json_parse_command(const char* json, size_t json_len,
                               plexus_command_t* cmd);
int plexus_json_build_result(char* buf, size_t buf_size,
                              const char* status, int exit_code,
                              const char* output, const char* error);
#endif

#endif /* PLEXUS_INTERNAL_H */
