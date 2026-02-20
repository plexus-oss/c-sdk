/**
 * @file plexus_internal.h
 * @brief Private declarations for Plexus C SDK implementation
 *
 * NOT part of the public API. Included only by SDK source files
 * (plexus.c, plexus_json.c) and test code.
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
 * Used for source_id validation to prevent URL injection.
 */
bool plexus_internal_is_url_safe(const char* s);

int plexus_json_serialize(const plexus_client_t* client, char* buf, size_t buf_size);

#endif /* PLEXUS_INTERNAL_H */
