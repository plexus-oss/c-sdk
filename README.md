# Plexus C SDK

Minimal footprint C library for sending telemetry from embedded devices (ESP32, STM32, Arduino) to the Plexus ingest API.

**Target: ~2KB RAM footprint**

## Features

- Zero external dependencies (custom JSON serializer)
- Supports ESP32, STM32, and Arduino platforms
- Configurable buffer sizes for memory-constrained devices
- Automatic batching with configurable flush thresholds
- Retry logic with exponential backoff
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
    https://github.com/plexus/plexus-oss.git#main
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
    git: https://github.com/plexus/plexus-oss.git
    path: c-sdk
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

// Free client resources
void plexus_free(plexus_client_t* client);

// Set custom endpoint URL
plexus_err_t plexus_set_endpoint(plexus_client_t* client, const char* endpoint);
```

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

Typical RAM usage with default configuration:

| Component | Bytes |
|-----------|-------|
| Client struct | ~380 |
| Metric buffer (32 metrics) | ~1600 |
| JSON buffer | 2048 |
| **Total** | **~4KB** |

With minimal configuration (~1KB):

| Component | Bytes |
|-----------|-------|
| Client struct | ~380 |
| Metric buffer (8 metrics) | ~400 |
| JSON buffer | 512 |
| **Total** | **~1.3KB** |

## License

MIT License - see LICENSE file for details.

## Support

- Issues: https://github.com/plexus/plexus-oss/issues
- Discord: https://discord.gg/plexus
