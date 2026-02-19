/**
 * @file gps.c
 * @brief GPS NMEA parser implementation
 *
 * Parses $GPGGA and $GPRMC sentences for position, altitude, speed.
 * Minimal parser — no external NMEA library needed.
 */

#include "gps.h"
#include "plexus_firmware_config.h"
#include "plexus.h"

#include "driver/uart.h"
#include "esp_log.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char* TAG = "gps";

#define GPS_BUF_SIZE  512
#define GPS_LINE_SIZE 128

static int s_uart_num = -1;
static gps_data_t s_data = {0};
static char s_line_buf[GPS_LINE_SIZE];
static int s_line_pos = 0;

/* ── NMEA coordinate parsing ──────────────────────────────────────────────── */

/**
 * Parse NMEA latitude/longitude: DDMM.MMMMM → decimal degrees
 */
static double nmea_to_decimal(const char* field, const char* dir) {
    if (!field || !dir || field[0] == '\0') return 0.0;

    double raw = atof(field);
    int degrees = (int)(raw / 100.0);
    double minutes = raw - (degrees * 100.0);
    double decimal = degrees + (minutes / 60.0);

    if (*dir == 'S' || *dir == 'W') decimal = -decimal;
    return decimal;
}

/**
 * Split NMEA sentence into fields by comma.
 * Returns number of fields parsed.
 */
static int nmea_split(char* sentence, char** fields, int max_fields) {
    int count = 0;
    fields[count++] = sentence;

    for (char* p = sentence; *p && count < max_fields; p++) {
        if (*p == ',') {
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    return count;
}

/* ── NMEA sentence handlers ───────────────────────────────────────────────── */

/**
 * Parse $GPGGA (or $GNGGA) — Global Positioning System Fix Data
 * Fields: time, lat, N/S, lon, E/W, quality, numSV, HDOP, alt, M, ...
 */
static void parse_gga(char* sentence) {
    char* fields[15];
    int n = nmea_split(sentence, fields, 15);
    if (n < 10) return;

    int quality = atoi(fields[6]);
    if (quality == 0) {
        s_data.valid = false;
        return;
    }

    s_data.latitude = nmea_to_decimal(fields[2], fields[3]);
    s_data.longitude = nmea_to_decimal(fields[4], fields[5]);
    s_data.satellites = atoi(fields[7]);
    s_data.hdop = (float)atof(fields[8]);
    s_data.altitude_m = (float)atof(fields[9]);
    s_data.valid = true;
}

/**
 * Parse $GPRMC (or $GNRMC) — Recommended Minimum Navigation Information
 * Fields: time, status, lat, N/S, lon, E/W, speed_knots, course, date, ...
 */
static void parse_rmc(char* sentence) {
    char* fields[13];
    int n = nmea_split(sentence, fields, 13);
    if (n < 8) return;

    if (fields[2][0] != 'A') {
        /* 'V' = void (no fix) */
        return;
    }

    s_data.latitude = nmea_to_decimal(fields[3], fields[4]);
    s_data.longitude = nmea_to_decimal(fields[5], fields[6]);
    s_data.speed_knots = (float)atof(fields[7]);
    s_data.valid = true;
}

/**
 * Process a complete NMEA sentence.
 */
static void process_sentence(char* sentence) {
    /* Verify checksum (optional — skip if no asterisk) */
    char* star = strchr(sentence, '*');
    if (star) *star = '\0'; /* Truncate at checksum for parsing */

    /* Route to appropriate parser */
    if (strncmp(sentence, "$GPGGA", 6) == 0 || strncmp(sentence, "$GNGGA", 6) == 0) {
        parse_gga(sentence + 7);
    } else if (strncmp(sentence, "$GPRMC", 6) == 0 || strncmp(sentence, "$GNRMC", 6) == 0) {
        parse_rmc(sentence + 7);
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

bool gps_init(int uart_num, int rx_pin, int tx_pin, int baud) {
    uart_config_t uart_config = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(uart_num, GPS_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART install failed: %s", esp_err_to_name(err));
        return false;
    }

    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    s_uart_num = uart_num;
    ESP_LOGI(TAG, "GPS initialized on UART%d (RX=%d, TX=%d, %d baud)",
             uart_num, rx_pin, tx_pin, baud);

    /* Wait briefly and check if we receive any data */
    uint8_t probe_buf[32];
    int len = uart_read_bytes(uart_num, probe_buf, sizeof(probe_buf), pdMS_TO_TICKS(3000));
    if (len > 0) {
        ESP_LOGI(TAG, "GPS module detected (%d bytes received)", len);
        return true;
    }

    ESP_LOGW(TAG, "No GPS data received — module may not be connected");
    /* Don't uninstall — the module might start sending later */
    return false;
}

bool gps_init_default(void) {
    return gps_init(2, 16, 17, 9600);
}

void gps_register_metrics(plexus_client_t* client) {
    if (!client) return;

#if PLEXUS_ENABLE_HEARTBEAT
    plexus_register_metric(client, "gps.latitude");
    plexus_register_metric(client, "gps.longitude");
    plexus_register_metric(client, "gps.altitude");
    plexus_register_metric(client, "gps.speed_knots");
    plexus_register_metric(client, "gps.satellites");
    plexus_register_metric(client, "gps.hdop");
#endif
}

void gps_read(plexus_client_t* client) {
    if (!client || s_uart_num < 0) return;

    /* Read available UART data (non-blocking) */
    uint8_t buf[128];
    int len = uart_read_bytes(s_uart_num, buf, sizeof(buf), 0);

    for (int i = 0; i < len; i++) {
        char c = (char)buf[i];

        if (c == '$') {
            /* Start of new sentence */
            s_line_pos = 0;
            s_line_buf[s_line_pos++] = c;
        } else if (c == '\n' || c == '\r') {
            if (s_line_pos > 5) {
                s_line_buf[s_line_pos] = '\0';
                process_sentence(s_line_buf);
            }
            s_line_pos = 0;
        } else if (s_line_pos > 0 && s_line_pos < GPS_LINE_SIZE - 1) {
            s_line_buf[s_line_pos++] = c;
        }
    }

    /* Send latest valid fix as telemetry */
    if (s_data.valid) {
        plexus_send_number(client, "gps.latitude", s_data.latitude);
        plexus_send_number(client, "gps.longitude", s_data.longitude);
        plexus_send_number(client, "gps.altitude", (double)s_data.altitude_m);
        plexus_send_number(client, "gps.speed_knots", (double)s_data.speed_knots);
        plexus_send_number(client, "gps.satellites", (double)s_data.satellites);
        plexus_send_number(client, "gps.hdop", (double)s_data.hdop);
    }
}

const gps_data_t* gps_get_data(void) {
    return &s_data;
}
