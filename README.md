# Plexus C SDK

Minimal footprint C library for sending telemetry from embedded devices (ESP32, STM32, Arduino) to the Plexus ingest API.

**Configurable RAM footprint — ~17KB default, ~2KB minimal (numbers-only, 8 metrics)**

## Features

- Zero external dependencies (custom JSON serializer)
- Opaque client handle — internals are not exposed in the public API
- Supports ESP32, STM32, and Arduino platforms
- Configurable buffer sizes for memory-constrained devices
- Automatic batching with configurable flush thresholds
- Retry logic with configurable delay between attempts
- Support for numeric, string, and boolean values
- Optional metric tags

## Quick Start

### ESP32 with ESP-IDF

```c
#include "plexus.h"

void app_main(void) {
    // Initialize (after WiFi connection)
    plexus_client_t* client = plexus_init("plx_your_api_key", "esp32-001");

    // Send metrics
    plexus_send_number(client, "temperature", 72.5);
    plexus_send_number(client, "humidity", 45.0);

    // Flush to API
    plexus_err_t err = plexus_flush(client);
    if (err != PLEXUS_OK) {
        printf("Error: %s\n", plexus_strerror(err));
    }

    // Cleanup
    plexus_free(client);
}
```

### Arduino (ESP32/ESP8266)

```cpp
#include <WiFi.h>
#include "plexus.h"

PlexusClient plexus("plx_your_api_key", "arduino-001");

void setup() {
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);
}

void loop() {
    plexus.sendNumber("temperature", analogRead(A0) * 0.1);
    plexus.flush();
    delay(5000);
}
```

## Installation

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
    https://github.com/plexus-oss/c-sdk.git#main
```

Or install from registry:

```bash
pio lib install "plexus-sdk"
```

### ESP-IDF Component

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  plexus-sdk:
    git: https://github.com/plexus-oss/c-sdk.git
```

Or copy the `c-sdk` directory to your project's `components/` folder.

### Manual Installation

1. Copy `include/` and `src/` to your project
2. Copy the appropriate HAL from `hal/` (esp32, stm32, or arduino)
3. Add source files to your build system

## API Reference

### Initialization

```c
// Create client with API key and source ID
plexus_client_t* plexus_init(const char* api_key, const char* source_id);

// Free client resources (call plexus_flush() first to send pending data)
void plexus_free(plexus_client_t* client);

// Set custom endpoint URL
plexus_err_t plexus_set_endpoint(plexus_client_t* client, const char* endpoint);

// Override flush interval at runtime (0 = use compile-time default)
plexus_err_t plexus_set_flush_interval(plexus_client_t* client, uint32_t interval_ms);

// Override auto-flush count at runtime (0 = use compile-time default)
plexus_err_t plexus_set_flush_count(plexus_client_t* client, uint16_t count);
```

These runtime setters allow changing flush behavior without reflashing, useful for OTA configuration updates.

### Sending Metrics

```c
// Send numeric value
plexus_err_t plexus_send_number(plexus_client_t* client, const char* metric, double value);

// Send with explicit timestamp (milliseconds since epoch)
plexus_err_t plexus_send_number_ts(plexus_client_t* client, const char* metric,
                                    double value, uint64_t timestamp_ms);

// Send string value (if PLEXUS_ENABLE_STRING_VALUES=1)
plexus_err_t plexus_send_string(plexus_client_t* client, const char* metric, const char* value);

// Send boolean value (if PLEXUS_ENABLE_BOOL_VALUES=1)
plexus_err_t plexus_send_bool(plexus_client_t* client, const char* metric, bool value);

// Send with tags (if PLEXUS_ENABLE_TAGS=1)
plexus_err_t plexus_send_number_tagged(plexus_client_t* client, const char* metric,
                                        double value, const char** tag_keys,
                                        const char** tag_values, uint8_t tag_count);
```

### Flushing

```c
// Send all queued metrics to API
plexus_err_t plexus_flush(plexus_client_t* client);

// Get number of queued metrics
uint16_t plexus_pending_count(const plexus_client_t* client);

// Clear queue without sending
void plexus_clear(plexus_client_t* client);
```

### Error Handling

```c
// Get error message string
const char* plexus_strerror(plexus_err_t err);

// Error codes
typedef enum {
    PLEXUS_OK = 0,
    PLEXUS_ERR_NULL_PTR,
    PLEXUS_ERR_BUFFER_FULL,
    PLEXUS_ERR_STRING_TOO_LONG,
    PLEXUS_ERR_NO_DATA,
    PLEXUS_ERR_NETWORK,
    PLEXUS_ERR_AUTH,
    PLEXUS_ERR_RATE_LIMIT,
    PLEXUS_ERR_SERVER,
    PLEXUS_ERR_JSON,
    PLEXUS_ERR_NOT_INITIALIZED,
    PLEXUS_ERR_HAL,
} plexus_err_t;
```

## Configuration

Override defaults in `plexus_config.h` or via compiler flags:

| Option | Default | Description |
|--------|---------|-------------|
| `PLEXUS_MAX_METRICS` | 32 | Max metrics per flush |
| `PLEXUS_MAX_METRIC_NAME_LEN` | 64 | Max metric name length |
| `PLEXUS_MAX_STRING_VALUE_LEN` | 128 | Max string value length |
| `PLEXUS_JSON_BUFFER_SIZE` | 2048 | JSON serialization buffer |
| `PLEXUS_HTTP_TIMEOUT_MS` | 10000 | HTTP request timeout |
| `PLEXUS_MAX_RETRIES` | 3 | Retry count on failure |
| `PLEXUS_AUTO_FLUSH_COUNT` | 16 | Auto-flush after N metrics |
| `PLEXUS_ENABLE_TAGS` | 1 | Enable metric tags |
| `PLEXUS_ENABLE_STRING_VALUES` | 1 | Enable string values |
| `PLEXUS_ENABLE_BOOL_VALUES` | 1 | Enable boolean values |
| `PLEXUS_DEBUG` | 0 | Enable debug logging |

### Minimal Memory Configuration

For very constrained devices (~1KB RAM):

```c
#define PLEXUS_MAX_METRICS 8
#define PLEXUS_JSON_BUFFER_SIZE 512
#define PLEXUS_ENABLE_TAGS 0
#define PLEXUS_ENABLE_STRING_VALUES 0
#define PLEXUS_ENABLE_BOOL_VALUES 0
```

## Platform Support

### ESP32 (ESP-IDF)

Full support with:
- HTTPClient over WiFi
- NTP time synchronization
- FreeRTOS integration

### ESP8266

Supported via Arduino framework with WiFiClientSecure.

### STM32

Template HAL provided. Requires implementing HTTP transport based on your network stack (LwIP, AT commands, etc.).

### Arduino

Supports ESP32 and ESP8266 boards with built-in WiFi. For other boards, implement the HAL functions or use an Ethernet shield.

## Memory Usage

RAM usage depends on your configuration. The JSON buffer and metric array are embedded in the client struct (no global state), so each client is fully self-contained.

Default configuration (tags + strings enabled, 32 metrics):

| Component | Bytes |
|-----------|-------|
| Client struct + metric buffer | ~15KB |
| JSON buffer (in struct) | 2048 |
| **Total per client** | **~17KB** |

Minimal configuration (numbers only, 8 metrics):

```c
#define PLEXUS_MAX_METRICS 8
#define PLEXUS_JSON_BUFFER_SIZE 512
#define PLEXUS_ENABLE_TAGS 0
#define PLEXUS_ENABLE_STRING_VALUES 0
#define PLEXUS_ENABLE_BOOL_VALUES 0
```

| Component | Bytes |
|-----------|-------|
| Client struct + metric buffer | ~1.5KB |
| JSON buffer (in struct) | 512 |
| **Total per client** | **~2KB** |

Use `sizeof(plexus_client_t)` with your config flags to get the exact size for your build.

## Thread Safety

This SDK is **not thread-safe**. All calls to a given `plexus_client_t*` must be made from the same thread or FreeRTOS task. Do not call `plexus_send_*()` from one task while calling `plexus_flush()` from another on the same client.

Multiple clients in separate tasks is safe — each client has its own buffer and JSON serializer with no shared global state.

## Failure Modes & Recovery

| Scenario | Behavior | Error Code |
|----------|----------|------------|
| **WiFi drops** | Metrics stay in RAM buffer. Flush retries up to `PLEXUS_MAX_RETRIES` times. If persistent buffer is enabled, data is written to flash on final retry failure. | `PLEXUS_ERR_NETWORK` |
| **Buffer full** | `plexus_send_*` returns immediately. Caller should flush or drop metrics. Auto-flush helps prevent this. | `PLEXUS_ERR_BUFFER_FULL` |
| **API down / server errors** | Retries up to `PLEXUS_MAX_RETRIES` times. Data stays in buffer on failure. With persistent buffer, survives reboot. | `PLEXUS_ERR_SERVER` |
| **Auth token expired** | Returns immediately (no retry). Caller must update API key and retry. | `PLEXUS_ERR_AUTH` |
| **Rate limited** | Returns immediately (no retry). Caller should back off before retrying. | `PLEXUS_ERR_RATE_LIMIT` |
| **Reboot during send** | Without persistent buffer, unsent data is lost. With `PLEXUS_ENABLE_PERSISTENT_BUFFER=1`, the last failed batch is written to flash and retried on the next `plexus_flush()` call. | — |

## TLS/HTTPS Support

| Platform | TLS Status | Notes |
|----------|-----------|-------|
| **ESP32 (ESP-IDF)** | Full HTTPS | Uses `esp_http_client` with the default CA bundle. Works out of the box. |
| **ESP32/ESP8266 (Arduino)** | HTTPS (no verification) | Uses `WiFiClientSecure` with `setInsecure()`. For production, add certificate pinning by calling `setCACert()` with the server's root CA PEM. |
| **STM32** | HTTP only | HTTPS requires mbedTLS integration with LwIP `altcp_tls`. The provided STM32 HAL does not include TLS — you must integrate it with your board's network stack. |

## Persistent Buffering

Enable flash-backed storage so unsent telemetry survives reboots:

```c
#define PLEXUS_ENABLE_PERSISTENT_BUFFER 1
```

**How it works:**
- On flush failure after all retries, the serialized JSON batch is written to flash under the key `"plexus_buf"`.
- On the next `plexus_flush()` call, persisted data is sent first before any new metrics.
- On successful send, the persisted data is cleared from flash.

**Platform support:**

| Platform | Storage Backend | Status |
|----------|----------------|--------|
| ESP32 (ESP-IDF) | NVS (Non-Volatile Storage) | Works today |
| STM32 | User-provided | You must implement `plexus_hal_storage_write()`, `plexus_hal_storage_read()`, and `plexus_hal_storage_clear()` |
| Arduino | User-provided | Same as STM32 — implement the three storage HAL functions |

## Why Plexus?

If you're evaluating options for getting telemetry off an embedded device, here's how Plexus compares:

**vs. Raw MQTT** — MQTT gives you a transport pipe, not an observability stack. You still need a broker, a subscriber service, a time-series database, and a visualization layer. Plexus is one HTTP call from your device to a managed dashboard.

**vs. Custom HTTP** — Rolling your own POST works until you need retry logic, exponential backoff, buffer management, JSON serialization, and persistent storage across reboots. Plexus handles all of that in ~2KB of RAM.

**vs. ThingsBoard SDK** — ThingsBoard's C SDK pulls in MQTT client libraries (~50KB+ RAM), requires a persistent broker connection, and assumes you're running their full platform. Plexus is stateless HTTP — no broker, no long-lived connections.

**vs. AWS IoT SDK** — AWS IoT Device SDK requires mTLS certificates, per-device provisioning through IoT Core, and a complex auth chain. Plexus uses a single API key — flash it once, send telemetry immediately.

| | Plexus C SDK | Raw MQTT | ThingsBoard SDK | AWS IoT SDK |
|---|---|---|---|---|
| **RAM footprint** | ~2KB (min ~1.3KB) | ~10KB+ | ~50KB+ | ~30KB+ |
| **Dependencies** | None | MQTT client lib | MQTT + cJSON | mbedTLS + MQTT |
| **Auth model** | API key header | Broker credentials | Device token | mTLS certificates |
| **Protocol** | HTTP(S) | MQTT (persistent) | MQTT (persistent) | MQTT + mTLS |
| **Persistent buffer** | Built-in (opt-in) | Manual | Manual | Manual |
| **Managed backend** | Yes | No (bring your own) | Self-host or cloud | AWS only |
| **Platforms** | ESP32, STM32, Arduino | Varies | ESP32, Linux | ESP32, Linux |

## License

Apache License 2.0 - see [LICENSE](LICENSE) file for details.

## Support

- Issues: https://github.com/plexus-oss/c-sdk/issues
- Discord: https://discord.gg/plexus
