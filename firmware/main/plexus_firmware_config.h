/**
 * @file plexus_firmware_config.h
 * @brief Plexus SDK configuration for the generic firmware
 *
 * Enables all features at the "standard" memory preset (~8 KB RAM).
 * Included before plexus.h to override defaults in plexus_config.h.
 */

#ifndef PLEXUS_FIRMWARE_CONFIG_H
#define PLEXUS_FIRMWARE_CONFIG_H

/* Standard memory preset */
#define PLEXUS_MAX_METRICS              16
#define PLEXUS_JSON_BUFFER_SIZE         1024
#define PLEXUS_MAX_API_KEY_LEN          128
#define PLEXUS_MAX_METRIC_NAME_LEN      48

/* Enable all features for the generic firmware */
#define PLEXUS_ENABLE_SENSOR_DISCOVERY  1
#define PLEXUS_ENABLE_AUTO_REGISTER     1
#define PLEXUS_ENABLE_HEARTBEAT         1
#define PLEXUS_ENABLE_PERSISTENT_BUFFER 1
#define PLEXUS_ENABLE_COMMANDS          1
#define PLEXUS_ENABLE_TYPED_COMMANDS    1
#define PLEXUS_ENABLE_THREAD_SAFE       1
#define PLEXUS_ENABLE_STATUS_CALLBACK   1
#define PLEXUS_ENABLE_TAGS              1
#define PLEXUS_ENABLE_STRING_VALUES     1
#define PLEXUS_ENABLE_BOOL_VALUES       1

/* Debug logging in development builds */
#ifndef NDEBUG
#define PLEXUS_DEBUG                    1
#endif

#endif /* PLEXUS_FIRMWARE_CONFIG_H */
