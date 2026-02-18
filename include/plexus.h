/**
 * @file plexus.h
 * @brief Plexus C SDK - Minimal footprint telemetry client for embedded devices
 *
 * This SDK provides a simple interface for sending telemetry data from
 * ESP32, STM32, and Arduino devices to the Plexus ingest API.
 *
 * Memory usage depends on configuration. Call plexus_client_size() or use
 * PLEXUS_CLIENT_STATIC_BUF() to determine the exact size for your build.
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
/* Compiler attributes                                                       */
/* ------------------------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#define PLEXUS_PRINTF_FMT(fmt_idx, arg_idx) \
    __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define PLEXUS_PRINTF_FMT(fmt_idx, arg_idx)
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
    PLEXUS_ERR_INVALID_ARG,     /* Invalid argument (bad characters, etc.) */
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
 * Initialize a Plexus client (heap-allocated)
 *
 * @param api_key   Your Plexus API key (e.g., "plx_xxxxx")
 * @param source_id Device/source identifier (e.g., "drone-001").
 *                  Must contain only [a-zA-Z0-9._-] characters.
 * @return          Pointer to client, or NULL on failure
 *
 * @note The returned client must be freed with plexus_free()
 */
plexus_client_t* plexus_init(const char* api_key, const char* source_id);

/**
 * Initialize a Plexus client in user-provided memory (no malloc)
 *
 * Use this on platforms where dynamic allocation is prohibited or undesirable.
 * The buffer must be at least plexus_client_size() bytes and remain valid
 * for the lifetime of the client. Do NOT call plexus_free() on a statically
 * initialized client — just stop using it.
 *
 * @param buf       User-provided buffer (must be at least plexus_client_size() bytes)
 * @param buf_size  Size of the provided buffer
 * @param api_key   Your Plexus API key
 * @param source_id Device/source identifier (same restrictions as plexus_init)
 * @return          Pointer to client (== buf on success), or NULL on failure
 *
 * @example
 *   static uint8_t client_buf[PLEXUS_CLIENT_STATIC_SIZE];
 *   plexus_client_t* client = plexus_init_static(
 *       client_buf, sizeof(client_buf), "plx_xxx", "device-001");
 */
plexus_client_t* plexus_init_static(void* buf, size_t buf_size,
                                      const char* api_key, const char* source_id);

/**
 * Get the required buffer size for plexus_init_static()
 *
 * @return Size in bytes needed for a plexus_client_t with current config
 */
size_t plexus_client_size(void);

/**
 * Convenience macro to declare a correctly-sized static buffer.
 *
 * @example
 *   PLEXUS_CLIENT_STATIC_BUF(my_buf);
 *   plexus_client_t* client = plexus_init_static(
 *       my_buf, sizeof(my_buf), "plx_xxx", "device-001");
 */
#define PLEXUS_CLIENT_STATIC_SIZE  (sizeof(plexus_client_t))

/* Forward-declare struct so sizeof works in the macro above.
 * The actual definition is in plexus_internal.h. Application code
 * must not access struct members directly. */

/**
 * Free a Plexus client and release resources
 *
 * Call plexus_flush() before freeing if you need to ensure all queued
 * metrics are sent. This function does NOT flush automatically — any
 * unsent metrics in the buffer will be discarded.
 *
 * @param client Client to free (safe to pass NULL).
 *               Must have been created with plexus_init(), NOT plexus_init_static().
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
 * @note Uses exponential backoff with jitter between retries
 */
plexus_err_t plexus_flush(plexus_client_t* client);

/**
 * Call periodically from your main loop to handle time-based operations
 *
 * This function:
 * - Flushes queued metrics when the flush interval elapses
 * - Polls for commands (if PLEXUS_ENABLE_COMMANDS is set)
 *
 * Returns PLEXUS_OK when idle (nothing to do). Only returns an error
 * if a flush or command poll actually fails.
 *
 * @param client Plexus client
 * @return       PLEXUS_OK on success or idle, error code on flush failure
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
 * @return Version string (e.g., "0.1.1")
 */
const char* plexus_version(void);

/* ------------------------------------------------------------------------- */
/* HAL interface (implemented per platform)                                  */
/* ------------------------------------------------------------------------- */

/**
 * @param url        Target URL
 * @param api_key    API key for x-api-key header
 * @param user_agent User-Agent header value (e.g., "plexus-c-sdk/0.1.1")
 * @param body       JSON request body
 * @param body_len   Length of body in bytes
 * @return           PLEXUS_OK on 2xx, error code otherwise
 */
plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* user_agent,
                                   const char* body, size_t body_len);

#if PLEXUS_ENABLE_COMMANDS
plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
                                  const char* user_agent,
                                  char* response_buf, size_t buf_size,
                                  size_t* response_len);
#endif

uint64_t plexus_hal_get_time_ms(void);
uint32_t plexus_hal_get_tick_ms(void);
void plexus_hal_delay_ms(uint32_t ms);
void plexus_hal_log(const char* fmt, ...) PLEXUS_PRINTF_FMT(1, 2);

#if PLEXUS_ENABLE_PERSISTENT_BUFFER
plexus_err_t plexus_hal_storage_write(const char* key, const void* data, size_t len);
plexus_err_t plexus_hal_storage_read(const char* key, void* data, size_t max_len, size_t* out_len);
plexus_err_t plexus_hal_storage_clear(const char* key);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PLEXUS_H */
