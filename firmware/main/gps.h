/**
 * @file gps.h
 * @brief GPS NMEA parser over UART for Plexus firmware
 *
 * Reads NMEA sentences from a GPS module connected via UART
 * (e.g., NEO-6M, NEO-7M, u-blox). Sends latitude, longitude,
 * altitude, speed, and satellite count as telemetry.
 *
 * Default: UART2, RX=GPIO16, TX=GPIO17, 9600 baud
 */

#ifndef PLEXUS_GPS_H
#define PLEXUS_GPS_H

#include "plexus.h"
#include <stdbool.h>

/** GPS fix data */
typedef struct {
    double latitude;
    double longitude;
    float altitude_m;
    float speed_knots;
    float hdop;
    int satellites;
    bool valid;
} gps_data_t;

/**
 * Initialize GPS UART.
 * @param uart_num UART port number (default: 2)
 * @param rx_pin GPIO number for RX (default: 16)
 * @param tx_pin GPIO number for TX (default: 17)
 * @param baud Baud rate (default: 9600)
 * @return true if initialization succeeded
 */
bool gps_init(int uart_num, int rx_pin, int tx_pin, int baud);

/**
 * Initialize GPS with default pins (UART2, RX=16, TX=17, 9600 baud).
 * Returns false if no GPS data received within 3 seconds.
 */
bool gps_init_default(void);

/**
 * Register GPS metric names with heartbeat.
 */
void gps_register_metrics(plexus_client_t* client);

/**
 * Read latest GPS data and send as telemetry.
 * Non-blocking — reads available UART data and parses any complete sentences.
 */
void gps_read(plexus_client_t* client);

/**
 * Get the latest GPS fix data.
 */
const gps_data_t* gps_get_data(void);

#endif /* PLEXUS_GPS_H */
