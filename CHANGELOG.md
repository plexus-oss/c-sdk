# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-02-17

### Added

- Core SDK: `plexus_init()`, `plexus_free()`, `plexus_send_number()`, `plexus_send_number_ts()`, `plexus_flush()`, `plexus_tick()`, `plexus_pending_count()`, `plexus_clear()`
- Runtime configuration: `plexus_set_endpoint()`, `plexus_set_flush_interval()`, `plexus_set_flush_count()`
- Diagnostic accessors: `plexus_total_sent()`, `plexus_total_errors()`
- String value support via `plexus_send_string()` (opt-in with `PLEXUS_ENABLE_STRING_VALUES=1`)
- Boolean value support via `plexus_send_bool()` (opt-in with `PLEXUS_ENABLE_BOOL_VALUES=1`)
- Metric tags via `plexus_send_number_tagged()` (opt-in with `PLEXUS_ENABLE_TAGS=1`)
- Command polling support (opt-in with `PLEXUS_ENABLE_COMMANDS=1`)
- Persistent buffer: flash-backed storage for unsent telemetry (opt-in with `PLEXUS_ENABLE_PERSISTENT_BUFFER=1`)
- Zero-dependency JSON serializer with NaN-to-null conversion and trailing zero stripping
- Automatic batching with configurable flush count and time interval
- Retry logic with configurable retries and HAL-backed delay between attempts (`PLEXUS_MAX_RETRIES`, `PLEXUS_RETRY_DELAY_MS`)
- Opaque client handle — struct internals hidden in `src/plexus_internal.h`, public API exposes only `plexus_client_t*`
- Per-client JSON buffer — no shared global state, safe to use multiple clients in separate threads/tasks
- ESP32 HAL: ESP-IDF `esp_http_client`, SNTP time sync, NVS persistent storage
- Arduino HAL: ESP32/ESP8266 via `WiFiClientSecure` + `HTTPClient`, C++ wrapper class
- STM32 HAL: LwIP raw sockets, RTC timestamps, UART debug logging (HTTP only), conditional includes for F4/F7/H7
- HAL interface includes `plexus_hal_delay_ms()` for platform-native delay (FreeRTOS vTaskDelay, HAL_Delay, etc.)
- HAL porting template with annotated stubs and verification checklist
- Dual-mode CMake: ESP-IDF component registration or standalone static library
- PlatformIO manifest (`library.json`) with Arduino HAL source filter
- ESP-IDF Component Registry manifest (`idf_component.yml`)
- Host test suite: `test_core` (24 tests) and `test_json` (15 tests) with mock HAL — tests use only public API
- CI workflow: host tests + PlatformIO cross-compilation for ESP32, ESP8266, STM32
- Examples: ESP32 ESP-IDF, Arduino basic, STM32 FreeRTOS

[0.1.0]: https://github.com/plexus-oss/c-sdk/releases/tag/v0.1.0
