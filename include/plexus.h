/**
 * @file plexus.h
 * @brief Plexus C SDK - Minimal footprint telemetry client for embedded devices
 *
 * This SDK provides a simple interface for sending telemetry data from
 * ESP32, STM32, and Arduino devices to the Plexus ingest API.
 *
 * Target: ~2KB RAM footprint
 *
 * @example
 *   plexus_client_t* client = plexus_init("plx_xxx", "device-001");
 *   plexus_send_number(client, "temperature", 72.5);
 *   plexus_send_number(client, "humidity", 45.0);
 *   plexus_flush(client);
 *   plexus_free(client);
 */

#ifndef PLEXUS_H
#define PLEXUS_H

#include "plexus_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Error codes                                                               */
/* ------------------------------------------------------------------------- */

typedef enum {
    PLEXUS_OK = 0,              /* Success */
    PLEXUS_ERR_NULL_PTR,        /* Null pointer argument */
    PLEXUS_ERR_BUFFER_FULL,     /* Metric buffer is full */
    PLEXUS_ERR_STRING_TOO_LONG, /* String exceeds max length */
    PLEXUS_ERR_NO_DATA,         /* No data to flush */
    PLEXUS_ERR_NETWORK,         /* Network/HTTP error */
    PLEXUS_ERR_AUTH,            /* Authentication failed (401) */
    PLEXUS_ERR_RATE_LIMIT,      /* Rate limited (429) */
    PLEXUS_ERR_SERVER,          /* Server error (5xx) */
    PLEXUS_ERR_JSON,            /* JSON serialization error */
    PLEXUS_ERR_NOT_INITIALIZED, /* Client not initialized */
    PLEXUS_ERR_HAL,             /* HAL layer error */
} plexus_err_t;

/* ------------------------------------------------------------------------- */
/* Value types                                                               */
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
/* Metric structure                                                          */
/* ------------------------------------------------------------------------- */

typedef struct {
    char name[PLEXUS_MAX_METRIC_NAME_LEN];
    plexus_value_t value;
    uint64_t timestamp_ms;  /* Unix timestamp in milliseconds (0 = use server time) */
#if PLEXUS_ENABLE_TAGS
    char tag_keys[4][32];   /* Up to 4 tags */
    char tag_values[4][32];
    uint8_t tag_count;
#endif
} plexus_metric_t;

/* ------------------------------------------------------------------------- */
/* Client structure                                                          */
/* ------------------------------------------------------------------------- */

typedef struct plexus_client plexus_client_t;

/* Forward declare for command handler */
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
    char endpoint[256];

    plexus_metric_t metrics[PLEXUS_MAX_METRICS];
    uint16_t metric_count;

    uint32_t last_flush_ms;
    uint32_t total_sent;
    uint32_t total_errors;

    bool initialized;

#if PLEXUS_ENABLE_COMMANDS
    plexus_command_handler_fn command_handler;
    uint32_t last_command_poll_ms;
#endif
};

/* ------------------------------------------------------------------------- */
/* Core API                                                                  */
/* ------------------------------------------------------------------------- */

/**
 * Initialize a Plexus client
 *
 * @param api_key   Your Plexus API key (e.g., "plx_xxxxx")
 * @param source_id Device/source identifier (e.g., "drone-001")
 * @return          Pointer to client, or NULL on failure
 *
 * @note The returned client must be freed with plexus_free()
 */
plexus_client_t* plexus_init(const char* api_key, const char* source_id);

/**
 * Free a Plexus client and release resources
 *
 * @param client Client to free (safe to pass NULL)
 */
void plexus_free(plexus_client_t* client);

/**
 * Set custom ingest endpoint URL
 *
 * @param client   Plexus client
 * @param endpoint Full URL (e.g., "https://custom.domain/api/ingest")
 * @return         PLEXUS_OK on success
 */
plexus_err_t plexus_set_endpoint(plexus_client_t* client, const char* endpoint);

/* ------------------------------------------------------------------------- */
/* Send metrics                                                              */
/* ------------------------------------------------------------------------- */

/**
 * Queue a numeric metric for sending
 *
 * @param client Plexus client
 * @param metric Metric name (e.g., "temperature")
 * @param value  Numeric value
 * @return       PLEXUS_OK on success, PLEXUS_ERR_BUFFER_FULL if buffer full
 *
 * @note Call plexus_flush() to send queued metrics
 */
plexus_err_t plexus_send_number(plexus_client_t* client, const char* metric, double value);

/**
 * Queue a numeric metric with timestamp
 *
 * @param client       Plexus client
 * @param metric       Metric name
 * @param value        Numeric value
 * @param timestamp_ms Unix timestamp in milliseconds
 * @return             PLEXUS_OK on success
 */
plexus_err_t plexus_send_number_ts(plexus_client_t* client, const char* metric,
                                    double value, uint64_t timestamp_ms);

#if PLEXUS_ENABLE_STRING_VALUES
/**
 * Queue a string metric for sending
 *
 * @param client Plexus client
 * @param metric Metric name
 * @param value  String value
 * @return       PLEXUS_OK on success
 */
plexus_err_t plexus_send_string(plexus_client_t* client, const char* metric, const char* value);
#endif

#if PLEXUS_ENABLE_BOOL_VALUES
/**
 * Queue a boolean metric for sending
 *
 * @param client Plexus client
 * @param metric Metric name
 * @param value  Boolean value
 * @return       PLEXUS_OK on success
 */
plexus_err_t plexus_send_bool(plexus_client_t* client, const char* metric, bool value);
#endif

#if PLEXUS_ENABLE_TAGS
/**
 * Queue a numeric metric with tags
 *
 * @param client     Plexus client
 * @param metric     Metric name
 * @param value      Numeric value
 * @param tag_keys   Array of tag keys (NULL-terminated or up to 4)
 * @param tag_values Array of tag values
 * @param tag_count  Number of tags
 * @return           PLEXUS_OK on success
 */
plexus_err_t plexus_send_number_tagged(plexus_client_t* client, const char* metric,
                                        double value, const char** tag_keys,
                                        const char** tag_values, uint8_t tag_count);
#endif

/* ------------------------------------------------------------------------- */
/* Flush & network operations                                                */
/* ------------------------------------------------------------------------- */

/**
 * Send all queued metrics to the Plexus API
 *
 * @param client Plexus client
 * @return       PLEXUS_OK on success, error code on failure
 *
 * @note On success, the metric buffer is cleared
 * @note On network error, metrics remain in buffer for retry
 */
plexus_err_t plexus_flush(plexus_client_t* client);

/**
 * Check if time-based auto-flush is needed and flush if so
 *
 * Call this periodically from your main loop to enable time-based flushing.
 * The flush interval is configured by PLEXUS_AUTO_FLUSH_INTERVAL_MS.
 *
 * @param client Plexus client
 * @return       PLEXUS_OK if no flush needed or flush succeeded,
 *               PLEXUS_ERR_NO_DATA if no data to flush,
 *               other error code on flush failure
 *
 * @example
 *   // In main loop:
 *   while (1) {
 *       plexus_tick(client);  // Check for time-based flush
 *       // ... other tasks ...
 *   }
 */
plexus_err_t plexus_tick(plexus_client_t* client);

/**
 * Get number of queued metrics
 *
 * @param client Plexus client
 * @return       Number of metrics in buffer
 */
uint16_t plexus_pending_count(const plexus_client_t* client);

/**
 * Clear all queued metrics without sending
 *
 * @param client Plexus client
 */
void plexus_clear(plexus_client_t* client);

/* ------------------------------------------------------------------------- */
/* Command support (opt-in via PLEXUS_ENABLE_COMMANDS)                       */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_COMMANDS

/**
 * Command received from the server
 */
typedef struct {
    char id[64];                              /* Command UUID */
    char command[PLEXUS_MAX_COMMAND_LEN];     /* Command string to execute */
    int timeout_seconds;                       /* Execution timeout */
} plexus_command_t;

/**
 * Command handler callback
 *
 * The user implements this callback to execute received commands.
 * Return the output string (or NULL) and set exit_code.
 *
 * @param cmd       Command to execute
 * @param output    Buffer to write output into (up to PLEXUS_MAX_COMMAND_RESULT_LEN)
 * @param exit_code Set to the exit code of the command
 * @return          PLEXUS_OK on success (even if command itself failed)
 */
typedef plexus_err_t (*plexus_command_handler_t)(
    const plexus_command_t* cmd,
    char* output,
    int* exit_code
);

/**
 * Register a command handler
 *
 * @param client  Plexus client
 * @param handler Callback invoked when a command is received
 * @return        PLEXUS_OK on success
 */
plexus_err_t plexus_register_command_handler(plexus_client_t* client,
                                              plexus_command_handler_t handler);

/**
 * Poll for queued commands and execute them via the registered handler
 *
 * Call this periodically, or rely on plexus_tick() which calls it automatically
 * at the configured PLEXUS_COMMAND_POLL_INTERVAL_MS interval.
 *
 * @param client Plexus client
 * @return       PLEXUS_OK on success or no commands, error code on failure
 */
plexus_err_t plexus_poll_commands(plexus_client_t* client);

#endif /* PLEXUS_ENABLE_COMMANDS */

/* ------------------------------------------------------------------------- */
/* Utility functions                                                         */
/* ------------------------------------------------------------------------- */

/**
 * Get error message string
 *
 * @param err Error code
 * @return    Human-readable error message
 */
const char* plexus_strerror(plexus_err_t err);

/**
 * Get SDK version string
 *
 * @return Version string (e.g., "1.0.0")
 */
const char* plexus_version(void);

/* ------------------------------------------------------------------------- */
/* HAL interface (implemented per platform)                                  */
/* ------------------------------------------------------------------------- */

/**
 * Platform-specific HTTP POST implementation
 *
 * @param url      Target URL
 * @param api_key  API key for x-api-key header
 * @param body     JSON body to send
 * @param body_len Length of body
 * @return         PLEXUS_OK on 2xx response, error code otherwise
 */
plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* body, size_t body_len);

#if PLEXUS_ENABLE_COMMANDS
/**
 * Platform-specific HTTP GET implementation (for command polling)
 *
 * @param url          Target URL
 * @param api_key      API key for x-api-key header
 * @param response_buf Buffer to write response body into
 * @param buf_size     Size of response buffer
 * @param response_len Output: actual bytes written to response_buf
 * @return             PLEXUS_OK on 2xx response, error code otherwise
 */
plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
                                  char* response_buf, size_t buf_size,
                                  size_t* response_len);
#endif

/**
 * Get current time in milliseconds since epoch
 *
 * @return Unix timestamp in milliseconds, or 0 if unavailable
 */
uint64_t plexus_hal_get_time_ms(void);

/**
 * Get monotonic time in milliseconds (for intervals)
 *
 * @return Milliseconds since boot/start
 */
uint32_t plexus_hal_get_tick_ms(void);

/**
 * Debug logging (optional, can be no-op)
 *
 * @param fmt Printf-style format string
 */
void plexus_hal_log(const char* fmt, ...);

/* ------------------------------------------------------------------------- */
/* Persistent storage HAL (opt-in via PLEXUS_ENABLE_PERSISTENT_BUFFER)       */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_PERSISTENT_BUFFER

/**
 * Write data to persistent storage under a given key
 *
 * @param key   Storage key (e.g., "plexus_buf")
 * @param data  Pointer to data to write
 * @param len   Number of bytes to write
 * @return      PLEXUS_OK on success
 */
plexus_err_t plexus_hal_storage_write(const char* key, const void* data, size_t len);

/**
 * Read data from persistent storage
 *
 * @param key      Storage key
 * @param data     Buffer to read into
 * @param max_len  Size of the output buffer
 * @param out_len  Actual bytes read (set to 0 if key not found)
 * @return         PLEXUS_OK on success, PLEXUS_ERR_HAL if not found or error
 */
plexus_err_t plexus_hal_storage_read(const char* key, void* data, size_t max_len, size_t* out_len);

/**
 * Clear data associated with a key from persistent storage
 *
 * @param key  Storage key to erase
 * @return     PLEXUS_OK on success
 */
plexus_err_t plexus_hal_storage_clear(const char* key);

#endif /* PLEXUS_ENABLE_PERSISTENT_BUFFER */

#ifdef __cplusplus
}
#endif

#endif /* PLEXUS_H */
