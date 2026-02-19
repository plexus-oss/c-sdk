/**
 * @file plexus_sensors.c
 * @brief I2C sensor discovery and built-in drivers for Plexus C SDK
 *
 * When PLEXUS_ENABLE_SENSOR_DISCOVERY is set, the SDK can:
 * - Scan the I2C bus for known sensors
 * - Probe chip ID registers to confirm sensor identity
 * - Read sensor values via built-in drivers (BME280, MPU6050)
 * - Automatically register detected metrics for heartbeat reporting
 *
 * Sensor descriptors (name, addresses, metrics) are const and live in flash.
 * Only the detected_sensors array (~80 bytes) is allocated in client RAM.
 */

#include "plexus_internal.h"

#if PLEXUS_ENABLE_SENSOR_DISCOVERY

#include <string.h>

/* ========================================================================= */
/* Built-in sensor descriptors (const — lives in flash)                      */
/* ========================================================================= */

/* --- BME280 Environmental Sensor --- */

#if PLEXUS_SENSOR_BME280
static const char* const s_bme280_metrics[] = {
    "temperature", "humidity", "pressure"
};

static bool bme280_probe(uint8_t addr) {
    uint8_t chip_id = 0;
    if (plexus_hal_i2c_read_reg(addr, 0xD0, &chip_id) != PLEXUS_OK) {
        return false;
    }
    /* BME280 chip_id = 0x60, BMP280 = 0x58 */
    return (chip_id == 0x60 || chip_id == 0x58);
}

static plexus_err_t bme280_read(uint8_t addr, float* values, uint8_t count) {
    if (count < 3) return PLEXUS_ERR_INVALID_ARG;

    /* Trigger forced measurement: ctrl_hum then ctrl_meas */
    plexus_err_t err;
    err = plexus_hal_i2c_write_reg(addr, 0xF2, 0x01); /* humidity oversampling x1 */
    if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;
    err = plexus_hal_i2c_write_reg(addr, 0xF4, 0x25); /* temp+press x1, forced mode */
    if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;

    /* Wait for measurement (~10ms typical) */
    plexus_hal_delay_ms(12);

    /* Read raw data registers 0xF7-0xFE (8 bytes) */
    uint8_t raw[8];
    for (int i = 0; i < 8; i++) {
        err = plexus_hal_i2c_read_reg(addr, (uint8_t)(0xF7 + i), &raw[i]);
        if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;
    }

    /* Approximate conversion without calibration data.
     * These give ballpark readings for dashboard verification.
     * For production accuracy, users should implement calibrated drivers. */
    int32_t adc_p = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_t = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    int32_t adc_h = ((int32_t)raw[6] << 8) | raw[7];

    /* Approximate temperature (assumes typical calibration) */
    values[0] = (float)(adc_t - 409600) / 16384.0f * 5.0f + 25.0f;

    /* Approximate humidity (rough linear mapping) */
    values[1] = (float)adc_h / 419430.0f * 100.0f;

    /* Approximate pressure (rough mapping to hPa) */
    values[2] = (float)adc_p / 25600.0f;

    return PLEXUS_OK;
}

static const plexus_sensor_descriptor_t s_bme280_descriptor = {
    .name = "BME280",
    .description = "Environmental sensor",
    .metrics = s_bme280_metrics,
    .metric_count = 3,
    .i2c_addrs = {0x76, 0x77, 0, 0},
    .default_sample_rate_hz = 1.0f,
    .probe = bme280_probe,
    .read = bme280_read,
};
#endif /* PLEXUS_SENSOR_BME280 */

/* --- MPU6050 IMU --- */

#if PLEXUS_SENSOR_MPU6050
static const char* const s_mpu6050_metrics[] = {
    "accel_x", "accel_y", "accel_z",
    "gyro_x", "gyro_y", "gyro_z"
};

static bool mpu6050_probe(uint8_t addr) {
    uint8_t who_am_i = 0;
    if (plexus_hal_i2c_read_reg(addr, 0x75, &who_am_i) != PLEXUS_OK) {
        return false;
    }
    /* MPU6050 returns 0x68, MPU6500 returns 0x70, MPU9250 returns 0x71/0x73 */
    return (who_am_i == 0x68 || who_am_i == 0x70 ||
            who_am_i == 0x71 || who_am_i == 0x73);
}

static plexus_err_t mpu6050_read(uint8_t addr, float* values, uint8_t count) {
    if (count < 6) return PLEXUS_ERR_INVALID_ARG;

    /* Wake up MPU6050 (clear sleep bit) */
    plexus_hal_i2c_write_reg(addr, 0x6B, 0x00);

    /* Read 14 bytes starting from 0x3B (accel + temp + gyro) */
    uint8_t raw[14];
    plexus_err_t err;
    for (int i = 0; i < 14; i++) {
        err = plexus_hal_i2c_read_reg(addr, (uint8_t)(0x3B + i), &raw[i]);
        if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;
    }

    /* Accelerometer: ±2g default, 16384 LSB/g */
    values[0] = (float)((int16_t)((raw[0] << 8) | raw[1])) / 16384.0f;
    values[1] = (float)((int16_t)((raw[2] << 8) | raw[3])) / 16384.0f;
    values[2] = (float)((int16_t)((raw[4] << 8) | raw[5])) / 16384.0f;

    /* Skip raw[6-7] (temperature) */

    /* Gyroscope: ±250°/s default, 131 LSB/(°/s) */
    values[3] = (float)((int16_t)((raw[8] << 8) | raw[9])) / 131.0f;
    values[4] = (float)((int16_t)((raw[10] << 8) | raw[11])) / 131.0f;
    values[5] = (float)((int16_t)((raw[12] << 8) | raw[13])) / 131.0f;

    return PLEXUS_OK;
}

static const plexus_sensor_descriptor_t s_mpu6050_descriptor = {
    .name = "MPU6050",
    .description = "6-axis IMU",
    .metrics = s_mpu6050_metrics,
    .metric_count = 6,
    .i2c_addrs = {0x68, 0x69, 0, 0},
    .default_sample_rate_hz = 10.0f,
    .probe = mpu6050_probe,
    .read = mpu6050_read,
};
#endif /* PLEXUS_SENSOR_MPU6050 */

/* --- INA219 Current/Power Monitor --- */

#if PLEXUS_SENSOR_INA219
static const char* const s_ina219_metrics[] = {
    "bus_voltage", "shunt_voltage", "current_ma", "power_mw"
};

static bool ina219_probe(uint8_t addr) {
    /* INA219 has no WHO_AM_I — verify by reading config register (0x00).
     * Default config value is 0x399F. Check high byte is reasonable. */
    uint8_t msb = 0;
    if (plexus_hal_i2c_read_reg(addr, 0x00, &msb) != PLEXUS_OK) return false;
    return (msb == 0x39 || msb == 0x01 || msb == 0x00);
}

static plexus_err_t ina219_read(uint8_t addr, float* values, uint8_t count) {
    if (count < 4) return PLEXUS_ERR_INVALID_ARG;

    uint8_t raw[2];
    plexus_err_t err;

    /* Bus voltage register (0x02): bits [15:3] = voltage, LSB = 4mV */
    err = plexus_hal_i2c_read_reg(addr, 0x02, &raw[0]);
    if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;
    err = plexus_hal_i2c_read_reg(addr, 0x03, &raw[1]);
    if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;
    /* Note: reading two bytes via sequential single-byte reads.
     * Register auto-increments within a register pair on INA219. */
    int16_t bus_raw = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    values[0] = (float)((bus_raw >> 3) * 4) / 1000.0f; /* Volts */

    /* Shunt voltage register (0x01): LSB = 10uV */
    err = plexus_hal_i2c_read_reg(addr, 0x01, &raw[0]);
    if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;
    /* Second byte of shunt register — re-read at offset */
    uint8_t shunt_lsb = 0;
    /* Use a two-byte read pattern: write reg addr, then read two bytes */
    plexus_hal_i2c_write_reg(addr, 0x01, 0); /* Point to register */
    err = plexus_hal_i2c_read_reg(addr, 0x01, &raw[0]);
    if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;

    /* Simplified: read shunt as single high byte, approximate */
    int8_t shunt_msb = (int8_t)raw[0];
    values[1] = (float)shunt_msb * 2.56f; /* mV approximate */

    /* Current — approximate using default calibration (0.1 ohm shunt) */
    values[2] = values[1] / 0.1f; /* mA = shunt_mV / R_shunt */

    /* Power = bus_voltage * current */
    values[3] = values[0] * values[2]; /* mW */

    return PLEXUS_OK;
}

static const plexus_sensor_descriptor_t s_ina219_descriptor = {
    .name = "INA219",
    .description = "Current/power monitor",
    .metrics = s_ina219_metrics,
    .metric_count = 4,
    .i2c_addrs = {0x40, 0x41, 0x44, 0x45},
    .default_sample_rate_hz = 1.0f,
    .probe = ina219_probe,
    .read = ina219_read,
};
#endif /* PLEXUS_SENSOR_INA219 */

/* --- ADS1115 16-bit ADC --- */

#if PLEXUS_SENSOR_ADS1115
static const char* const s_ads1115_metrics[] = {
    "adc_ch0", "adc_ch1", "adc_ch2", "adc_ch3"
};

static bool ads1115_probe(uint8_t addr) {
    /* Read config register (0x01). Default is 0x8583. */
    uint8_t msb = 0;
    if (plexus_hal_i2c_read_reg(addr, 0x01, &msb) != PLEXUS_OK) return false;
    return (msb == 0x85 || msb == 0x05 || msb == 0xC5);
}

static plexus_err_t ads1115_read(uint8_t addr, float* values, uint8_t count) {
    if (count < 4) return PLEXUS_ERR_INVALID_ARG;

    /* PGA = ±4.096V (default), LSB = 0.125mV for 16-bit */
    const float lsb_mv = 0.125f;

    for (uint8_t ch = 0; ch < 4; ch++) {
        /* Configure: single-shot, AINx vs GND, ±4.096V, 128SPS */
        uint16_t config = 0xC183 | ((uint16_t)(0x04 + ch) << 12);
        uint8_t cfg_msb = (uint8_t)(config >> 8);
        uint8_t cfg_lsb = (uint8_t)(config & 0xFF);

        plexus_hal_i2c_write_reg(addr, 0x01, cfg_msb);
        /* Write LSB as a second byte — simplified approach */
        plexus_hal_i2c_write_reg(addr, 0x01, cfg_lsb);

        /* Wait for conversion (~8ms at 128SPS) */
        plexus_hal_delay_ms(10);

        /* Read conversion register (0x00) */
        uint8_t raw[2] = {0};
        plexus_err_t err = plexus_hal_i2c_read_reg(addr, 0x00, &raw[0]);
        if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;

        int16_t raw_val = (int16_t)((uint16_t)raw[0] << 8);
        values[ch] = (float)raw_val * lsb_mv / 1000.0f; /* Convert to Volts */
    }

    return PLEXUS_OK;
}

static const plexus_sensor_descriptor_t s_ads1115_descriptor = {
    .name = "ADS1115",
    .description = "16-bit ADC",
    .metrics = s_ads1115_metrics,
    .metric_count = 4,
    .i2c_addrs = {0x48, 0x49, 0x4A, 0x4B},
    .default_sample_rate_hz = 1.0f,
    .probe = ads1115_probe,
    .read = ads1115_read,
};
#endif /* PLEXUS_SENSOR_ADS1115 */

/* --- SHT3x Humidity/Temperature Sensor --- */

#if PLEXUS_SENSOR_SHT3X
static const char* const s_sht3x_metrics[] = {
    "sht_temperature", "sht_humidity"
};

static bool sht3x_probe(uint8_t addr) {
    /* Send soft reset command (0x30A2) and check ACK */
    plexus_hal_i2c_write_reg(addr, 0x30, 0xA2);
    plexus_hal_delay_ms(2);
    /* Read status register (0xF32D) — if device responds, it's an SHT3x */
    uint8_t val = 0;
    return (plexus_hal_i2c_read_reg(addr, 0xF3, &val) == PLEXUS_OK);
}

static plexus_err_t sht3x_read(uint8_t addr, float* values, uint8_t count) {
    if (count < 2) return PLEXUS_ERR_INVALID_ARG;

    /* Trigger single-shot measurement: high repeatability, clock stretching */
    plexus_hal_i2c_write_reg(addr, 0x2C, 0x06);
    plexus_hal_delay_ms(16); /* Max measurement time */

    /* Read 6 bytes: temp_msb, temp_lsb, temp_crc, hum_msb, hum_lsb, hum_crc */
    uint8_t raw[6];
    plexus_err_t err;
    for (int i = 0; i < 6; i++) {
        err = plexus_hal_i2c_read_reg(addr, (uint8_t)i, &raw[i]);
        if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;
    }

    uint16_t temp_raw = ((uint16_t)raw[0] << 8) | raw[1];
    uint16_t hum_raw = ((uint16_t)raw[3] << 8) | raw[4];

    /* Conversion formulas from SHT3x datasheet */
    values[0] = -45.0f + 175.0f * ((float)temp_raw / 65535.0f);
    values[1] = 100.0f * ((float)hum_raw / 65535.0f);

    return PLEXUS_OK;
}

static const plexus_sensor_descriptor_t s_sht3x_descriptor = {
    .name = "SHT3x",
    .description = "Precision humidity/temperature",
    .metrics = s_sht3x_metrics,
    .metric_count = 2,
    .i2c_addrs = {0x44, 0x45, 0, 0},
    .default_sample_rate_hz = 1.0f,
    .probe = sht3x_probe,
    .read = sht3x_read,
};
#endif /* PLEXUS_SENSOR_SHT3X */

/* --- BH1750 Ambient Light Sensor --- */

#if PLEXUS_SENSOR_BH1750
static const char* const s_bh1750_metrics[] = {
    "light_lux"
};

static bool bh1750_probe(uint8_t addr) {
    /* Power on the device and see if it ACKs */
    return (plexus_hal_i2c_write_reg(addr, 0x01, 0x00) == PLEXUS_OK);
}

static plexus_err_t bh1750_read(uint8_t addr, float* values, uint8_t count) {
    if (count < 1) return PLEXUS_ERR_INVALID_ARG;

    /* Trigger one-time high-resolution measurement (0x20) */
    plexus_hal_i2c_write_reg(addr, 0x20, 0x00);
    plexus_hal_delay_ms(180); /* Max measurement time for Hi-Res mode */

    /* Read 2 bytes of light data */
    uint8_t raw[2] = {0};
    plexus_err_t err;
    err = plexus_hal_i2c_read_reg(addr, 0x00, &raw[0]);
    if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;
    err = plexus_hal_i2c_read_reg(addr, 0x01, &raw[1]);
    if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;

    uint16_t raw_val = ((uint16_t)raw[0] << 8) | raw[1];
    values[0] = (float)raw_val / 1.2f; /* Convert to lux */

    return PLEXUS_OK;
}

static const plexus_sensor_descriptor_t s_bh1750_descriptor = {
    .name = "BH1750",
    .description = "Ambient light sensor",
    .metrics = s_bh1750_metrics,
    .metric_count = 1,
    .i2c_addrs = {0x23, 0x5C, 0, 0},
    .default_sample_rate_hz = 1.0f,
    .probe = bh1750_probe,
    .read = bh1750_read,
};
#endif /* PLEXUS_SENSOR_BH1750 */

/* --- VL53L0X Time-of-Flight Distance Sensor --- */

#if PLEXUS_SENSOR_VL53L0X
static const char* const s_vl53l0x_metrics[] = {
    "distance_mm"
};

static bool vl53l0x_probe(uint8_t addr) {
    /* Model ID register (0xC0) should return 0xEE */
    uint8_t model_id = 0;
    if (plexus_hal_i2c_read_reg(addr, 0xC0, &model_id) != PLEXUS_OK) return false;
    return (model_id == 0xEE);
}

static plexus_err_t vl53l0x_read(uint8_t addr, float* values, uint8_t count) {
    if (count < 1) return PLEXUS_ERR_INVALID_ARG;

    /* Simplified single-shot ranging (not using full ST API).
     * Start measurement */
    plexus_hal_i2c_write_reg(addr, 0x00, 0x01); /* SYSRANGE_START */
    plexus_hal_delay_ms(50); /* Typical measurement time */

    /* Wait for measurement complete — poll interrupt status */
    uint8_t status = 0;
    int retries = 20;
    while (retries-- > 0) {
        plexus_hal_i2c_read_reg(addr, 0x13, &status); /* RESULT_INTERRUPT_STATUS */
        if (status & 0x07) break;
        plexus_hal_delay_ms(5);
    }

    if (!(status & 0x07)) return PLEXUS_ERR_I2C; /* Timeout */

    /* Read range result (0x14 + 0x15 = 16-bit mm) */
    uint8_t raw[2] = {0};
    plexus_err_t err;
    err = plexus_hal_i2c_read_reg(addr, 0x14, &raw[0]);
    if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;
    err = plexus_hal_i2c_read_reg(addr, 0x15, &raw[1]);
    if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;

    uint16_t range_mm = ((uint16_t)raw[0] << 8) | raw[1];

    /* Clear interrupt */
    plexus_hal_i2c_write_reg(addr, 0x0B, 0x01);

    /* 8190 = out of range */
    values[0] = (range_mm < 8190) ? (float)range_mm : -1.0f;

    return PLEXUS_OK;
}

static const plexus_sensor_descriptor_t s_vl53l0x_descriptor = {
    .name = "VL53L0X",
    .description = "Time-of-flight distance",
    .metrics = s_vl53l0x_metrics,
    .metric_count = 1,
    .i2c_addrs = {0x29, 0, 0, 0},
    .default_sample_rate_hz = 5.0f,
    .probe = vl53l0x_probe,
    .read = vl53l0x_read,
};
#endif /* PLEXUS_SENSOR_VL53L0X */

/* --- HMC5883L / QMC5883L Magnetometer --- */

#if PLEXUS_SENSOR_QMC5883L || PLEXUS_SENSOR_HMC5883L
static const char* const s_mag_metrics[] = {
    "mag_x", "mag_y", "mag_z"
};
#endif

#if PLEXUS_SENSOR_QMC5883L
static bool qmc5883l_probe(uint8_t addr) {
    /* QMC5883L chip ID register (0x0D) should return 0xFF */
    uint8_t chip_id = 0;
    if (plexus_hal_i2c_read_reg(addr, 0x0D, &chip_id) != PLEXUS_OK) return false;
    return (chip_id == 0xFF);
}

static plexus_err_t qmc5883l_read(uint8_t addr, float* values, uint8_t count) {
    if (count < 3) return PLEXUS_ERR_INVALID_ARG;

    /* Configure: continuous mode, 200Hz, 2G range, 512 oversampling */
    plexus_hal_i2c_write_reg(addr, 0x09, 0x0D); /* Control register 1 */
    plexus_hal_delay_ms(10);

    /* Check data ready */
    uint8_t status = 0;
    plexus_hal_i2c_read_reg(addr, 0x06, &status);
    if (!(status & 0x01)) {
        plexus_hal_delay_ms(10); /* Wait a bit more */
    }

    /* Read 6 bytes: X_LSB, X_MSB, Y_LSB, Y_MSB, Z_LSB, Z_MSB */
    uint8_t raw[6];
    plexus_err_t err;
    for (int i = 0; i < 6; i++) {
        err = plexus_hal_i2c_read_reg(addr, (uint8_t)i, &raw[i]);
        if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;
    }

    /* QMC5883L: LSB first, 2G range → 12000 LSB/Gauss */
    values[0] = (float)((int16_t)((raw[1] << 8) | raw[0])) / 12000.0f; /* Gauss */
    values[1] = (float)((int16_t)((raw[3] << 8) | raw[2])) / 12000.0f;
    values[2] = (float)((int16_t)((raw[5] << 8) | raw[4])) / 12000.0f;

    return PLEXUS_OK;
}

static const plexus_sensor_descriptor_t s_qmc5883l_descriptor = {
    .name = "QMC5883L",
    .description = "3-axis magnetometer",
    .metrics = s_mag_metrics,
    .metric_count = 3,
    .i2c_addrs = {0x0D, 0, 0, 0},
    .default_sample_rate_hz = 5.0f,
    .probe = qmc5883l_probe,
    .read = qmc5883l_read,
};
#endif /* PLEXUS_SENSOR_QMC5883L */

#if PLEXUS_SENSOR_HMC5883L
static bool hmc5883l_probe(uint8_t addr) {
    /* HMC5883L identification registers: A=0x48, B=0x34, C=0x33 */
    uint8_t id_a = 0;
    if (plexus_hal_i2c_read_reg(addr, 0x0A, &id_a) != PLEXUS_OK) return false;
    return (id_a == 0x48);
}

static plexus_err_t hmc5883l_read(uint8_t addr, float* values, uint8_t count) {
    if (count < 3) return PLEXUS_ERR_INVALID_ARG;

    /* Configure: 8 samples avg, 15Hz, normal measurement */
    plexus_hal_i2c_write_reg(addr, 0x00, 0x70); /* Config A */
    plexus_hal_i2c_write_reg(addr, 0x01, 0x20); /* Config B: gain 1.3Ga */
    plexus_hal_i2c_write_reg(addr, 0x02, 0x00); /* Continuous measurement */
    plexus_hal_delay_ms(70); /* Wait for measurement */

    /* Read 6 bytes: X_MSB, X_LSB, Z_MSB, Z_LSB, Y_MSB, Y_LSB */
    uint8_t raw[6];
    plexus_err_t err;
    for (int i = 0; i < 6; i++) {
        err = plexus_hal_i2c_read_reg(addr, (uint8_t)(0x03 + i), &raw[i]);
        if (err != PLEXUS_OK) return PLEXUS_ERR_I2C;
    }

    /* HMC5883L: MSB first. Default gain 1090 LSB/Gauss */
    values[0] = (float)((int16_t)((raw[0] << 8) | raw[1])) / 1090.0f; /* Gauss */
    values[1] = (float)((int16_t)((raw[4] << 8) | raw[5])) / 1090.0f; /* Y is last */
    values[2] = (float)((int16_t)((raw[2] << 8) | raw[3])) / 1090.0f;

    return PLEXUS_OK;
}

static const plexus_sensor_descriptor_t s_hmc5883l_descriptor = {
    .name = "HMC5883L",
    .description = "3-axis magnetometer",
    .metrics = s_mag_metrics,
    .metric_count = 3,
    .i2c_addrs = {0x1E, 0, 0, 0},
    .default_sample_rate_hz = 5.0f,
    .probe = hmc5883l_probe,
    .read = hmc5883l_read,
};
#endif /* PLEXUS_SENSOR_HMC5883L */

/* Built-in sensor registry — only includes enabled sensors */
static const plexus_sensor_descriptor_t* const s_builtin_sensors[] = {
#if PLEXUS_SENSOR_BME280
    &s_bme280_descriptor,
#endif
#if PLEXUS_SENSOR_MPU6050
    &s_mpu6050_descriptor,
#endif
#if PLEXUS_SENSOR_INA219
    &s_ina219_descriptor,
#endif
#if PLEXUS_SENSOR_ADS1115
    &s_ads1115_descriptor,
#endif
#if PLEXUS_SENSOR_SHT3X
    &s_sht3x_descriptor,
#endif
#if PLEXUS_SENSOR_BH1750
    &s_bh1750_descriptor,
#endif
#if PLEXUS_SENSOR_VL53L0X
    &s_vl53l0x_descriptor,
#endif
#if PLEXUS_SENSOR_QMC5883L
    &s_qmc5883l_descriptor,
#endif
#if PLEXUS_SENSOR_HMC5883L
    &s_hmc5883l_descriptor,
#endif
};
#define BUILTIN_SENSOR_COUNT (sizeof(s_builtin_sensors) / sizeof(s_builtin_sensors[0]))

/* Custom sensor registry */
static const plexus_sensor_descriptor_t* s_custom_sensors[PLEXUS_MAX_CUSTOM_SENSORS];
static uint8_t s_custom_sensor_count = 0;

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

plexus_err_t plexus_sensor_register(const plexus_sensor_descriptor_t* descriptor) {
    if (!descriptor) return PLEXUS_ERR_NULL_PTR;
    if (s_custom_sensor_count >= PLEXUS_MAX_CUSTOM_SENSORS) return PLEXUS_ERR_BUFFER_FULL;

    s_custom_sensors[s_custom_sensor_count++] = descriptor;
    return PLEXUS_OK;
}

/**
 * Check if a given I2C address matches any address in a sensor descriptor.
 */
static bool addr_matches(const plexus_sensor_descriptor_t* desc, uint8_t addr) {
    for (int i = 0; i < 4 && desc->i2c_addrs[i] != 0; i++) {
        if (desc->i2c_addrs[i] == addr) return true;
    }
    return false;
}

plexus_err_t plexus_scan_sensors(plexus_client_t* client) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;

    client->detected_sensor_count = 0;

    for (uint8_t addr = PLEXUS_I2C_SCAN_START; addr < PLEXUS_I2C_SCAN_END; addr++) {
        if (client->detected_sensor_count >= PLEXUS_MAX_DETECTED_SENSORS) break;

        /* Check if device responds at this address */
        if (!plexus_hal_i2c_probe(addr)) continue;

        /* Try to match against built-in sensors */
        const plexus_sensor_descriptor_t* matched = NULL;

        for (size_t i = 0; i < BUILTIN_SENSOR_COUNT; i++) {
            if (addr_matches(s_builtin_sensors[i], addr)) {
                /* If descriptor has a probe function, use it to verify */
                if (s_builtin_sensors[i]->probe) {
                    if (s_builtin_sensors[i]->probe(addr)) {
                        matched = s_builtin_sensors[i];
                        break;
                    }
                } else {
                    matched = s_builtin_sensors[i];
                    break;
                }
            }
        }

        /* Try custom sensors */
        if (!matched) {
            for (uint8_t i = 0; i < s_custom_sensor_count; i++) {
                if (addr_matches(s_custom_sensors[i], addr)) {
                    if (s_custom_sensors[i]->probe) {
                        if (s_custom_sensors[i]->probe(addr)) {
                            matched = s_custom_sensors[i];
                            break;
                        }
                    } else {
                        matched = s_custom_sensors[i];
                        break;
                    }
                }
            }
        }

        if (matched) {
            plexus_detected_sensor_t* ds = &client->detected_sensors[client->detected_sensor_count];
            ds->descriptor = matched;
            ds->addr = addr;
            ds->active = true;
            client->detected_sensor_count++;

#if PLEXUS_DEBUG
            plexus_hal_log("Detected %s at 0x%02X", matched->name, addr);
#endif

            /* Auto-register metrics for heartbeat */
#if PLEXUS_ENABLE_HEARTBEAT
            for (uint8_t m = 0; m < matched->metric_count; m++) {
                plexus_register_metric(client, matched->metrics[m]);
            }
#endif
        }
    }

#if PLEXUS_DEBUG
    plexus_hal_log("I2C scan complete: %d sensors detected", client->detected_sensor_count);
#endif

    return PLEXUS_OK;
}

plexus_err_t plexus_sensor_read_all(plexus_client_t* client) {
    if (!client) return PLEXUS_ERR_NULL_PTR;
    if (!client->initialized) return PLEXUS_ERR_NOT_INITIALIZED;
    if (client->detected_sensor_count == 0) return PLEXUS_OK;

    plexus_err_t last_err = PLEXUS_OK;

    for (uint8_t i = 0; i < client->detected_sensor_count; i++) {
        plexus_detected_sensor_t* ds = &client->detected_sensors[i];
        if (!ds->active || !ds->descriptor || !ds->descriptor->read) continue;

        float values[PLEXUS_MAX_SENSOR_METRICS];
        plexus_err_t err = ds->descriptor->read(ds->addr, values, ds->descriptor->metric_count);

        if (err != PLEXUS_OK) {
#if PLEXUS_DEBUG
            plexus_hal_log("Failed to read %s at 0x%02X: %s",
                           ds->descriptor->name, ds->addr, plexus_strerror(err));
#endif
            last_err = err;
            continue;
        }

        /* Queue each metric value */
        for (uint8_t m = 0; m < ds->descriptor->metric_count; m++) {
            plexus_err_t send_err = plexus_send_number(client, ds->descriptor->metrics[m], (double)values[m]);
            if (send_err != PLEXUS_OK) {
                last_err = send_err;
            }
        }
    }

    return last_err;
}

uint8_t plexus_detected_sensor_count(const plexus_client_t* client) {
    if (!client || !client->initialized) return 0;
    return client->detected_sensor_count;
}

const plexus_detected_sensor_t* plexus_detected_sensor(const plexus_client_t* client, uint8_t index) {
    if (!client || !client->initialized) return NULL;
    if (index >= client->detected_sensor_count) return NULL;
    return &client->detected_sensors[index];
}

#endif /* PLEXUS_ENABLE_SENSOR_DISCOVERY */
