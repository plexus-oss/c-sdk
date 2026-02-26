# Plexus C SDK

Send telemetry from ESP32, STM32, and Arduino to [Plexus](https://plexus.company) in 3 lines of code.

```c
plexus_client_t* px = plexus_init("plx_your_api_key", "esp32-001");
plexus_send(px, "temperature", 72.5);
plexus_flush(px);
```

**~1.5KB RAM minimal, ~5KB default** | Zero dependencies | Retry + backoff + rate-limit built in

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
#include "plexus.hpp"

PlexusClient px("plx_your_api_key", "arduino-001");

void setup() {
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);
}

void loop() {
    px.send("temperature", analogRead(34) * 0.1);
    px.send("humidity", analogRead(35) * 0.2);
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

**`source_id` must be URL-safe:** only `a-z A-Z 0-9 . _ -` are allowed (no spaces). Use the slug shown in the Plexus dashboard, not the display name. `plexus_init` returns `NULL` if the source ID contains invalid characters.

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
| `PLEXUS_ERR_FORBIDDEN`   | Missing write scope (403) — no retry                        |
| `PLEXUS_ERR_BILLING`     | Billing limit exceeded (402) — no retry                     |
| `PLEXUS_ERR_RATE_LIMIT`  | Throttled (429) — auto-cooldown, retries after 30s          |
| `PLEXUS_ERR_SERVER`      | Server error (5xx) — retried with backoff                   |

## Configuration

Override via compiler flags (`-DPLEXUS_MAX_METRICS=8`) or before including `plexus.h`:

| Option                            | Default | Description                         |
| --------------------------------- | ------- | ----------------------------------- |
| `PLEXUS_MAX_METRICS`              | 32      | Max metrics per flush               |
| `PLEXUS_JSON_BUFFER_SIZE`         | 2048    | JSON serialization buffer           |
| `PLEXUS_MAX_RETRIES`              | 3       | Retry count on failure              |
| `PLEXUS_AUTO_FLUSH_COUNT`         | 16      | Auto-flush after N metrics          |
| `PLEXUS_AUTO_FLUSH_INTERVAL_MS`   | 5000    | Auto-flush interval (0=disabled)    |
| `PLEXUS_ENABLE_TAGS`              | 1       | Metric tags support                 |
| `PLEXUS_ENABLE_STRING_VALUES`     | 1       | String value support                |
| `PLEXUS_ENABLE_BOOL_VALUES`       | 1       | Boolean value support               |
| `PLEXUS_ENABLE_PERSISTENT_BUFFER` | 0       | Flash-backed buffer for unsent data |
| `PLEXUS_ENABLE_STATUS_CALLBACK`   | 0       | Connection status notifications     |
| `PLEXUS_ENABLE_THREAD_SAFE`       | 0       | Mutex-protected client access       |
| `PLEXUS_DEBUG`                    | 0       | Debug logging                       |

### Minimal config (~1.5KB RAM)

```c
-DPLEXUS_MAX_METRICS=8
-DPLEXUS_JSON_BUFFER_SIZE=512
-DPLEXUS_ENABLE_TAGS=0
-DPLEXUS_ENABLE_STRING_VALUES=0
-DPLEXUS_ENABLE_BOOL_VALUES=0
```

## Memory

RAM usage is dominated by three components:

| Component | Default | Minimal | Scales with |
|-----------|---------|---------|-------------|
| Metrics buffer | largest | smallest | `PLEXUS_MAX_METRICS` × (name + value + timestamp + tags) |
| JSON buffer | 2048 B | 512 B | `PLEXUS_JSON_BUFFER_SIZE` |
| Fixed fields | ~512 B | ~320 B | API key, source ID, endpoint, session, state |

| Config | Total RAM per client |
|--------|----------------------|
| Default (all features, 32 metrics) | ~5 KB |
| Minimal (numbers only, 8 metrics) | ~1.5 KB |

Disabling tags (`PLEXUS_ENABLE_TAGS=0`) and string values (`PLEXUS_ENABLE_STRING_VALUES=0`) shrinks each metric slot significantly — the value union drops from 128 bytes to 8 bytes.

Get the exact size for your build:

    printf("Client size: %zu bytes\n", sizeof(plexus_client_t));

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

### STM32 HTTPS setup

The STM32 HAL ships with plain HTTP. For production, add HTTPS via mbedTLS + LwIP's altcp_tls layer:

**1. Enable mbedTLS** in STM32CubeMX → Middleware → mbedTLS. Enable at minimum: `MBEDTLS_SSL_CLI_C`, `MBEDTLS_SSL_TLS_C`, `MBEDTLS_NET_C`.

**2. Add to `lwipopts.h`:**

    #define LWIP_ALTCP              1
    #define LWIP_ALTCP_TLS          1
    #define LWIP_ALTCP_TLS_MBEDTLS  1

**3. Replace socket calls** in `plexus_hal_stm32.c` with `altcp` equivalents — `altcp_new`, `altcp_connect`, `altcp_write`, `altcp_output`. See the `altcp_tls` section in the [LwIP docs](https://www.nongnu.org/lwip/2_1_x/group__altcp__tls.html).

**4. Load root CA certificate** for `app.plexus.company` to verify the server.

Alternatively, use a TLS-terminating reverse proxy (e.g., nginx) on your network edge and keep the HAL as-is with HTTP.

## Persistent Buffering

Unsent telemetry survives reboots when enabled:

```c
-DPLEXUS_ENABLE_PERSISTENT_BUFFER=1
```

On flush failure, the serialized batch is written to flash with CRC32 integrity check. On next `plexus_flush()`, persisted data is sent first. ESP32 uses NVS; STM32/Arduino require implementing 3 HAL storage functions.

Uses a ring buffer with `PLEXUS_PERSIST_MAX_BATCHES` slots (default 8). Oldest batches are overwritten when the ring buffer is full.

## Thread Safety

**Not thread-safe by default.** Confine all calls to a given client to a single thread/task.

- **One client per task** (default): No flag needed. Each client has its own buffers and no global state, so separate clients in separate tasks are always safe.
- **Shared client across tasks**: Enable `-DPLEXUS_ENABLE_THREAD_SAFE=1`. This wraps all API calls in a platform mutex (FreeRTOS `osMutex` on STM32/ESP32, `xSemaphoreCreateRecursiveMutex` on ESP-IDF). The calling task blocks if another task holds the lock.
- **`plexus_init_static()` with a shared buffer**: The buffer must not be accessed by multiple tasks during `plexus_init_static()`. After initialization, access is governed by the thread-safe flag above.

## Blocking Behavior

`plexus_flush()` blocks while sending. Timing depends on the outcome:

| Scenario | Duration |
|----------|----------|
| Success (first attempt) | < 1 second |
| Transient failure + retry | 1–5 seconds |
| All retries fail (worst case) | ~14 seconds |

The worst case is 3 retries with exponential backoff (500ms → 1s → 2s → ... up to 8s max, plus ±25% jitter). On FreeRTOS, the calling task yields during delays. On bare-metal Arduino, `delay()` blocks everything.

For non-blocking operation, use `plexus_tick()` from your main loop — it only flushes when the auto-flush interval or count threshold is reached, and returns immediately otherwise.

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
