/**
 * @file plexus_hal_i2c_esp32.c
 * @brief ESP-IDF I2C HAL for Plexus sensor discovery
 *
 * Uses ESP-IDF i2c driver API (v5.0+).
 * Default pins: SDA=GPIO21, SCL=GPIO22 (standard ESP32 DevKit).
 * Override with PLEXUS_I2C_SDA_PIN / PLEXUS_I2C_SCL_PIN defines.
 */

#include "plexus.h"

#if defined(ESP_PLATFORM) && PLEXUS_ENABLE_SENSOR_DISCOVERY

#include "driver/i2c.h"
#include "esp_log.h"

#ifndef PLEXUS_I2C_SDA_PIN
#define PLEXUS_I2C_SDA_PIN 21
#endif

#ifndef PLEXUS_I2C_SCL_PIN
#define PLEXUS_I2C_SCL_PIN 22
#endif

#ifndef PLEXUS_I2C_FREQ_HZ
#define PLEXUS_I2C_FREQ_HZ 100000
#endif

#define I2C_TIMEOUT_MS 100

static const char* TAG = "plexus_i2c";

plexus_err_t plexus_hal_i2c_init(uint8_t bus_num) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PLEXUS_I2C_SDA_PIN,
        .scl_io_num = PLEXUS_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = PLEXUS_I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config((i2c_port_t)bus_num, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed: %s", esp_err_to_name(err));
        return PLEXUS_ERR_I2C;
    }

    err = i2c_driver_install((i2c_port_t)bus_num, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return PLEXUS_ERR_I2C;
    }

    return PLEXUS_OK;
}

bool plexus_hal_i2c_probe(uint8_t addr) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    return (err == ESP_OK);
}

plexus_err_t plexus_hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t* out) {
    if (!out) return PLEXUS_ERR_NULL_PTR;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, out, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    return (err == ESP_OK) ? PLEXUS_OK : PLEXUS_ERR_I2C;
}

plexus_err_t plexus_hal_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    return (err == ESP_OK) ? PLEXUS_OK : PLEXUS_ERR_I2C;
}

#endif /* ESP_PLATFORM && PLEXUS_ENABLE_SENSOR_DISCOVERY */
