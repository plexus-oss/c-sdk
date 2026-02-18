/**
 * @file plexus.h
 * @brief Plexus C SDK - Minimal footprint telemetry client for embedded devices
 *
 * This SDK provides a simple interface for sending telemetry data from
 * ESP32, STM32, and Arduino devices to the Plexus ingest API.
 *
 * Memory usage depends on configuration. See README.md for sizing details.
 *
 * Thread safety: This SDK is NOT thread-safe. Confine all calls for a given
 * client to a single thread/task. Multiple clients in separate tasks is safe.
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
    PLEXUS_ERR__COUNT           /* Sentinel — must be last */
} plexus_err_t;

/* ------------------------------------------------------------------------- */
/* Opaque client handle                                                      */
/* ------------------------------------------------------------------------- */

typedef struct plexus_client plexus_client_t;

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
 * Call plexus_flush() before freeing if you need to ensure all queued
 * metrics are sent. This function does NOT flush automatically — any
 * unsent metrics in the buffer will be discarded.
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

/**
 * Set runtime flush interval (overrides PLEXUS_AUTO_FLUSH_INTERVAL_MS)
 *
 * @param client      Plexus client
 * @param interval_ms Flush interval in milliseconds (0 = disable time-based flush)
 * @return            PLEXUS_OK on success
 */
plexus_err_t plexus_set_flush_interval(plexus_client_t* client, uint32_t interval_ms);

/**
 * Set runtime auto-flush count (overrides PLEXUS_AUTO_FLUSH_COUNT)
 *
 * @param client Plexus client
 * @param count  Flush after this many queued metrics (0 = disable count-based flush)
 * @return       PLEXUS_OK on success
 */
plexus_err_t plexus_set_flush_count(plexus_client_t* client, uint16_t count);

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
 * @param tag_keys   Array of tag keys
 * @param tag_values Array of tag values
 * @param tag_count  Number of tags (max 4)
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
 *
 * @param client Plexus client
 * @return       PLEXUS_OK if no flush needed or flush succeeded
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

/**
 * Get total number of successfully sent metrics (lifetime counter)
 *
 * @param client Plexus client
 * @return       Total metrics sent since init
 */
uint32_t plexus_total_sent(const plexus_client_t* client);

/**
 * Get total number of send errors (lifetime counter)
 *
 * @param client Plexus client
 * @return       Total errors since init
 */
uint32_t plexus_total_errors(const plexus_client_t* client);

/* ------------------------------------------------------------------------- */
/* Command support (opt-in via PLEXUS_ENABLE_COMMANDS)                       */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_COMMANDS

typedef struct {
    char id[64];
    char command[PLEXUS_MAX_COMMAND_LEN];
    int timeout_seconds;
} plexus_command_t;

typedef plexus_err_t (*plexus_command_handler_t)(
    const plexus_command_t* cmd,
    char* output,
    int* exit_code
);

plexus_err_t plexus_register_command_handler(plexus_client_t* client,
                                              plexus_command_handler_t handler);

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
 * @return Version string (e.g., "0.1.0")
 */
const char* plexus_version(void);

/* ------------------------------------------------------------------------- */
/* HAL interface (implemented per platform)                                  */
/* ------------------------------------------------------------------------- */

plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* body, size_t body_len);

#if PLEXUS_ENABLE_COMMANDS
plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
                                  char* response_buf, size_t buf_size,
                                  size_t* response_len);
#endif

uint64_t plexus_hal_get_time_ms(void);
uint32_t plexus_hal_get_tick_ms(void);
void plexus_hal_delay_ms(uint32_t ms);
void plexus_hal_log(const char* fmt, ...);

#if PLEXUS_ENABLE_PERSISTENT_BUFFER
plexus_err_t plexus_hal_storage_write(const char* key, const void* data, size_t len);
plexus_err_t plexus_hal_storage_read(const char* key, void* data, size_t max_len, size_t* out_len);
plexus_err_t plexus_hal_storage_clear(const char* key);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PLEXUS_H */
