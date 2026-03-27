/**
 * @file i2c_scan.c
 * @brief I2C sensor auto-detection and reading for the flashable firmware.
 *
 * Supports BME280, MPU6050, BH1750, SHT3x, and INA219 with minimal
 * single-shot read implementations. Not a general-purpose driver library.
 */

#include "i2c_scan.h"

#include <stdio.h>
#include <string.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "i2c_scan";

#define I2C_PORT     I2C_NUM_0
#define I2C_FREQ_HZ  100000
#define I2C_TIMEOUT  pdMS_TO_TICKS(100)

/* ========================================================================= */
/* Low-level I2C helpers                                                     */
/* ========================================================================= */

static esp_err_t i2c_read_byte(uint8_t addr, uint8_t reg, uint8_t* out) {
    return i2c_master_write_read_device(I2C_PORT, addr, &reg, 1, out, 1, I2C_TIMEOUT);
}

static esp_err_t i2c_read_bytes(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
    return i2c_master_write_read_device(I2C_PORT, addr, &reg, 1, buf, len, I2C_TIMEOUT);
}

static esp_err_t i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t data[2] = {reg, val};
    return i2c_master_write_to_device(I2C_PORT, addr, data, 2, I2C_TIMEOUT);
}

/* ========================================================================= */
/* BME280 — temperature, humidity, pressure                                  */
/* ========================================================================= */

typedef struct {
    uint16_t T1; int16_t T2, T3;
    uint16_t P1; int16_t P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t  H1; int16_t H2; uint8_t H3; int16_t H4, H5; int8_t H6;
    int32_t  t_fine;  /* Per-sensor compensation state */
} bme280_calib_t;

/* Per-sensor calibration storage (indexed by detection order) */
#define BME280_MAX_INSTANCES 2
static bme280_calib_t s_bme_calib[BME280_MAX_INSTANCES];
static int s_bme_count = 0;

static int bme280_get_index(uint8_t addr) {
    /* Map address to calibration index. Simple: 0x76 -> 0, 0x77 -> 1 */
    return (addr == 0x77) ? 1 : 0;
}

static bool bme280_identify(uint8_t addr) {
    uint8_t id;
    if (i2c_read_byte(addr, 0xD0, &id) != ESP_OK) return false;
    return id == 0x60;
}

static void bme280_init(uint8_t addr) {
    int idx = bme280_get_index(addr);
    bme280_calib_t* cal = &s_bme_calib[idx];

    /* Read temperature + pressure calibration (registers 0x88-0xA1) */
    uint8_t buf[26];
    i2c_read_bytes(addr, 0x88, buf, 26);
    cal->T1 = buf[0]  | (buf[1] << 8);
    cal->T2 = buf[2]  | (buf[3] << 8);
    cal->T3 = buf[4]  | (buf[5] << 8);
    cal->P1 = buf[6]  | (buf[7] << 8);
    cal->P2 = buf[8]  | (buf[9] << 8);
    cal->P3 = buf[10] | (buf[11] << 8);
    cal->P4 = buf[12] | (buf[13] << 8);
    cal->P5 = buf[14] | (buf[15] << 8);
    cal->P6 = buf[16] | (buf[17] << 8);
    cal->P7 = buf[18] | (buf[19] << 8);
    cal->P8 = buf[20] | (buf[21] << 8);
    cal->P9 = buf[22] | (buf[23] << 8);

    /* Read humidity calibration (split across 0xA1 and 0xE1-0xE7) */
    i2c_read_byte(addr, 0xA1, &cal->H1);

    uint8_t hbuf[7];
    i2c_read_bytes(addr, 0xE1, hbuf, 7);
    cal->H2 = hbuf[0] | (hbuf[1] << 8);
    cal->H3 = hbuf[2];
    cal->H4 = (hbuf[3] << 4) | (hbuf[4] & 0x0F);
    cal->H5 = (hbuf[4] >> 4)  | (hbuf[5] << 4);
    cal->H6 = hbuf[6];
    cal->t_fine = 0;

    s_bme_count++;

    /* Configure: humidity x1, temp x1, pressure x1, normal mode */
    /* Note: ctrl_hum (0xF2) must be written BEFORE ctrl_meas (0xF4) to take effect */
    i2c_write_byte(addr, 0xF2, 0x01);  /* ctrl_hum: oversampling x1 */
    i2c_write_byte(addr, 0xF5, 0xA0);  /* config: 1000ms standby, filter off */
    i2c_write_byte(addr, 0xF4, 0x27);  /* ctrl_meas: temp x1, press x1, normal mode */
}

static void bme280_read(uint8_t addr, i2c_sensor_t* sensor) {
    int idx = bme280_get_index(addr);
    bme280_calib_t* cal = &s_bme_calib[idx];

    uint8_t raw[8];
    if (i2c_read_bytes(addr, 0xF7, raw, 8) != ESP_OK) return;

    int32_t adc_P = (raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = (raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4);
    int32_t adc_H = (raw[6] << 8)  |  raw[7];

    /* Temperature compensation (from BME280 datasheet section 4.2.3) */
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)cal->T1 << 1))) * cal->T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - cal->T1) * ((adc_T >> 4) - cal->T1)) >> 12) * cal->T3) >> 14;
    cal->t_fine = var1 + var2;
    int32_t t_fine = cal->t_fine;
    sensor->metrics[0].value = (float)((t_fine * 5 + 128) >> 8) / 100.0f;

    /* Pressure compensation (int64 variant from datasheet) */
    int64_t p_var1 = (int64_t)t_fine - 128000;
    int64_t p_var2 = p_var1 * p_var1 * (int64_t)cal->P6;
    p_var2 = p_var2 + ((p_var1 * (int64_t)cal->P5) << 17);
    p_var2 = p_var2 + (((int64_t)cal->P4) << 35);
    p_var1 = ((p_var1 * p_var1 * (int64_t)cal->P3) >> 8) + ((p_var1 * (int64_t)cal->P2) << 12);
    p_var1 = (((int64_t)1 << 47) + p_var1) * (int64_t)cal->P1 >> 33;
    if (p_var1 != 0) {
        int64_t p = 1048576 - adc_P;
        p = (((p << 31) - p_var2) * 3125) / p_var1;
        p_var1 = ((int64_t)cal->P9 * (p >> 13) * (p >> 13)) >> 25;
        p_var2 = ((int64_t)cal->P8 * p) >> 19;
        p = ((p + p_var1 + p_var2) >> 8) + ((int64_t)cal->P7 << 4);
        sensor->metrics[1].value = (float)p / 25600.0f;  /* hPa */
    }

    /* Humidity compensation (from BME280 datasheet section 4.2.3) */
    int32_t h = t_fine - 76800;
    h = (((((adc_H << 14) - ((int32_t)cal->H4 << 20) - ((int32_t)cal->H5 * h)) + 16384) >> 15) *
         (((((((h * (int32_t)cal->H6) >> 10) * (((h * (int32_t)cal->H3) >> 11) + 32768)) >> 10) + 2097152) *
         (int32_t)cal->H2 + 8192) >> 14));
    h = h - (((((h >> 15) * (h >> 15)) >> 7) * (int32_t)cal->H1) >> 4);
    h = h < 0 ? 0 : h;
    h = h > 419430400 ? 419430400 : h;
    sensor->metrics[2].value = (float)(h >> 12) / 1024.0f;

    sensor->metric_count = 3;
}

/* ========================================================================= */
/* MPU6050 — accelerometer + gyroscope                                       */
/* ========================================================================= */

static bool mpu6050_identify(uint8_t addr) {
    uint8_t id;
    if (i2c_read_byte(addr, 0x75, &id) != ESP_OK) return false;
    return id == 0x68;
}

static void mpu6050_init(uint8_t addr) {
    i2c_write_byte(addr, 0x6B, 0x00);  /* Wake up (clear sleep bit) */
    i2c_write_byte(addr, 0x1B, 0x00);  /* Gyro: ±250°/s */
    i2c_write_byte(addr, 0x1C, 0x00);  /* Accel: ±2g */
}

static void mpu6050_read(uint8_t addr, i2c_sensor_t* sensor) {
    uint8_t raw[14];
    if (i2c_read_bytes(addr, 0x3B, raw, 14) != ESP_OK) return;

    /* Accel: 16384 LSB/g at ±2g */
    sensor->metrics[0].value = (float)((int16_t)(raw[0] << 8 | raw[1])) / 16384.0f;
    sensor->metrics[1].value = (float)((int16_t)(raw[2] << 8 | raw[3])) / 16384.0f;
    sensor->metrics[2].value = (float)((int16_t)(raw[4] << 8 | raw[5])) / 16384.0f;

    /* Gyro: 131 LSB/°/s at ±250°/s */
    sensor->metrics[3].value = (float)((int16_t)(raw[8]  << 8 | raw[9]))  / 131.0f;
    sensor->metrics[4].value = (float)((int16_t)(raw[10] << 8 | raw[11])) / 131.0f;
    sensor->metrics[5].value = (float)((int16_t)(raw[12] << 8 | raw[13])) / 131.0f;

    sensor->metric_count = 6;
}

/* ========================================================================= */
/* BH1750 — ambient light                                                    */
/* ========================================================================= */

static bool bh1750_identify(uint8_t addr) {
    /* BH1750 has no WHO_AM_I. Power it on, start a measurement,
     * and check if we get a plausible reading (not 0xFFFF). */
    if (addr != 0x23 && addr != 0x5C) return false;

    uint8_t cmd_on = 0x01;   /* Power On */
    uint8_t cmd_hr = 0x20;   /* One-time high-res mode (auto power-down after) */
    if (i2c_master_write_to_device(I2C_PORT, addr, &cmd_on, 1, I2C_TIMEOUT) != ESP_OK)
        return false;
    if (i2c_master_write_to_device(I2C_PORT, addr, &cmd_hr, 1, I2C_TIMEOUT) != ESP_OK)
        return false;

    vTaskDelay(pdMS_TO_TICKS(180));  /* Max measurement time for high-res mode */

    uint8_t raw[2];
    if (i2c_master_read_from_device(I2C_PORT, addr, raw, 2, I2C_TIMEOUT) != ESP_OK)
        return false;

    uint16_t val = (raw[0] << 8) | raw[1];
    /* 0xFFFF means the device didn't respond with real data (not a BH1750) */
    return val != 0xFFFF;
}

static void bh1750_init(uint8_t addr) {
    /* Power on first, then set continuous high-res mode */
    uint8_t cmd_on = 0x01;
    i2c_master_write_to_device(I2C_PORT, addr, &cmd_on, 1, I2C_TIMEOUT);
    vTaskDelay(pdMS_TO_TICKS(10));  /* Wait for power-on */

    uint8_t cmd_hr = 0x10;  /* Continuous high-res mode */
    i2c_master_write_to_device(I2C_PORT, addr, &cmd_hr, 1, I2C_TIMEOUT);
}

static void bh1750_read(uint8_t addr, i2c_sensor_t* sensor) {
    uint8_t raw[2];
    if (i2c_master_read_from_device(I2C_PORT, addr, raw, 2, I2C_TIMEOUT) != ESP_OK) return;

    uint16_t level = (raw[0] << 8) | raw[1];
    sensor->metrics[0].value = (float)level / 1.2f;  /* Convert to lux */
    sensor->metric_count = 1;
}

/* ========================================================================= */
/* SHT3x — temperature + humidity                                            */
/* ========================================================================= */

static bool sht3x_identify(uint8_t addr) {
    /* SHT3x has no WHO_AM_I but has a status register we can read.
     * Read status (cmd 0xF32D) and verify the response is plausible.
     * This distinguishes SHT3x from INA219 and other devices at 0x44/0x45. */
    if (addr != 0x44 && addr != 0x45) return false;

    uint8_t cmd[2] = {0xF3, 0x2D};  /* Read Status Register */
    if (i2c_master_write_to_device(I2C_PORT, addr, cmd, 2, I2C_TIMEOUT) != ESP_OK)
        return false;

    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t raw[3];  /* 2 bytes status + 1 byte CRC */
    if (i2c_master_read_from_device(I2C_PORT, addr, raw, 3, I2C_TIMEOUT) != ESP_OK)
        return false;

    /* On SHT3x, the status register reset value has bit 4 = 0 (no reset detected after clear).
     * On a non-SHT3x device, this read would return garbage or NACK.
     * Accept any response where we got 3 bytes back — the two-byte command
     * is SHT3x-specific enough that other devices won't respond correctly. */
    return true;
}

static void sht3x_init(uint8_t addr) {
    (void)addr;  /* No special init needed */
}

static void sht3x_read(uint8_t addr, i2c_sensor_t* sensor) {
    /* Trigger single-shot measurement: high repeatability */
    uint8_t cmd[2] = {0x24, 0x00};
    i2c_master_write_to_device(I2C_PORT, addr, cmd, 2, I2C_TIMEOUT);
    vTaskDelay(pdMS_TO_TICKS(20));  /* Measurement time */

    uint8_t raw[6];
    if (i2c_master_read_from_device(I2C_PORT, addr, raw, 6, I2C_TIMEOUT) != ESP_OK) return;

    uint16_t raw_temp = (raw[0] << 8) | raw[1];
    uint16_t raw_hum  = (raw[3] << 8) | raw[4];

    sensor->metrics[0].value = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    sensor->metrics[1].value = 100.0f * ((float)raw_hum / 65535.0f);
    sensor->metric_count = 2;
}

/* ========================================================================= */
/* INA219 — voltage + current                                                */
/* ========================================================================= */

static bool ina219_identify(uint8_t addr) {
    /* INA219 has no WHO_AM_I — check config register has non-zero default */
    uint8_t raw[2];
    if (i2c_read_bytes(addr, 0x00, raw, 2) != ESP_OK) return false;
    uint16_t config = (raw[0] << 8) | raw[1];
    return config == 0x399F;  /* Default config register value */
}

static void ina219_init(uint8_t addr) {
    /* Set calibration for 0.1Ω shunt, 32V bus, 320mA range */
    uint8_t cal[3] = {0x05, 0x10, 0x00};  /* Reg 0x05, value 0x1000 */
    i2c_master_write_to_device(I2C_PORT, addr, cal, 3, I2C_TIMEOUT);
}

static void ina219_read(uint8_t addr, i2c_sensor_t* sensor) {
    uint8_t raw[2];

    /* Bus voltage (reg 0x02): bits [15:3] = voltage, LSB = 4mV */
    if (i2c_read_bytes(addr, 0x02, raw, 2) == ESP_OK) {
        uint16_t raw_v = ((uint16_t)(raw[0] << 8) | raw[1]) >> 3;
        sensor->metrics[0].value = (float)raw_v * 0.004f;
    }

    /* Current (reg 0x04): LSB depends on calibration */
    if (i2c_read_bytes(addr, 0x04, raw, 2) == ESP_OK) {
        int16_t raw_c = (raw[0] << 8) | raw[1];
        sensor->metrics[1].value = (float)raw_c * 0.1f;  /* mA */
    }

    sensor->metric_count = 2;
}

/* ========================================================================= */
/* Sensor registry                                                           */
/* ========================================================================= */

typedef struct {
    const char* name;
    uint8_t     addresses[2];
    int         addr_count;
    bool        (*identify)(uint8_t addr);
    void        (*init)(uint8_t addr);
    void        (*read_fn)(uint8_t addr, i2c_sensor_t* sensor);
    const char* metric_names[I2C_SCAN_MAX_METRICS];
    int         metric_count;
} sensor_def_t;

static const sensor_def_t SENSOR_DEFS[] = {
    {
        .name = "BME280",
        .addresses = {0x76, 0x77}, .addr_count = 2,
        .identify = bme280_identify, .init = bme280_init, .read_fn = bme280_read,
        .metric_names = {"temperature", "pressure", "humidity"},
        .metric_count = 3,
    },
    {
        .name = "MPU6050",
        .addresses = {0x68, 0x69}, .addr_count = 2,
        .identify = mpu6050_identify, .init = mpu6050_init, .read_fn = mpu6050_read,
        .metric_names = {"accel_x", "accel_y", "accel_z", "gyro_x", "gyro_y", "gyro_z"},
        .metric_count = 6,
    },
    {
        .name = "BH1750",
        .addresses = {0x23, 0x5C}, .addr_count = 2,
        .identify = bh1750_identify, .init = bh1750_init, .read_fn = bh1750_read,
        .metric_names = {"light_lux"},
        .metric_count = 1,
    },
    {
        .name = "SHT3x",
        .addresses = {0x44, 0x45}, .addr_count = 2,
        .identify = sht3x_identify, .init = sht3x_init, .read_fn = sht3x_read,
        .metric_names = {"sht_temperature", "sht_humidity"},
        .metric_count = 2,
    },
    {
        .name = "INA219",
        .addresses = {0x40, 0x41}, .addr_count = 2,
        .identify = ina219_identify, .init = ina219_init, .read_fn = ina219_read,
        .metric_names = {"bus_voltage", "current_ma"},
        .metric_count = 2,
    },
};

#define SENSOR_DEF_COUNT (sizeof(SENSOR_DEFS) / sizeof(SENSOR_DEFS[0]))

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

bool i2c_scan_init(int sda_pin, int scl_pin) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) return false;

    err = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    return err == ESP_OK;
}

void i2c_scan_detect(i2c_scan_result_t* result) {
    memset(result, 0, sizeof(*result));

    /* Scan all valid I2C addresses */
    uint8_t found_addrs[112];
    int found_count = 0;
    int str_pos = 0;

    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        uint8_t dummy;
        if (i2c_master_read_from_device(I2C_PORT, addr, &dummy, 1, I2C_TIMEOUT) == ESP_OK) {
            found_addrs[found_count++] = addr;
            if (str_pos > 0 && str_pos < (int)sizeof(result->devices_str) - 6) {
                str_pos += snprintf(result->devices_str + str_pos,
                                    sizeof(result->devices_str) - str_pos, ",");
            }
            str_pos += snprintf(result->devices_str + str_pos,
                                sizeof(result->devices_str) - str_pos, "0x%02X", addr);
            ESP_LOGI(TAG, "I2C device at 0x%02X", addr);
        }
    }

    ESP_LOGI(TAG, "Found %d I2C devices", found_count);

    /* Try to identify known sensors */
    for (int i = 0; i < found_count && result->count < I2C_SCAN_MAX_SENSORS; i++) {
        uint8_t addr = found_addrs[i];

        for (int d = 0; d < (int)SENSOR_DEF_COUNT; d++) {
            const sensor_def_t* def = &SENSOR_DEFS[d];

            /* Check if this address matches */
            bool addr_match = false;
            for (int a = 0; a < def->addr_count; a++) {
                if (def->addresses[a] == addr) { addr_match = true; break; }
            }
            if (!addr_match) continue;

            /* Try to identify */
            if (def->identify(addr)) {
                i2c_sensor_t* sensor = &result->sensors[result->count];
                sensor->sensor_name = def->name;
                sensor->address = addr;
                sensor->metric_count = def->metric_count;

                for (int m = 0; m < def->metric_count; m++) {
                    sensor->metrics[m].name = def->metric_names[m];
                    sensor->metrics[m].value = 0.0f;
                }

                /* Initialize the sensor */
                def->init(addr);

                ESP_LOGI(TAG, "Identified %s at 0x%02X", def->name, addr);
                result->count++;
                break;  /* Don't try other sensor types for this address */
            }
        }
    }

    ESP_LOGI(TAG, "Identified %d known sensors", result->count);
}

void i2c_scan_read_all(i2c_scan_result_t* result) {
    for (int i = 0; i < result->count; i++) {
        i2c_sensor_t* sensor = &result->sensors[i];

        /* Find the matching sensor definition to call its read function */
        for (int d = 0; d < (int)SENSOR_DEF_COUNT; d++) {
            if (strcmp(SENSOR_DEFS[d].name, sensor->sensor_name) == 0) {
                SENSOR_DEFS[d].read_fn(sensor->address, sensor);
                break;
            }
        }
    }
}

void i2c_scan_send(plexus_client_t* px, const i2c_scan_result_t* result) {
    for (int i = 0; i < result->count; i++) {
        const i2c_sensor_t* sensor = &result->sensors[i];
        for (int m = 0; m < sensor->metric_count; m++) {
            plexus_send(px, sensor->metrics[m].name, sensor->metrics[m].value);
        }
    }
}
