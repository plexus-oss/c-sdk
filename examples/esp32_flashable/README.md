# Plexus Flashable Firmware

Zero-code ESP32 firmware that auto-detects I2C sensors, reads ADC pins, and streams everything to Plexus. Flash once, configure via serial, start streaming.

## How It Works

1. **Flash** the firmware to your ESP32
2. **Configure** WiFi and API key via serial terminal (or browser flash with pre-baked config)
3. **Sensors detected automatically** — BME280, MPU6050, BH1750, SHT3x, INA219
4. **ADC pins read automatically** — GPIO 32-37 (channels above noise floor)
5. **System metrics streamed** — free heap, uptime, WiFi RSSI

No code to write. No SDK to learn. Just hardware and a Plexus account.

## Serial Configuration

When no config is found, the firmware enters serial config mode at **115200 baud** on UART0 (the USB serial port).

### Protocol

```
Device sends:  PLEXUS:READY
You send:      PLEXUS:api_key=plx_your_key_here
Device sends:  PLEXUS:OK
You send:      PLEXUS:source_id=my-esp32
Device sends:  PLEXUS:OK
You send:      PLEXUS:wifi_ssid=MyNetwork
Device sends:  PLEXUS:OK
You send:      PLEXUS:wifi_pass=MyPassword
Device sends:  PLEXUS:OK
You send:      PLEXUS:COMMIT
Device sends:  PLEXUS:SAVED
(device reboots and connects)
```

### Required Keys

| Key | Description |
|-----|-------------|
| `api_key` | Your Plexus API key (starts with `plx_`) |
| `source_id` | Device identifier in Plexus (e.g., `esp32-lab-01`) |
| `wifi_ssid` | WiFi network name |
| `wifi_pass` | WiFi password |

### Optional Keys

| Key | Description | Default |
|-----|-------------|---------|
| `endpoint` | Custom Plexus API endpoint | `https://app.plexus.company/api/ingest` |

## LED Status

| Pattern | Meaning |
|---------|---------|
| Fast blink (100ms) | Waiting for serial config |
| Slow blink (500ms) | Connecting to WiFi |
| Solid on | Streaming telemetry |

## Auto-Detected Sensors

The firmware scans the I2C bus at boot and automatically reads any recognized sensors:

| Sensor | Address(es) | Metrics |
|--------|-------------|---------|
| BME280 | 0x76, 0x77 | `temperature`, `pressure`, `humidity` |
| MPU6050 | 0x68, 0x69 | `accel_x/y/z`, `gyro_x/y/z` |
| BH1750 | 0x23, 0x5C | `light_lux` |
| SHT3x | 0x44, 0x45 | `sht_temperature`, `sht_humidity` |
| INA219 | 0x40, 0x41 | `bus_voltage`, `current_ma` |

Unrecognized I2C devices are reported as an `i2c_devices` string metric (comma-separated hex addresses) so you can see what's connected even without a built-in driver.

## ADC Channels

6 ADC channels on GPIO 32-37 are read each second. Only channels with voltage above 50mV are sent (to avoid spamming zeros for unconnected pins).

| Metric | GPIO |
|--------|------|
| `adc_ch0` | 32 |
| `adc_ch1` | 33 |
| `adc_ch2` | 34 |
| `adc_ch3` | 35 |
| `adc_ch4` | 36 |
| `adc_ch5` | 37 |

Values are in millivolts (calibrated if the ESP32 has eFuse calibration data).

## Pin Assignments

| Function | GPIO | Notes |
|----------|------|-------|
| I2C SDA | 21 | Default ESP32 I2C |
| I2C SCL | 22 | Default ESP32 I2C |
| ADC CH0-5 | 32-37 | ADC1 input-only pins |
| Status LED | 2 | Built-in LED on most dev boards |
| UART TX | 1 | USB serial (config + logs) |
| UART RX | 3 | USB serial (config input) |

## System Metrics

Always streamed regardless of connected sensors:

| Metric | Description |
|--------|-------------|
| `free_heap` | Free RAM in bytes |
| `uptime_s` | Seconds since boot |
| `wifi_rssi` | WiFi signal strength (dBm) |

## Building from Source

Requires [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/).

```bash
cd examples/esp32_flashable
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Alternative: Browser Flash

Instead of serial config, you can pre-bake WiFi and API key into the firmware image. The firmware checks for a `plexus_cfg` flash partition containing plain-text `key=value` pairs. If found, it imports them to NVS and reboots.

This is used by the Plexus web flasher — users enter their config in a browser, and the flasher generates a complete image with the config partition pre-populated.
