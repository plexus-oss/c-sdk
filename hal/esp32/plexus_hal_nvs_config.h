/**
 * @file plexus_hal_nvs_config.h
 * @brief Read Plexus device configuration from a dedicated NVS partition
 *
 * Used by browser-flashed firmware: the web UI generates a small NVS
 * partition containing api_key, source_id, endpoint, wifi_ssid, and
 * wifi_pass, which is flashed alongside the application binary.
 *
 * The config lives in NVS namespace "plexus_cfg" (separate from the
 * SDK's own "plexus" namespace used for persistent buffering).
 */

#ifndef PLEXUS_HAL_NVS_CONFIG_H
#define PLEXUS_HAL_NVS_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum lengths for config values (including null terminator) */
#define PLEXUS_CFG_MAX_API_KEY   128
#define PLEXUS_CFG_MAX_SOURCE_ID  64
#define PLEXUS_CFG_MAX_ENDPOINT  256
#define PLEXUS_CFG_MAX_WIFI_SSID  33  /* IEEE 802.11 max SSID = 32 chars */
#define PLEXUS_CFG_MAX_WIFI_PASS  64

/** Configuration read from NVS */
typedef struct {
    char api_key[PLEXUS_CFG_MAX_API_KEY];
    char source_id[PLEXUS_CFG_MAX_SOURCE_ID];
    char endpoint[PLEXUS_CFG_MAX_ENDPOINT];
    char wifi_ssid[PLEXUS_CFG_MAX_WIFI_SSID];
    char wifi_pass[PLEXUS_CFG_MAX_WIFI_PASS];
    bool valid;  /* true if at least api_key and source_id were found */
} plexus_nvs_config_t;

/**
 * Read device configuration from the "plexus_cfg" NVS namespace.
 *
 * @param[out] cfg  Populated on success; cfg->valid indicates whether
 *                  the minimum required keys (api_key, source_id) exist.
 * @return true if NVS was readable (cfg->valid may still be false if
 *         keys are missing); false on NVS init failure.
 */
bool plexus_nvs_config_read(plexus_nvs_config_t* cfg);

#ifdef __cplusplus
}
#endif

#endif /* PLEXUS_HAL_NVS_CONFIG_H */
