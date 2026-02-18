/**
 * @file plexus_internal.h
 * @brief Private definitions for Plexus C SDK implementation
 *
 * This header is NOT part of the public API. It is included only by
 * SDK source files (plexus.c, plexus_json.c, plexus_commands.c) and
 * test code. Application code should only include plexus.h.
 */

#ifndef PLEXUS_INTERNAL_H
#define PLEXUS_INTERNAL_H

#include "plexus.h"

/* ------------------------------------------------------------------------- */
/* Value types (internal)                                                    */
/* ------------------------------------------------------------------------- */

typedef enum {
    PLEXUS_VALUE_NUMBER,
    PLEXUS_VALUE_STRING,
    PLEXUS_VALUE_BOOL,
} plexus_value_type_t;

typedef struct {
    plexus_value_type_t type;
    union {
        double number;
        char string[PLEXUS_MAX_STRING_VALUE_LEN];
        bool boolean;
    } data;
} plexus_value_t;

/* ------------------------------------------------------------------------- */
/* Metric structure (internal)                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    char name[PLEXUS_MAX_METRIC_NAME_LEN];
    plexus_value_t value;
    uint64_t timestamp_ms;
#if PLEXUS_ENABLE_TAGS
    char tag_keys[4][32];
    char tag_values[4][32];
    uint8_t tag_count;
#endif
} plexus_metric_t;

/* ------------------------------------------------------------------------- */
/* Client structure (internal)                                               */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_COMMANDS
typedef plexus_err_t (*plexus_command_handler_fn)(
    const void* cmd,
    char* output,
    int* exit_code
);
#endif

struct plexus_client {
    char api_key[PLEXUS_MAX_API_KEY_LEN];
    char source_id[PLEXUS_MAX_SOURCE_ID_LEN];
    char endpoint[PLEXUS_MAX_ENDPOINT_LEN];

    plexus_metric_t metrics[PLEXUS_MAX_METRICS];
    uint16_t metric_count;

    uint32_t last_flush_ms;
    uint32_t total_sent;
    uint32_t total_errors;

    /* Runtime-configurable overrides (0 = use compile-time default) */
    uint32_t flush_interval_ms;
    uint16_t auto_flush_count;

    bool initialized;

    /* Per-client JSON serialization buffer (avoids global state) */
    char json_buffer[PLEXUS_JSON_BUFFER_SIZE];

#if PLEXUS_ENABLE_COMMANDS
    plexus_command_handler_fn command_handler;
    uint32_t last_command_poll_ms;
#endif
};

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
