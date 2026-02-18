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

/* Persistent buffer (flash storage for unsent data) */
#ifndef PLEXUS_ENABLE_PERSISTENT_BUFFER
#define PLEXUS_ENABLE_PERSISTENT_BUFFER 0  /* Enable flash-backed buffer on flush failure */
#endif

/* Debug settings */
#ifndef PLEXUS_DEBUG
#define PLEXUS_DEBUG 0                 /* Enable debug logging */
#endif

#endif /* PLEXUS_CONFIG_H */
