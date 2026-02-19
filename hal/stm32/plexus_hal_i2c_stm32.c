/**
 * @file plexus_hal_i2c_stm32.c
 * @brief STM32 I2C HAL for Plexus sensor discovery
 *
 * Uses STM32 HAL I2C driver (HAL_I2C_*).
 * Configure I2C peripheral handle with PLEXUS_STM32_I2C define.
 */

#include "plexus.h"

#if (defined(STM32F4) || defined(STM32F7) || defined(STM32H7) || defined(STM32_HAL)) && PLEXUS_ENABLE_SENSOR_DISCOVERY

#if defined(STM32F4)
    #include "stm32f4xx_hal.h"
#elif defined(STM32F7)
    #include "stm32f7xx_hal.h"
#elif defined(STM32H7)
    #include "stm32h7xx_hal.h"
#endif

#ifndef PLEXUS_STM32_I2C
#define PLEXUS_STM32_I2C hi2c1
#endif

extern I2C_HandleTypeDef PLEXUS_STM32_I2C;

#define I2C_TIMEOUT_MS 100

plexus_err_t plexus_hal_i2c_init(uint8_t bus_num) {
    /* STM32 I2C is typically initialized via CubeMX-generated MX_I2C1_Init().
     * This function is a no-op — the user must call MX_I2Cx_Init() before
     * plexus_scan_sensors(). */
    (void)bus_num;
    return PLEXUS_OK;
}

bool plexus_hal_i2c_probe(uint8_t addr) {
    HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(
        &PLEXUS_STM32_I2C, (uint16_t)(addr << 1), 1, I2C_TIMEOUT_MS);
    return (status == HAL_OK);
}

plexus_err_t plexus_hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t* out) {
    if (!out) return PLEXUS_ERR_NULL_PTR;

    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
        &PLEXUS_STM32_I2C, (uint16_t)(addr << 1), reg,
        I2C_MEMADD_SIZE_8BIT, out, 1, I2C_TIMEOUT_MS);

    return (status == HAL_OK) ? PLEXUS_OK : PLEXUS_ERR_I2C;
}

plexus_err_t plexus_hal_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
        &PLEXUS_STM32_I2C, (uint16_t)(addr << 1), reg,
        I2C_MEMADD_SIZE_8BIT, &val, 1, I2C_TIMEOUT_MS);

    return (status == HAL_OK) ? PLEXUS_OK : PLEXUS_ERR_I2C;
}

#endif /* STM32 && PLEXUS_ENABLE_SENSOR_DISCOVERY */
