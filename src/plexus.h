/**
 * @file plexus.h
 * @brief Plexus C SDK - Minimal footprint telemetry for embedded devices
 *
 * Send metrics from ESP32, STM32, and Arduino to the Plexus ingest API.
 *
 * Quickstart:
 *   plexus_client_t* px = plexus_init("plx_xxx", "device-001");
 *   plexus_send(px, "temperature", 72.5);
 *   plexus_flush(px);
 *   plexus_free(px);
 *
 * Thread safety: NOT thread-safe. Confine all calls for a given client
 * to a single thread/task. Multiple clients in separate tasks is safe.
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
/* Version                                                                   */
/* ------------------------------------------------------------------------- */

#define PLEXUS_SDK_VERSION "0.5.6"

/* ------------------------------------------------------------------------- */
/* Compiler attributes                                                       */
/* ------------------------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#define PLEXUS_PRINTF_FMT(fmt_idx, arg_idx) \
    __attribute__((format(printf, fmt_idx, arg_idx)))
#define PLEXUS_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define PLEXUS_PRINTF_FMT(fmt_idx, arg_idx)
#define PLEXUS_WARN_UNUSED_RESULT
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
    PLEXUS_ERR_FORBIDDEN,       /* Forbidden — missing scope (403) */
    PLEXUS_ERR_BILLING,         /* Billing limit exceeded (402) */
    PLEXUS_ERR_RATE_LIMIT,      /* Rate limited (429) */
    PLEXUS_ERR_SERVER,          /* Server error (5xx) */
    PLEXUS_ERR_JSON,            /* JSON serialization error */
    PLEXUS_ERR_NOT_INITIALIZED, /* Client not initialized */
    PLEXUS_ERR_HAL,             /* HAL layer error */
    PLEXUS_ERR_INVALID_ARG,     /* Invalid argument (bad characters, etc.) */
    PLEXUS_ERR__COUNT           /* Sentinel — must be last */
} plexus_err_t;

/* ========================================================================= */
/* Client struct layout (exposed for compile-time sizing only)               */
/*                                                                           */
/* These types are public so that sizeof(plexus_client_t) resolves at        */
/* compile time, enabling stack/static allocation without malloc.            */
/*                                                                           */
/* >>> DO NOT access struct members directly. <<<                            */
/* >>> The layout WILL change between versions. <<<                          */
/* ========================================================================= */

/** @internal Value type tag */
typedef enum {
    PLEXUS_VALUE_NUMBER,
    PLEXUS_VALUE_STRING,
    PLEXUS_VALUE_BOOL,
} plexus_value_type_t;

/** @internal Tagged value union */
typedef struct {
    plexus_value_type_t type;
    union {
        double number;
#if PLEXUS_ENABLE_STRING_VALUES
        char string[PLEXUS_MAX_STRING_VALUE_LEN];
#endif
#if PLEXUS_ENABLE_BOOL_VALUES
        bool boolean;
#endif
    } data;
} plexus_value_t;

/** @internal Single queued metric */
typedef struct {
    char name[PLEXUS_MAX_METRIC_NAME_LEN];
    plexus_value_t value;
    uint64_t timestamp_ms;
#if PLEXUS_ENABLE_TAGS
    char tag_keys[PLEXUS_MAX_TAGS][PLEXUS_MAX_TAG_LEN];
    char tag_values[PLEXUS_MAX_TAGS][PLEXUS_MAX_TAG_LEN];
    uint8_t tag_count;
#endif
} plexus_metric_t;

/* Connection status types (when enabled) */
#if PLEXUS_ENABLE_STATUS_CALLBACK

typedef enum {
    PLEXUS_STATUS_CONNECTED,
    PLEXUS_STATUS_DISCONNECTED,
    PLEXUS_STATUS_AUTH_FAILED,
    PLEXUS_STATUS_RATE_LIMITED,
} plexus_conn_status_t;

typedef void (*plexus_status_callback_t)(plexus_conn_status_t status, void* user_data);

#endif /* PLEXUS_ENABLE_STATUS_CALLBACK */

/** @internal Client struct — do not access members directly */
struct plexus_client {
    char api_key[PLEXUS_MAX_API_KEY_LEN];
    char source_id[PLEXUS_MAX_SOURCE_ID_LEN];
    char session_id[PLEXUS_MAX_SESSION_ID_LEN];
    char endpoint[PLEXUS_MAX_ENDPOINT_LEN];

    plexus_metric_t metrics[PLEXUS_MAX_METRICS];
    uint16_t metric_count;

    uint32_t last_flush_ms;
    uint32_t total_sent;
    uint32_t total_errors;

    /* Runtime-configurable overrides (0 = use compile-time default) */
    uint32_t flush_interval_ms;
    uint16_t auto_flush_count;

    /* Retry backoff state */
    uint32_t retry_backoff_ms;
    uint32_t rate_limit_until_ms;

    bool initialized;
    bool _heap_allocated; /* true = created via plexus_init(), safe to free() */

    /* Per-client JSON serialization buffer (no global state) */
    char json_buffer[PLEXUS_JSON_BUFFER_SIZE];

#if PLEXUS_ENABLE_STATUS_CALLBACK
    plexus_status_callback_t status_callback;
    void* status_callback_data;
    plexus_conn_status_t last_status;
#endif

#if PLEXUS_ENABLE_THREAD_SAFE
    void* mutex;
#endif
};

typedef struct plexus_client plexus_client_t;

/* ------------------------------------------------------------------------- */
/* Static allocation helpers                                                 */
/* ------------------------------------------------------------------------- */

/**
 * Compile-time size of plexus_client_t for static buffer declarations.
 *
 * @example
 *   static uint8_t buf[PLEXUS_CLIENT_STATIC_SIZE];
 *   plexus_client_t* px = plexus_init_static(buf, sizeof(buf), key, id);
 */
#define PLEXUS_CLIENT_STATIC_SIZE  sizeof(plexus_client_t)

/**
 * Declare a correctly-sized and aligned static buffer for a client.
 *
 * @example
 *   PLEXUS_CLIENT_STATIC_BUF(my_buf);
 *   plexus_client_t* px = plexus_init_static(
 *       &my_buf, sizeof(my_buf), "plx_xxx", "device-001");
 */
#define PLEXUS_CLIENT_STATIC_BUF(name) \
    static plexus_client_t name

/* ------------------------------------------------------------------------- */
/* Core API                                                                  */
/* ------------------------------------------------------------------------- */

/**
 * Initialize a Plexus client (heap-allocated).
 *
 * @param api_key   Your Plexus API key (e.g., "plx_xxxxx")
 * @param source_id Device identifier (e.g., "drone-001").
 *                  Must contain only [a-zA-Z0-9._-] characters.
 * @return          Client pointer, or NULL on failure
 *
 * @note Free with plexus_free() when done.
 */
plexus_client_t* plexus_init(const char* api_key, const char* source_id);

/**
 * Initialize a Plexus client in user-provided memory (no malloc).
 *
 * The buffer must be at least sizeof(plexus_client_t) bytes, correctly
 * aligned, and remain valid for the lifetime of the client.
 * Use PLEXUS_CLIENT_STATIC_BUF() for guaranteed correct size and alignment.
 * Returns NULL if the buffer is too small or misaligned.
 *
 * plexus_free() is safe on static clients — it marks them uninitialized
 * but does not call free().
 *
 * @param buf       Buffer (at least PLEXUS_CLIENT_STATIC_SIZE bytes, pointer-aligned)
 * @param buf_size  Size of the provided buffer
 * @param api_key   Your Plexus API key
 * @param source_id Device identifier (same restrictions as plexus_init)
 * @return          Client pointer (== buf on success), or NULL on failure
 */
plexus_client_t* plexus_init_static(void* buf, size_t buf_size,
                                      const char* api_key, const char* source_id);

/**
 * Get the required buffer size for plexus_init_static().
 * Equivalent to sizeof(plexus_client_t) but callable at runtime.
 */
size_t plexus_client_size(void);

/**
 * Free a heap-allocated Plexus client.
 *
 * Safe to pass NULL. Does NOT flush — call plexus_flush() first if needed.
 * If called on a statically-allocated client (plexus_init_static), this
 * is a no-op (the client is marked uninitialized but not freed).
 *
 * @param client Client to free
 */
void plexus_free(plexus_client_t* client);

/**
 * Set custom ingest endpoint URL.
 *
 * @param client   Plexus client
 * @param endpoint Full URL (e.g., "https://custom.domain/api/ingest")
 * @return         PLEXUS_OK on success
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_set_endpoint(plexus_client_t* client, const char* endpoint);

/**
 * Set runtime flush interval (overrides PLEXUS_AUTO_FLUSH_INTERVAL_MS).
 *
 * @param client      Plexus client
 * @param interval_ms Flush interval in milliseconds (0 = disable)
 * @return            PLEXUS_OK on success
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_set_flush_interval(plexus_client_t* client, uint32_t interval_ms);

/**
 * Set runtime auto-flush count (overrides PLEXUS_AUTO_FLUSH_COUNT).
 *
 * @param client Plexus client
 * @param count  Flush after this many queued metrics (0 = disable)
 * @return       PLEXUS_OK on success
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_set_flush_count(plexus_client_t* client, uint16_t count);

/* ------------------------------------------------------------------------- */
/* Send metrics                                                              */
/* ------------------------------------------------------------------------- */

/**
 * Queue a numeric metric.
 *
 * @param client Plexus client
 * @param metric Metric name (e.g., "temperature")
 * @param value  Numeric value
 * @return       PLEXUS_OK, or PLEXUS_ERR_BUFFER_FULL if buffer full
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_send_number(plexus_client_t* client, const char* metric, double value);

/**
 * Convenience alias: plexus_send() == plexus_send_number().
 *
 * Matches the Python SDK's px.send("metric", value) pattern.
 *
 * @example
 *   plexus_send(px, "temperature", 72.5);
 */
#define plexus_send(client, metric, value) \
    plexus_send_number((client), (metric), (value))

/**
 * Queue a numeric metric with explicit timestamp.
 *
 * @param client       Plexus client
 * @param metric       Metric name
 * @param value        Numeric value
 * @param timestamp_ms Unix timestamp in milliseconds
 * @return             PLEXUS_OK on success
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_send_number_ts(plexus_client_t* client, const char* metric,
                                    double value, uint64_t timestamp_ms);

#if PLEXUS_ENABLE_STRING_VALUES
/**
 * Queue a string metric.
 *
 * @param client Plexus client
 * @param metric Metric name
 * @param value  String value
 * @return       PLEXUS_OK on success
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_send_string(plexus_client_t* client, const char* metric, const char* value);
#endif

#if PLEXUS_ENABLE_BOOL_VALUES
/**
 * Queue a boolean metric.
 *
 * @param client Plexus client
 * @param metric Metric name
 * @param value  Boolean value
 * @return       PLEXUS_OK on success
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_send_bool(plexus_client_t* client, const char* metric, bool value);
#endif

#if PLEXUS_ENABLE_TAGS
/**
 * Queue a numeric metric with tags.
 *
 * @param client     Plexus client
 * @param metric     Metric name
 * @param value      Numeric value
 * @param tag_keys   Array of tag keys
 * @param tag_values Array of tag values
 * @param tag_count  Number of tags (max 4)
 * @return           PLEXUS_OK on success
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_send_number_tagged(plexus_client_t* client, const char* metric,
                                        double value, const char** tag_keys,
                                        const char** tag_values, uint8_t tag_count);
#endif

/* ------------------------------------------------------------------------- */
/* Flush & network                                                           */
/* ------------------------------------------------------------------------- */

/**
 * Send all queued metrics to the Plexus API.
 *
 * On success, the metric buffer is cleared.
 * On network error, metrics remain in buffer for retry.
 *
 * WARNING: This function blocks during retries with exponential backoff.
 * Worst case: ~14 seconds (3 retries with 8-second max backoff).
 * On FreeRTOS, the calling task yields during delays. On bare-metal
 * Arduino, delay() blocks the entire MCU.
 *
 * @param client Plexus client
 * @return       PLEXUS_OK on success, error code on failure
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_flush(plexus_client_t* client);

/**
 * Call periodically from your main loop.
 *
 * Handles time-based auto-flush.
 * Returns PLEXUS_OK when idle. Only returns an error if a flush
 * actually fails.
 *
 * @param client Plexus client
 * @return       PLEXUS_OK on success or idle
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_tick(plexus_client_t* client);

/** Get number of queued metrics. */
uint16_t plexus_pending_count(const plexus_client_t* client);

/** Clear all queued metrics without sending. */
void plexus_clear(plexus_client_t* client);

/** Lifetime counter: total metrics successfully sent. */
uint32_t plexus_total_sent(const plexus_client_t* client);

/** Lifetime counter: total send errors. */
uint32_t plexus_total_errors(const plexus_client_t* client);

/* ------------------------------------------------------------------------- */
/* Recording sessions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * Start a recording session.
 *
 * All subsequent metrics will include session_id in their JSON payload
 * until plexus_session_end() is called. Session IDs follow the same
 * URL-safe character rules as source_id: [a-zA-Z0-9._-].
 *
 * @param client     Plexus client
 * @param session_id Session identifier (e.g., "motor-test-001")
 * @return           PLEXUS_OK on success
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_session_start(plexus_client_t* client, const char* session_id);

/**
 * End the current recording session.
 *
 * Subsequent metrics will no longer include a session_id field.
 *
 * @param client Plexus client
 * @return       PLEXUS_OK on success
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_session_end(plexus_client_t* client);

/**
 * Get the current session ID, or NULL if no session is active.
 *
 * @param client Plexus client
 * @return       Session ID string, or NULL
 */
const char* plexus_session_id(const plexus_client_t* client);

/* ------------------------------------------------------------------------- */
/* Connection status (opt-in via PLEXUS_ENABLE_STATUS_CALLBACK)              */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_STATUS_CALLBACK

/**
 * Register a callback for connection status changes.
 * The callback fires only on state transitions (not repeated for same status).
 */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_on_status_change(plexus_client_t* client,
                                      plexus_status_callback_t callback,
                                      void* user_data);

/** Get the last known connection status. */
plexus_conn_status_t plexus_get_status(const plexus_client_t* client);

#endif /* PLEXUS_ENABLE_STATUS_CALLBACK */

/* ------------------------------------------------------------------------- */
/* Utility                                                                   */
/* ------------------------------------------------------------------------- */

/** Get human-readable error message. */
const char* plexus_strerror(plexus_err_t err);

/** Get SDK version string (e.g., "0.5.0"). */
const char* plexus_version(void);

/* ------------------------------------------------------------------------- */
/* HAL interface (implemented per platform)                                  */
/* ------------------------------------------------------------------------- */

plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* user_agent,
                                   const char* body, size_t body_len);

uint64_t plexus_hal_get_time_ms(void);
uint32_t plexus_hal_get_tick_ms(void);
void plexus_hal_delay_ms(uint32_t ms);
void plexus_hal_log(const char* fmt, ...) PLEXUS_PRINTF_FMT(1, 2);

#if PLEXUS_ENABLE_PERSISTENT_BUFFER
plexus_err_t plexus_hal_storage_write(const char* key, const void* data, size_t len);
plexus_err_t plexus_hal_storage_read(const char* key, void* data, size_t max_len, size_t* out_len);
plexus_err_t plexus_hal_storage_clear(const char* key);
#endif

#if PLEXUS_ENABLE_THREAD_SAFE
void* plexus_hal_mutex_create(void);
void  plexus_hal_mutex_lock(void* mutex);
void  plexus_hal_mutex_unlock(void* mutex);
void  plexus_hal_mutex_destroy(void* mutex);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PLEXUS_H */
