/**
 * @file plexus_config.h
 * @brief Compile-time configuration for Plexus C SDK
 *
 * Override these defaults by defining them before including plexus.h
 * or via compiler flags (e.g., -DPLEXUS_MAX_METRICS=64)
 *
 * Memory sizing guide (approximate sizeof(plexus_client_t)):
 *
 *   Default config (all features):     ~17 KB
 *   Minimal config (numbers only):     ~1.5 KB
 *     -DPLEXUS_MAX_METRICS=8
 *     -DPLEXUS_ENABLE_TAGS=0
 *     -DPLEXUS_ENABLE_STRING_VALUES=0
 *     -DPLEXUS_ENABLE_BOOL_VALUES=0
 *     -DPLEXUS_JSON_BUFFER_SIZE=512
 *     -DPLEXUS_MAX_ENDPOINT_LEN=128
 *     -DPLEXUS_MAX_API_KEY_LEN=64
 *
 * Use plexus_client_size() at runtime or PLEXUS_CLIENT_STATIC_SIZE
 * at compile time to get the exact size for your configuration.
 */

#ifndef PLEXUS_CONFIG_H
#define PLEXUS_CONFIG_H

/* Buffer sizes */
#ifndef PLEXUS_MAX_METRICS
#define PLEXUS_MAX_METRICS 32          /* Max metrics per flush */
#endif

#ifndef PLEXUS_MAX_METRIC_NAME_LEN
#define PLEXUS_MAX_METRIC_NAME_LEN 64  /* Max metric name length */
#endif

#ifndef PLEXUS_MAX_STRING_VALUE_LEN
#define PLEXUS_MAX_STRING_VALUE_LEN 128 /* Max string value length */
#endif

#ifndef PLEXUS_MAX_SOURCE_ID_LEN
#define PLEXUS_MAX_SOURCE_ID_LEN 64    /* Max source ID length */
#endif

#ifndef PLEXUS_MAX_API_KEY_LEN
#define PLEXUS_MAX_API_KEY_LEN 128     /* Max API key length */
#endif

#ifndef PLEXUS_MAX_ENDPOINT_LEN
#define PLEXUS_MAX_ENDPOINT_LEN 256    /* Max endpoint URL length */
#endif

#ifndef PLEXUS_JSON_BUFFER_SIZE
#define PLEXUS_JSON_BUFFER_SIZE 2048   /* JSON serialization buffer */
#endif

/* Network settings */
#ifndef PLEXUS_DEFAULT_ENDPOINT
#define PLEXUS_DEFAULT_ENDPOINT "https://app.plexus.company/api/ingest"
#endif

#ifndef PLEXUS_HTTP_TIMEOUT_MS
#define PLEXUS_HTTP_TIMEOUT_MS 10000   /* HTTP request timeout */
#endif

#ifndef PLEXUS_MAX_RETRIES
#define PLEXUS_MAX_RETRIES 3           /* Retry count on failure */
#endif

/* Exponential backoff settings for retries */
#ifndef PLEXUS_RETRY_BASE_MS
#define PLEXUS_RETRY_BASE_MS 500       /* Initial retry delay */
#endif

#ifndef PLEXUS_RETRY_MAX_MS
#define PLEXUS_RETRY_MAX_MS 8000       /* Maximum retry delay */
#endif

#ifndef PLEXUS_RATE_LIMIT_COOLDOWN_MS
#define PLEXUS_RATE_LIMIT_COOLDOWN_MS 30000 /* Cooldown after 429 response */
#endif

/* Auto-flush settings */
#ifndef PLEXUS_AUTO_FLUSH_COUNT
#define PLEXUS_AUTO_FLUSH_COUNT 16     /* Auto-flush after N metrics */
#endif

#ifndef PLEXUS_AUTO_FLUSH_INTERVAL_MS
#define PLEXUS_AUTO_FLUSH_INTERVAL_MS 5000 /* Auto-flush interval (0=disabled) */
#endif

/* Tag settings */
#ifndef PLEXUS_MAX_TAG_LEN
#define PLEXUS_MAX_TAG_LEN 32          /* Max tag key/value length */
#endif

#ifndef PLEXUS_MAX_TAGS
#define PLEXUS_MAX_TAGS 4              /* Max tags per metric */
#endif

/* Memory optimization flags */
#ifndef PLEXUS_ENABLE_TAGS
#define PLEXUS_ENABLE_TAGS 1           /* Enable metric tags support */
#endif

#ifndef PLEXUS_ENABLE_STRING_VALUES
#define PLEXUS_ENABLE_STRING_VALUES 1  /* Enable string value support */
#endif

#ifndef PLEXUS_ENABLE_BOOL_VALUES
#define PLEXUS_ENABLE_BOOL_VALUES 1    /* Enable boolean value support */
#endif

/* Command support (compile-time opt-in) */
#ifndef PLEXUS_ENABLE_COMMANDS
#define PLEXUS_ENABLE_COMMANDS 0       /* Enable command polling support */
#endif

#ifndef PLEXUS_COMMAND_POLL_INTERVAL_MS
#define PLEXUS_COMMAND_POLL_INTERVAL_MS 10000 /* Command poll interval */
#endif

#ifndef PLEXUS_MAX_COMMAND_LEN
#define PLEXUS_MAX_COMMAND_LEN 256     /* Max command string length */
#endif

#ifndef PLEXUS_MAX_COMMAND_RESULT_LEN
#define PLEXUS_MAX_COMMAND_RESULT_LEN 256 /* Max command result/output length */
#endif

/* Typed command support (compile-time opt-in) */
#ifndef PLEXUS_ENABLE_TYPED_COMMANDS
#define PLEXUS_ENABLE_TYPED_COMMANDS 0
#endif

#ifndef PLEXUS_MAX_TYPED_COMMANDS
#define PLEXUS_MAX_TYPED_COMMANDS 8
#endif

#ifndef PLEXUS_MAX_COMMAND_PARAMS
#define PLEXUS_MAX_COMMAND_PARAMS 4
#endif

#ifndef PLEXUS_MAX_PARAM_NAME_LEN
#define PLEXUS_MAX_PARAM_NAME_LEN 32
#endif

#ifndef PLEXUS_MAX_PARAM_CHOICES
#define PLEXUS_MAX_PARAM_CHOICES 8
#endif

#ifndef PLEXUS_MAX_COMMAND_DESC_LEN
#define PLEXUS_MAX_COMMAND_DESC_LEN 64
#endif

/* Persistent buffer (flash storage for unsent data) */
#ifndef PLEXUS_ENABLE_PERSISTENT_BUFFER
#define PLEXUS_ENABLE_PERSISTENT_BUFFER 0  /* Enable flash-backed buffer on flush failure */
#endif

#ifndef PLEXUS_PERSIST_MAX_BATCHES
#define PLEXUS_PERSIST_MAX_BATCHES 8       /* Number of batch slots in persistent ring buffer */
#endif

/* Connection status callback (compile-time opt-in) */
#ifndef PLEXUS_ENABLE_STATUS_CALLBACK
#define PLEXUS_ENABLE_STATUS_CALLBACK 0    /* Enable connection status notifications */
#endif

/* Thread safety (compile-time opt-in) */
#ifndef PLEXUS_ENABLE_THREAD_SAFE
#define PLEXUS_ENABLE_THREAD_SAFE 0        /* Enable mutex-protected client access */
#endif

/* Device heartbeat / registration (compile-time opt-in) */
#ifndef PLEXUS_ENABLE_HEARTBEAT
#define PLEXUS_ENABLE_HEARTBEAT 0          /* Enable heartbeat and metric registry */
#endif

#ifndef PLEXUS_HEARTBEAT_INTERVAL_MS
#define PLEXUS_HEARTBEAT_INTERVAL_MS 60000 /* Heartbeat send interval */
#endif

#ifndef PLEXUS_MAX_REGISTERED_METRICS
#define PLEXUS_MAX_REGISTERED_METRICS 16   /* Max metric names in registry */
#endif

#ifndef PLEXUS_MAX_METADATA_LEN
#define PLEXUS_MAX_METADATA_LEN 64         /* Max device_type / firmware_version length */
#endif

/* MQTT transport (compile-time opt-in) */
#ifndef PLEXUS_ENABLE_MQTT
#define PLEXUS_ENABLE_MQTT 0               /* Enable MQTT as alternative transport */
#endif

#ifndef PLEXUS_MQTT_TOPIC_PREFIX
#define PLEXUS_MQTT_TOPIC_PREFIX "plexus/ingest"
#endif

#ifndef PLEXUS_MQTT_QOS
#define PLEXUS_MQTT_QOS 1                  /* MQTT QoS level for publish */
#endif

#ifndef PLEXUS_MQTT_KEEP_ALIVE_S
#define PLEXUS_MQTT_KEEP_ALIVE_S 60        /* MQTT keep-alive interval */
#endif

#ifndef PLEXUS_MQTT_CMD_TOPIC_PREFIX
#define PLEXUS_MQTT_CMD_TOPIC_PREFIX "plexus/commands"
#endif

/* Auto-registration (compile-time opt-in) */
#ifndef PLEXUS_ENABLE_AUTO_REGISTER
#define PLEXUS_ENABLE_AUTO_REGISTER 0
#endif

/* I2C sensor discovery (compile-time opt-in) */
#ifndef PLEXUS_ENABLE_SENSOR_DISCOVERY
#define PLEXUS_ENABLE_SENSOR_DISCOVERY 0
#endif

/* Per-sensor compile flags (default: all OFF — enable what you need) */
#ifndef PLEXUS_SENSOR_BME280
#define PLEXUS_SENSOR_BME280    0
#endif
#ifndef PLEXUS_SENSOR_MPU6050
#define PLEXUS_SENSOR_MPU6050   0
#endif
#ifndef PLEXUS_SENSOR_INA219
#define PLEXUS_SENSOR_INA219    0
#endif
#ifndef PLEXUS_SENSOR_ADS1115
#define PLEXUS_SENSOR_ADS1115   0
#endif
#ifndef PLEXUS_SENSOR_SHT3X
#define PLEXUS_SENSOR_SHT3X     0
#endif
#ifndef PLEXUS_SENSOR_BH1750
#define PLEXUS_SENSOR_BH1750    0
#endif
#ifndef PLEXUS_SENSOR_VL53L0X
#define PLEXUS_SENSOR_VL53L0X   0
#endif
#ifndef PLEXUS_SENSOR_QMC5883L
#define PLEXUS_SENSOR_QMC5883L  0
#endif
#ifndef PLEXUS_SENSOR_HMC5883L
#define PLEXUS_SENSOR_HMC5883L  0
#endif

#ifndef PLEXUS_MAX_DETECTED_SENSORS
#define PLEXUS_MAX_DETECTED_SENSORS 16
#endif

#ifndef PLEXUS_MAX_SENSOR_METRICS
#define PLEXUS_MAX_SENSOR_METRICS 8
#endif

#ifndef PLEXUS_MAX_CUSTOM_SENSORS
#define PLEXUS_MAX_CUSTOM_SENSORS 4
#endif

#ifndef PLEXUS_I2C_SCAN_START
#define PLEXUS_I2C_SCAN_START 0x03
#endif

#ifndef PLEXUS_I2C_SCAN_END
#define PLEXUS_I2C_SCAN_END 0x78
#endif

/* Debug settings */
#ifndef PLEXUS_DEBUG
#define PLEXUS_DEBUG 0                 /* Enable debug logging */
#endif

#endif /* PLEXUS_CONFIG_H */
