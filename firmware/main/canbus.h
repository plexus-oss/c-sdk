/**
 * @file canbus.h
 * @brief ESP32 CAN bus (TWAI) adapter for Plexus firmware
 *
 * Uses the ESP32's built-in TWAI (Two-Wire Automotive Interface) peripheral
 * for CAN bus communication. No external CAN controller chip needed —
 * only a CAN transceiver (e.g., SN65HVD230, MCP2551, TJA1050).
 *
 * Default pins: TX=GPIO5, RX=GPIO4 (common on ESP32 DevKit)
 * Default bitrate: 500 kbps (standard automotive)
 */

#ifndef PLEXUS_CANBUS_H
#define PLEXUS_CANBUS_H

#include "plexus.h"
#include <stdbool.h>
#include <stdint.h>

/** Maximum CAN IDs to track metrics for */
#define CAN_MAX_TRACKED_IDS 32

/**
 * Initialize CAN bus (TWAI) with default pins and bitrate.
 * @param tx_pin GPIO for CAN TX (default: 5)
 * @param rx_pin GPIO for CAN RX (default: 4)
 * @param bitrate Bitrate in bps (125000, 250000, 500000, 1000000)
 * @return true if initialization succeeded
 */
bool canbus_init(int tx_pin, int rx_pin, uint32_t bitrate);

/**
 * Initialize with defaults (TX=5, RX=4, 500kbps).
 */
bool canbus_init_default(void);

/**
 * Register CAN metrics with heartbeat.
 */
void canbus_register_metrics(plexus_client_t* client);

/**
 * Read available CAN frames and send as telemetry.
 * Non-blocking — reads all queued frames and extracts metrics.
 *
 * Emits metrics as "can.0xXXX" where XXX is the CAN ID in hex.
 * The first 4 bytes of the frame are interpreted as a float value.
 * Raw frame data is also sent as a hex string if string values are enabled.
 */
void canbus_read(plexus_client_t* client);

/**
 * Send a CAN frame.
 * @param id CAN ID (11-bit or 29-bit)
 * @param data Frame data (up to 8 bytes)
 * @param len Data length
 * @param extended true for 29-bit extended ID
 * @return true if sent successfully
 */
bool canbus_send(uint32_t id, const uint8_t* data, uint8_t len, bool extended);

/**
 * Get total frames received since init.
 */
uint32_t canbus_frames_received(void);

#endif /* PLEXUS_CANBUS_H */
