# Plexus C SDK

Send telemetry from ESP32, STM32, and Arduino to [Plexus](https://plexus.company) in 3 lines of code.

```c
plexus_client_t* px = plexus_init("plx_your_api_key", "esp32-001");
plexus_send(px, "temperature", 72.5);
plexus_flush(px);
```

**~2KB RAM minimal, ~17KB default** | Zero dependencies | Retry + backoff + rate-limit built in

## Quick Start

### 1. Get your API key

Go to [app.plexus.company](https://app.plexus.company) → **Add Device** → copy the API key.

### 2. Send telemetry

#### ESP32 / ESP-IDF

```c
#include "plexus.h"

void app_main(void) {
    // After WiFi is connected:
    plexus_client_t* px = plexus_init("plx_your_api_key", "esp32-001");

    plexus_send(px, "temperature", 72.5);
    plexus_send(px, "humidity", 45.0);
    plexus_flush(px);

    plexus_free(px);
}
```

#### Arduino (ESP32 / ESP8266)

```cpp
#include <WiFi.h>
#include "plexus.h"

PlexusClient px("plx_your_api_key", "arduino-001");

void setup() {
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);
}

void loop() {
    px.send("temperature", analogRead(A0) * 0.1);
    px.send("humidity", analogRead(A1) * 0.2);
    px.tick();    // auto-flushes every 5 seconds
    delay(1000);
}
```

#### STM32 (FreeRTOS + LwIP)

```c
#include "plexus.h"

PLEXUS_CLIENT_STATIC_BUF(px_buf);  // no malloc

void telemetry_task(void const* arg) {
    plexus_client_t* px = plexus_init_static(
        &px_buf, sizeof(px_buf), "plx_your_api_key", "stm32-001");
    plexus_set_endpoint(px, "http://app.plexus.company/api/ingest");

    for (;;) {
        plexus_send(px, "temperature", read_temp());
        plexus_tick(px);
        osDelay(1000);
    }
}
```

### 3. See it in your dashboard

Data appears in real time at [app.plexus.company](https://app.plexus.company).

## Setup

Go to [app.plexus.company](https://app.plexus.company) → **Add Device** → **Embedded**.
The wizard generates a `plexus_config.h` and starter `main.c` tuned for your
platform, memory budget, and selected sensors.

## Install

### PlatformIO

```ini
lib_deps =
    https://github.com/plexus-oss/c-sdk.git#main
```

### ESP-IDF Component

```yaml
# idf_component.yml
dependencies:
  plexus-sdk:
    git: https://github.com/plexus-oss/c-sdk.git
```

Or copy to your project's `components/` folder.

### Manual

Copy `include/`, `src/`, and the appropriate `hal/` directory to your project.

## API

### Init & Free

```c
// Heap-allocated (uses malloc)
plexus_client_t* px = plexus_init("plx_key", "source-id");
plexus_free(px);

// Static allocation (no malloc — MISRA C compliant)
PLEXUS_CLIENT_STATIC_BUF(buf);
plexus_client_t* px = plexus_init_static(&buf, sizeof(buf), "plx_key", "source-id");
// No free needed — just stop using it

// plexus_free() is safe on both: it only calls free() on heap-allocated clients.
```

### Send

```c
plexus_send(px, "temperature", 72.5);           // numeric (most common)
plexus_send_number_ts(px, "temp", 72.5, ts_ms); // with explicit timestamp
plexus_send_string(px, "status", "running");     // string (opt-in)
plexus_send_bool(px, "armed", true);             // boolean (opt-in)

// With tags
const char* keys[] = {"location"};
const char* vals[] = {"room-1"};
plexus_send_number_tagged(px, "temp", 72.5, keys, vals, 1);
```

### Flush

```c
plexus_flush(px);              // send now (blocks during retries, max ~14s)
plexus_tick(px);               // call from loop — auto-flushes on interval
plexus_pending_count(px);      // queued metrics
plexus_clear(px);              // discard queue
```

### Config

```c
plexus_set_endpoint(px, "https://custom.domain/api/ingest");
plexus_set_flush_interval(px, 10000);  // auto-flush every 10s
plexus_set_flush_count(px, 8);         // auto-flush after 8 metrics
```

### Errors

```c
plexus_err_t err = plexus_flush(px);
if (err != PLEXUS_OK) {
    printf("Error: %s\n", plexus_strerror(err));
}
```

| Code                     | Meaning                                                     |
| ------------------------ | ----------------------------------------------------------- |
| `PLEXUS_OK`              | Success                                                     |
| `PLEXUS_ERR_BUFFER_FULL` | Metric buffer full — flush or increase `PLEXUS_MAX_METRICS` |
| `PLEXUS_ERR_NETWORK`     | Connection failed — metrics stay in buffer for retry        |
| `PLEXUS_ERR_AUTH`        | Bad API key (401) — no retry                                |
| `PLEXUS_ERR_RATE_LIMIT`  | Throttled (429) — auto-cooldown, retries after 30s          |
| `PLEXUS_ERR_SERVER`      | Server error (5xx) — retried with backoff                   |

## Configuration

Override via compiler flags (`-DPLEXUS_MAX_METRICS=8`) or before including `plexus.h`:

| Option                            | Default | Description                           |
| --------------------------------- | ------- | ------------------------------------- |
| `PLEXUS_MAX_METRICS`              | 32      | Max metrics per flush                 |
| `PLEXUS_JSON_BUFFER_SIZE`         | 2048    | JSON serialization buffer             |
| `PLEXUS_MAX_RETRIES`              | 3       | Retry count on failure                |
| `PLEXUS_AUTO_FLUSH_COUNT`         | 16      | Auto-flush after N metrics            |
| `PLEXUS_AUTO_FLUSH_INTERVAL_MS`   | 5000    | Auto-flush interval (0=disabled)      |
| `PLEXUS_ENABLE_TAGS`              | 1       | Metric tags support                   |
| `PLEXUS_ENABLE_STRING_VALUES`     | 1       | String value support                  |
| `PLEXUS_ENABLE_BOOL_VALUES`       | 1       | Boolean value support                 |
| `PLEXUS_ENABLE_PERSISTENT_BUFFER` | 0       | Flash-backed buffer for unsent data   |
| `PLEXUS_ENABLE_COMMANDS`          | 0       | Remote shell command execution        |
| `PLEXUS_ENABLE_TYPED_COMMANDS`    | 0       | Typed commands with parameter schemas |
| `PLEXUS_ENABLE_HEARTBEAT`         | 0       | Device heartbeat with metric registry |
| `PLEXUS_ENABLE_AUTO_REGISTER`     | 0       | Auto-register device on first connect |
| `PLEXUS_ENABLE_SENSOR_DISCOVERY`  | 0       | I2C sensor auto-detection             |
| `PLEXUS_SENSOR_BME280`            | 0       | Compile BME280 driver                 |
| `PLEXUS_SENSOR_MPU6050`           | 0       | Compile MPU6050 driver                |
| `PLEXUS_SENSOR_INA219`            | 0       | Compile INA219 driver                 |
| `PLEXUS_SENSOR_ADS1115`           | 0       | Compile ADS1115 driver                |
| `PLEXUS_SENSOR_SHT3X`             | 0       | Compile SHT3x driver                  |
| `PLEXUS_SENSOR_BH1750`            | 0       | Compile BH1750 driver                 |
| `PLEXUS_SENSOR_VL53L0X`           | 0       | Compile VL53L0X driver                |
| `PLEXUS_SENSOR_QMC5883L`          | 0       | Compile QMC5883L driver               |
| `PLEXUS_SENSOR_HMC5883L`          | 0       | Compile HMC5883L driver               |
| `PLEXUS_DEBUG`                    | 0       | Debug logging                         |

### Minimal config (~1.5KB RAM)

```c
-DPLEXUS_MAX_METRICS=8
-DPLEXUS_JSON_BUFFER_SIZE=512
-DPLEXUS_ENABLE_TAGS=0
-DPLEXUS_ENABLE_STRING_VALUES=0
-DPLEXUS_ENABLE_BOOL_VALUES=0
```

## Typed Commands

Declare structured commands with typed parameters. The dashboard auto-generates UI controls — sliders, dropdowns, toggles — from the schema. Enable with `-DPLEXUS_ENABLE_TYPED_COMMANDS=1`.

```c
#include "plexus.h"

// Command handler
plexus_err_t set_speed_handler(
    plexus_client_t* client,
    const plexus_param_value_t* params,
    int param_count,
    void* user_data)
{
    float rpm = params[0].f;
    float ramp = params[1].f;
    motor_set_speed(rpm, ramp);
    return PLEXUS_OK;
}

void app_main(void) {
    plexus_client_t* px = plexus_init("plx_key", "motor-001");

    // Define parameters
    plexus_param_desc_t params[2] = {
        { .name = "rpm",       .type = PLEXUS_PARAM_FLOAT,
          .min = 0, .max = 10000, .has_min = true, .has_max = true },
        { .name = "ramp_time", .type = PLEXUS_PARAM_FLOAT,
          .min = 0.1, .max = 10.0, .has_min = true, .has_max = true,
          .has_default = true, .default_value = { .f = 1.0 } },
    };

    // Register the command
    plexus_register_typed_command(px,
        "set_speed", "Set motor speed",
        params, 2,
        set_speed_handler, NULL);

    for (;;) {
        plexus_tick(px);  // polls for commands + auto-flushes
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

This works the same way in the Python agent — see the [Python agent README](../agent/README.md#commands--remote-control) for the equivalent `@px.command` decorator API.

See `examples/typed_commands/` for a complete example.

## Sensor Discovery

Auto-detect I2C sensors at runtime. Enable with `-DPLEXUS_ENABLE_SENSOR_DISCOVERY=1` and the specific sensors you need:

```
-DPLEXUS_ENABLE_SENSOR_DISCOVERY=1
-DPLEXUS_SENSOR_BME280=1
-DPLEXUS_SENSOR_MPU6050=1
```

Only enabled sensor drivers are compiled into the binary, saving flash. The dashboard wizard generates these flags automatically based on your sensor selection.

```c
plexus_scan_sensors(px);
printf("Found %d sensors\n", plexus_detected_sensor_count(px));

// In your main loop:
plexus_sensor_read_all(px);   // reads all detected sensors and queues telemetry
plexus_tick(px);               // flushes to Plexus
```

See `examples/esp32_autodiscovery/` for a complete example with auto-registration and heartbeat.

## Memory

| Config                             | RAM per client |
| ---------------------------------- | -------------- |
| Default (all features, 32 metrics) | ~17KB          |
| Minimal (numbers only, 8 metrics)  | ~1.5KB         |

Use `plexus_client_size()` or `sizeof(plexus_client_t)` to get the exact size for your build.

## Platform Support

| Platform                    | TLS                    | Timestamps     | Notes                                               |
| --------------------------- | ---------------------- | -------------- | --------------------------------------------------- |
| **ESP32 (ESP-IDF)**         | HTTPS                  | NTP via SNTP   | Full support, production-ready                      |
| **ESP32/ESP8266 (Arduino)** | HTTPS (no cert verify) | `gettimeofday` | Add `setCACert()` for production                    |
| **STM32 (LwIP)**            | HTTP only              | RTC            | Requires mbedTLS for HTTPS — see `hal/stm32` header |

### STM32 peripheral config

The STM32 HAL defaults to `huart2` (debug UART) and `hrtc` (RTC). Override for your board:

```
-DPLEXUS_STM32_DEBUG_UART=huart3
-DPLEXUS_STM32_RTC=hrtc1
```

## Persistent Buffering

Unsent telemetry survives reboots when enabled:

```c
-DPLEXUS_ENABLE_PERSISTENT_BUFFER=1
```

On flush failure, the serialized batch is written to flash with CRC32 integrity check. On next `plexus_flush()`, persisted data is sent first. ESP32 uses NVS; STM32/Arduino require implementing 3 HAL storage functions.

**Note:** Only the most recent failed batch is persisted. If multiple flushes fail before recovery, earlier batches are overwritten. For extended offline buffering, increase `PLEXUS_MAX_METRICS` to batch more data per flush.

## Thread Safety

**Not thread-safe.** All calls to a given client must come from the same thread/task. Multiple clients in separate tasks is safe — each client has its own buffer with no global state.

## Blocking Behavior

`plexus_flush()` retries with exponential backoff on failure. Worst case: ~14 seconds blocking (3 retries, 8-second max backoff). On FreeRTOS, the calling task yields during delays. On bare-metal Arduino, `delay()` blocks everything. Use `plexus_tick()` from your main loop for non-blocking auto-flush.

## Porting to New Platforms

Copy `hal/template/plexus_hal_template.c` and implement the HAL functions. See the verification checklist at the bottom of the template.

## Why Plexus?

|                       | Plexus C SDK    | Raw MQTT                      | ThingsBoard SDK    | AWS IoT SDK             |
| --------------------- | --------------- | ----------------------------- | ------------------ | ----------------------- |
| **RAM**               | ~1.5KB min      | ~10KB+                        | ~50KB+             | ~30KB+                  |
| **Dependencies**      | None            | MQTT lib                      | MQTT + cJSON       | mbedTLS + MQTT          |
| **Auth**              | API key header  | Broker creds                  | Device token       | mTLS certs              |
| **Protocol**          | HTTP(S)         | MQTT persistent               | MQTT persistent    | MQTT + mTLS             |
| **Persistent buffer** | Built-in        | Manual                        | Manual             | Manual                  |
| **Setup**             | 3 lines of code | Broker + subscriber + DB + UI | Self-host or cloud | IoT Core + provisioning |

## License

Apache License 2.0 — see [LICENSE](LICENSE).

## Support

- Issues: https://github.com/plexus-oss/c-sdk/issues
- Discord: https://discord.gg/plexus
