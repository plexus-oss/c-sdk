/**
 * @file adc.h
 * @brief ESP32 built-in ADC reading for analog sensors
 *
 * Reads ESP32 ADC1 channels (GPIO 32-39) and sends voltage values
 * as telemetry metrics. Useful for analog sensors like thermocouples,
 * strain gauges, potentiometers, load cells, and soil moisture sensors.
 */

#ifndef PLEXUS_ADC_H
#define PLEXUS_ADC_H

#include "plexus.h"
#include <stdbool.h>

/** Maximum number of ADC channels to monitor */
#define ADC_MAX_CHANNELS 8

/**
 * Configure an ADC channel for monitoring.
 * @param channel ADC1 channel number (0-7, maps to GPIO 36,37,38,39,32,33,34,35)
 * @param metric_name Metric name to use in telemetry (e.g., "adc.ch0")
 * @param attenuation Attenuation level: 0=0dB(1.1V), 1=2.5dB, 2=6dB, 3=11dB(3.3V)
 * @return true if channel was added, false if full
 */
bool adc_add_channel(int channel, const char* metric_name, int attenuation);

/**
 * Auto-detect connected ADC channels.
 * Reads all 8 ADC1 channels and adds those showing a non-rail voltage.
 * @return Number of active channels found
 */
int adc_auto_detect(void);

/**
 * Register ADC metric names with the heartbeat registry.
 */
void adc_register_metrics(plexus_client_t* client);

/**
 * Read all configured ADC channels and send as telemetry.
 */
void adc_read_all(plexus_client_t* client);

#endif /* PLEXUS_ADC_H */
