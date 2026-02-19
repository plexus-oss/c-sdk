/**
 * @file main.c
 * @brief Plexus C SDK — Typed commands example for ESP32
 *
 * Declares structured commands with typed parameters. The Plexus
 * dashboard auto-generates UI controls (sliders, dropdowns, toggles)
 * from the command schema — no frontend code needed.
 *
 * This example registers two commands:
 *   - set_speed: Set motor RPM with a ramp time (float params)
 *   - set_mode:  Switch operating mode (enum param)
 *
 * The equivalent Python agent code uses @px.command / @param decorators.
 * See the Python agent README for that version.
 *
 * Build requirements:
 *   -DPLEXUS_ENABLE_TYPED_COMMANDS=1
 *   -DPLEXUS_ENABLE_HEARTBEAT=1
 */

#include "plexus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "plexus_cmds";

#ifndef CONFIG_PLEXUS_API_KEY
#define CONFIG_PLEXUS_API_KEY "plx_your_api_key_here"
#endif

/* ================================================================= */
/* Command handlers                                                    */
/* ================================================================= */

/**
 * Handle "set_speed" command.
 * params[0] = rpm (float, 0-10000)
 * params[1] = ramp_time (float, 0.1-10.0, default 1.0)
 */
static plexus_err_t cmd_set_speed(
    plexus_client_t* client,
    const plexus_param_value_t* params,
    int param_count,
    void* user_data)
{
    float rpm = params[0].f;
    float ramp = (param_count > 1) ? params[1].f : 1.0f;

    ESP_LOGI(TAG, "set_speed: rpm=%.0f ramp=%.1fs", rpm, ramp);

    /* TODO: Replace with your motor control logic */
    /* motor_set_speed(rpm, ramp); */

    /* Report the actual RPM back as telemetry */
    plexus_send(client, "motor.rpm", rpm);

    return PLEXUS_OK;
}

/**
 * Handle "set_mode" command.
 * params[0] = mode (enum: "idle", "run", "calibrate")
 */
static plexus_err_t cmd_set_mode(
    plexus_client_t* client,
    const plexus_param_value_t* params,
    int param_count,
    void* user_data)
{
    const char* mode = params[0].s;

    ESP_LOGI(TAG, "set_mode: %s", mode);

    /* TODO: Replace with your mode-switching logic */
    /* controller_set_mode(mode); */

    plexus_send_string(client, "device.mode", mode);

    return PLEXUS_OK;
}

/* ================================================================= */
/* Command registration                                                */
/* ================================================================= */

static void register_commands(plexus_client_t* px) {
    /* --- set_speed command --- */
    plexus_param_desc_t speed_params[2] = {
        {
            .name = "rpm",
            .type = PLEXUS_PARAM_FLOAT,
            .min = 0, .max = 10000,
            .has_min = true, .has_max = true,
            .required = true,
        },
        {
            .name = "ramp_time",
            .type = PLEXUS_PARAM_FLOAT,
            .min = 0.1f, .max = 10.0f,
            .has_min = true, .has_max = true,
            .has_default = true,
            .default_value = { .f = 1.0f },
            .required = false,
        },
    };

    plexus_register_typed_command(px,
        "set_speed", "Set motor speed",
        speed_params, 2,
        cmd_set_speed, NULL);

    /* --- set_mode command --- */
    plexus_param_desc_t mode_params[1] = {
        {
            .name = "mode",
            .type = PLEXUS_PARAM_ENUM,
            .required = true,
            .num_choices = 3,
            .choices = { "idle", "run", "calibrate" },
        },
    };

    plexus_register_typed_command(px,
        "set_mode", "Switch operating mode",
        mode_params, 1,
        cmd_set_mode, NULL);

    ESP_LOGI(TAG, "Registered %d typed commands",
             (int)px->typed_command_count);
}

/* ================================================================= */
/* Main                                                                */
/* ================================================================= */

/* Forward declaration — implement WiFi init for your project */
extern void wifi_init_sta(void);

void app_main(void) {
    wifi_init_sta();

    plexus_client_t* px = plexus_init(CONFIG_PLEXUS_API_KEY, "motor-001");
    if (!px) {
        ESP_LOGE(TAG, "Failed to init Plexus");
        return;
    }

    plexus_set_device_info(px, "ESP32", "1.0.0");

    /* Register typed commands — schemas are sent with the heartbeat */
    register_commands(px);

    /* Send initial heartbeat (includes command schemas) */
    plexus_heartbeat(px);

    ESP_LOGI(TAG, "Entering main loop — commands are handled by tick()");

    while (1) {
        /* Read sensors (replace with your telemetry) */
        plexus_send(px, "temperature", 25.0 + (esp_random() % 100) / 10.0);

        /* tick() auto-flushes telemetry AND polls for incoming commands */
        plexus_tick(px);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
