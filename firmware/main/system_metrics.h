/**
 * @file system_metrics.h
 * @brief ESP32 system health metrics — always available, no hardware required
 *
 * Reports internal chip temperature, free heap, WiFi RSSI, uptime,
 * and FreeRTOS task count. These metrics ensure the dashboard has
 * data to display even when no external sensors are connected.
 */

#ifndef PLEXUS_SYSTEM_METRICS_H
#define PLEXUS_SYSTEM_METRICS_H

#include "plexus.h"

/**
 * Register system metric names with the heartbeat registry.
 * Call after plexus_init() so the dashboard knows what to expect.
 */
void system_metrics_register(plexus_client_t* client);

/**
 * Read all ESP32 system metrics and queue them on the client.
 * Safe to call every tick (100ms) — values are cheap to read.
 */
void system_metrics_read(plexus_client_t* client);

#endif /* PLEXUS_SYSTEM_METRICS_H */
