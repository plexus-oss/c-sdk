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

#define PLEXUS_SDK_VERSION "0.4.0"

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
    PLEXUS_ERR_RATE_LIMIT,      /* Rate limited (429) */
    PLEXUS_ERR_SERVER,          /* Server error (5xx) */
    PLEXUS_ERR_JSON,            /* JSON serialization error */
    PLEXUS_ERR_NOT_INITIALIZED, /* Client not initialized */
    PLEXUS_ERR_HAL,             /* HAL layer error */
    PLEXUS_ERR_INVALID_ARG,     /* Invalid argument (bad characters, etc.) */
    PLEXUS_ERR_TRANSPORT,       /* Transport-specific error (MQTT disconnect, etc.) */
    PLEXUS_ERR_NOT_REGISTERED,  /* Device not yet registered */
    PLEXUS_ERR_I2C,             /* I2C communication error */
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

/* Command types (when enabled) */
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

#endif /* PLEXUS_ENABLE_COMMANDS */

/* Typed command types (when enabled) */
#if PLEXUS_ENABLE_TYPED_COMMANDS

typedef enum {
    PLEXUS_PARAM_FLOAT,
    PLEXUS_PARAM_INT,
    PLEXUS_PARAM_STRING,
    PLEXUS_PARAM_BOOL,
    PLEXUS_PARAM_ENUM,
} plexus_param_type_t;

typedef struct {
    char name[PLEXUS_MAX_PARAM_NAME_LEN];
    plexus_param_type_t type;
    char description[PLEXUS_MAX_COMMAND_DESC_LEN];
    char unit[16];
    double min_val;     /* For float/int */
    double max_val;     /* For float/int */
    double step;        /* For UI sliders */
    double default_val;
    bool has_default;
    bool required;
    const char* choices[PLEXUS_MAX_PARAM_CHOICES]; /* For enum, NULL-terminated */
    uint8_t choice_count;
} plexus_param_desc_t;

/* Parameter value passed to handler */
typedef struct {
    plexus_param_type_t type;
    union {
        double number;
        int integer;
        bool boolean;
        char string[PLEXUS_MAX_STRING_VALUE_LEN];
    } data;
} plexus_param_value_t;

/* Handler receives parsed, validated params */
typedef plexus_err_t (*plexus_typed_cmd_handler_t)(
    const char* command_name,
    const plexus_param_value_t* params,
    uint8_t param_count,
    char* result_json,
    size_t result_json_size
);

typedef struct {
    char name[PLEXUS_MAX_PARAM_NAME_LEN];
    char description[PLEXUS_MAX_COMMAND_DESC_LEN];
    plexus_param_desc_t params[PLEXUS_MAX_COMMAND_PARAMS];
    uint8_t param_count;
    plexus_typed_cmd_handler_t handler;
} plexus_typed_command_t;

#endif /* PLEXUS_ENABLE_TYPED_COMMANDS */

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

/* Sensor discovery types (when enabled) */
#if PLEXUS_ENABLE_SENSOR_DISCOVERY

typedef bool (*plexus_sensor_probe_fn)(uint8_t addr);
typedef plexus_err_t (*plexus_sensor_read_fn)(uint8_t addr, float* values, uint8_t count);

typedef struct {
    const char* name;               /* "BME280" */
    const char* description;        /* "Environmental sensor" */
    const char* const* metrics;     /* {"temperature","humidity","pressure"} */
    uint8_t metric_count;
    uint8_t i2c_addrs[4];          /* {0x76, 0x77, 0, 0} — 0-terminated */
    float default_sample_rate_hz;
    plexus_sensor_probe_fn probe;   /* NULL = ACK-only detection */
    plexus_sensor_read_fn read;     /* NULL = no built-in driver */
} plexus_sensor_descriptor_t;

typedef struct {
    const plexus_sensor_descriptor_t* descriptor;
    uint8_t addr;
    bool active;
} plexus_detected_sensor_t;

#endif /* PLEXUS_ENABLE_SENSOR_DISCOVERY */

/* Transport type (when MQTT enabled) */
#if PLEXUS_ENABLE_MQTT

typedef enum {
    PLEXUS_TRANSPORT_HTTP = 0,
    PLEXUS_TRANSPORT_MQTT,
} plexus_transport_t;

#endif /* PLEXUS_ENABLE_MQTT */

/** @internal Client struct — do not access members directly */
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

    /* Retry backoff state */
    uint32_t retry_backoff_ms;
    uint32_t rate_limit_until_ms;

    bool initialized;
    bool _heap_allocated; /* true = created via plexus_init(), safe to free() */

    /* Per-client JSON serialization buffer (no global state) */
    char json_buffer[PLEXUS_JSON_BUFFER_SIZE];

#if PLEXUS_ENABLE_COMMANDS
    plexus_command_handler_t command_handler;
    uint32_t last_command_poll_ms;
#endif

#if PLEXUS_ENABLE_STATUS_CALLBACK
    plexus_status_callback_t status_callback;
    void* status_callback_data;
    plexus_conn_status_t last_status;
#endif

#if PLEXUS_ENABLE_THREAD_SAFE
    void* mutex;
#endif

#if PLEXUS_ENABLE_HEARTBEAT
    char registered_metrics[PLEXUS_MAX_REGISTERED_METRICS][PLEXUS_MAX_METRIC_NAME_LEN];
    uint16_t registered_metric_count;
    char device_type[PLEXUS_MAX_METADATA_LEN];
    char firmware_version[PLEXUS_MAX_METADATA_LEN];
    uint32_t last_heartbeat_ms;
#endif

#if PLEXUS_ENABLE_MQTT
    plexus_transport_t transport;
    char broker_uri[PLEXUS_MAX_ENDPOINT_LEN];
    char mqtt_topic[PLEXUS_MAX_ENDPOINT_LEN];
#endif

#if PLEXUS_ENABLE_AUTO_REGISTER
    bool registered;
    char hostname[PLEXUS_MAX_METADATA_LEN];
    char platform_name[PLEXUS_MAX_METADATA_LEN];
#endif

#if PLEXUS_ENABLE_SENSOR_DISCOVERY
    plexus_detected_sensor_t detected_sensors[PLEXUS_MAX_DETECTED_SENSORS];
    uint8_t detected_sensor_count;
#endif

#if PLEXUS_ENABLE_TYPED_COMMANDS
    plexus_typed_command_t typed_commands[PLEXUS_MAX_TYPED_COMMANDS];
    uint8_t typed_command_count;
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
 * Handles time-based auto-flush and command polling (if enabled).
 * Returns PLEXUS_OK when idle. Only returns an error if a flush
 * or command poll actually fails.
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
/* Commands (opt-in via PLEXUS_ENABLE_COMMANDS)                              */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_COMMANDS

plexus_err_t plexus_register_command_handler(plexus_client_t* client,
                                              plexus_command_handler_t handler);

plexus_err_t plexus_poll_commands(plexus_client_t* client);

#endif /* PLEXUS_ENABLE_COMMANDS */

/* ------------------------------------------------------------------------- */
/* Typed commands (opt-in via PLEXUS_ENABLE_TYPED_COMMANDS)                  */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_TYPED_COMMANDS

/** Register a typed command with parameter schema for auto-generated UI. */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_register_typed_command(plexus_client_t* client,
                                            const plexus_typed_command_t* command);

/** Serialize all registered typed command schemas to JSON. */
int plexus_typed_commands_schema(const plexus_client_t* client,
                                 char* buf, size_t buf_size);

#endif /* PLEXUS_ENABLE_TYPED_COMMANDS */

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
/* Heartbeat (opt-in via PLEXUS_ENABLE_HEARTBEAT)                            */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_HEARTBEAT

/** Register a metric name for heartbeat reporting. */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_register_metric(plexus_client_t* client, const char* metric_name);

/** Set device info for heartbeat reporting. */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_set_device_info(plexus_client_t* client,
                                     const char* device_type,
                                     const char* firmware_version);

/** Send a heartbeat immediately. Also called by tick() on interval. */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_heartbeat(plexus_client_t* client);

#endif /* PLEXUS_ENABLE_HEARTBEAT */

/* ------------------------------------------------------------------------- */
/* MQTT transport (opt-in via PLEXUS_ENABLE_MQTT)                            */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* Auto-registration (opt-in via PLEXUS_ENABLE_AUTO_REGISTER)                */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_AUTO_REGISTER

/** Set device identity for registration. */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_set_device_identity(plexus_client_t* client,
                                         const char* hostname,
                                         const char* platform_name);

/** Register device with server. No-op if already registered. */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_register_device(plexus_client_t* client);

/** Check if device is registered (has a source_id from server). */
bool plexus_is_registered(const plexus_client_t* client);

#endif /* PLEXUS_ENABLE_AUTO_REGISTER */

/* ------------------------------------------------------------------------- */
/* I2C Sensor Discovery (opt-in via PLEXUS_ENABLE_SENSOR_DISCOVERY)          */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_SENSOR_DISCOVERY

/** Register a custom sensor descriptor for discovery. */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_sensor_register(const plexus_sensor_descriptor_t* descriptor);

/** Scan I2C bus for known sensors, populate detected_sensors. */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_scan_sensors(plexus_client_t* client);

/** Read all detected sensors and queue metrics via plexus_send(). */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_sensor_read_all(plexus_client_t* client);

/** Get count of detected sensors. */
uint8_t plexus_detected_sensor_count(const plexus_client_t* client);

/** Get a detected sensor by index (NULL if out of range). */
const plexus_detected_sensor_t* plexus_detected_sensor(const plexus_client_t* client, uint8_t index);

#endif /* PLEXUS_ENABLE_SENSOR_DISCOVERY */

/* ------------------------------------------------------------------------- */
/* MQTT transport (opt-in via PLEXUS_ENABLE_MQTT)                            */
/* ------------------------------------------------------------------------- */

#if PLEXUS_ENABLE_MQTT

/** Set MQTT as the transport and configure broker URI. */
PLEXUS_WARN_UNUSED_RESULT
plexus_err_t plexus_set_transport_mqtt(plexus_client_t* client, const char* broker_uri);

/** Get the currently active transport type. */
plexus_transport_t plexus_get_transport(const plexus_client_t* client);

#endif /* PLEXUS_ENABLE_MQTT */

/* ------------------------------------------------------------------------- */
/* Utility                                                                   */
/* ------------------------------------------------------------------------- */

/** Get human-readable error message. */
const char* plexus_strerror(plexus_err_t err);

/** Get SDK version string (e.g., "0.2.1"). */
const char* plexus_version(void);

/* ------------------------------------------------------------------------- */
/* HAL interface (implemented per platform)                                  */
/* ------------------------------------------------------------------------- */

plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* user_agent,
                                   const char* body, size_t body_len);

#if PLEXUS_ENABLE_COMMANDS
plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
                                  const char* user_agent,
                                  char* response_buf, size_t buf_size,
                                  size_t* response_len);
#endif

#if PLEXUS_ENABLE_AUTO_REGISTER
plexus_err_t plexus_hal_http_post_response(
    const char* url, const char* api_key, const char* user_agent,
    const char* body, size_t body_len,
    char* response_buf, size_t response_buf_size, size_t* response_len);
#endif

#if PLEXUS_ENABLE_SENSOR_DISCOVERY
plexus_err_t plexus_hal_i2c_init(uint8_t bus_num);
bool plexus_hal_i2c_probe(uint8_t addr);
plexus_err_t plexus_hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t* out);
plexus_err_t plexus_hal_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val);
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

#if PLEXUS_ENABLE_THREAD_SAFE
void* plexus_hal_mutex_create(void);
void  plexus_hal_mutex_lock(void* mutex);
void  plexus_hal_mutex_unlock(void* mutex);
void  plexus_hal_mutex_destroy(void* mutex);
#endif

#if PLEXUS_ENABLE_MQTT
plexus_err_t plexus_hal_mqtt_connect(const char* broker_uri, const char* api_key,
                                      const char* source_id);
plexus_err_t plexus_hal_mqtt_publish(const char* topic, const char* payload,
                                      size_t payload_len, int qos);
bool plexus_hal_mqtt_is_connected(void);
void plexus_hal_mqtt_disconnect(void);
#if PLEXUS_ENABLE_COMMANDS
plexus_err_t plexus_hal_mqtt_subscribe(const char* topic, int qos);
plexus_err_t plexus_hal_mqtt_receive(char* buf, size_t buf_size, size_t* msg_len);
#endif
#endif /* PLEXUS_ENABLE_MQTT */

#ifdef __cplusplus
}

/* ========================================================================= */
/* C++ wrapper (Arduino / ESP-IDF C++ projects)                              */
/*                                                                           */
/* Provides an idiomatic C++ class:                                          */
/*   PlexusClient px("plx_xxx", "device-001");                              */
/*   px.send("temperature", 72.5);                                          */
/*   px.tick();                                                              */
/* ========================================================================= */

class PlexusClient {
public:
    PlexusClient(const char* apiKey, const char* sourceId)
        : _client(plexus_init(apiKey, sourceId)) {}

    ~PlexusClient() {
        if (_client) {
            plexus_free(_client);
        }
    }

    /* Non-copyable */
    PlexusClient(const PlexusClient&) = delete;
    PlexusClient& operator=(const PlexusClient&) = delete;

    bool isValid() const { return _client != 0; }

    plexus_err_t send(const char* metric, double value) {
        return plexus_send_number(_client, metric, value);
    }

    plexus_err_t sendNumber(const char* metric, double value) {
        return plexus_send_number(_client, metric, value);
    }

    plexus_err_t sendNumberTs(const char* metric, double value, uint64_t timestamp_ms) {
        return plexus_send_number_ts(_client, metric, value, timestamp_ms);
    }

#if PLEXUS_ENABLE_STRING_VALUES
    plexus_err_t sendString(const char* metric, const char* value) {
        return plexus_send_string(_client, metric, value);
    }
#endif

#if PLEXUS_ENABLE_BOOL_VALUES
    plexus_err_t sendBool(const char* metric, bool value) {
        return plexus_send_bool(_client, metric, value);
    }
#endif

    plexus_err_t flush() { return plexus_flush(_client); }
    plexus_err_t tick() { return plexus_tick(_client); }
    uint16_t pendingCount() const { return plexus_pending_count(_client); }
    void clear() { plexus_clear(_client); }

    plexus_err_t setEndpoint(const char* endpoint) {
        return plexus_set_endpoint(_client, endpoint);
    }

    plexus_err_t setFlushInterval(uint32_t interval_ms) {
        return plexus_set_flush_interval(_client, interval_ms);
    }

    plexus_err_t setFlushCount(uint16_t count) {
        return plexus_set_flush_count(_client, count);
    }

#if PLEXUS_ENABLE_STATUS_CALLBACK
    plexus_err_t onStatusChange(plexus_status_callback_t cb, void* userData) {
        return plexus_on_status_change(_client, cb, userData);
    }

    plexus_conn_status_t getStatus() const {
        return plexus_get_status(_client);
    }
#endif

#if PLEXUS_ENABLE_HEARTBEAT
    plexus_err_t registerMetric(const char* name) {
        return plexus_register_metric(_client, name);
    }

    plexus_err_t setDeviceInfo(const char* deviceType, const char* fwVersion) {
        return plexus_set_device_info(_client, deviceType, fwVersion);
    }

    plexus_err_t heartbeat() { return plexus_heartbeat(_client); }
#endif

#if PLEXUS_ENABLE_MQTT
    plexus_err_t setTransportMqtt(const char* brokerUri) {
        return plexus_set_transport_mqtt(_client, brokerUri);
    }

    plexus_transport_t getTransport() const {
        return plexus_get_transport(_client);
    }
#endif

#if PLEXUS_ENABLE_AUTO_REGISTER
    plexus_err_t setDeviceIdentity(const char* hostname, const char* platformName) {
        return plexus_set_device_identity(_client, hostname, platformName);
    }

    plexus_err_t registerDevice() { return plexus_register_device(_client); }
    bool isRegistered() const { return plexus_is_registered(_client); }
#endif

#if PLEXUS_ENABLE_SENSOR_DISCOVERY
    plexus_err_t scanSensors() { return plexus_scan_sensors(_client); }
    plexus_err_t sensorReadAll() { return plexus_sensor_read_all(_client); }
    uint8_t detectedSensorCount() const { return plexus_detected_sensor_count(_client); }
#endif

#if PLEXUS_ENABLE_TYPED_COMMANDS
    plexus_err_t registerTypedCommand(const plexus_typed_command_t* cmd) {
        return plexus_register_typed_command(_client, cmd);
    }

    int typedCommandsSchema(char* buf, size_t bufSize) const {
        return plexus_typed_commands_schema(_client, buf, bufSize);
    }
#endif

    plexus_client_t* handle() { return _client; }

private:
    plexus_client_t* _client;
};

#endif /* __cplusplus */

#endif /* PLEXUS_H */
