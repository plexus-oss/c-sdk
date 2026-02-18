# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.1] - 2026-02-18

### Added

- Static allocation: `plexus_init_static()` for no-malloc environments (MISRA C compliant)
- `plexus_client_size()` and `PLEXUS_CLIENT_STATIC_SIZE` for compile-time and runtime buffer sizing
- `PLEXUS_ERR_INVALID_ARG` error code for invalid arguments (e.g., bad source_id)
- Source ID validation at init — rejects characters outside `[a-zA-Z0-9._-]`
- User-Agent header (`plexus-c-sdk/0.1.1`) sent on all HTTP requests
- SDK version field (`"sdk":"c/0.1.1"`) included in JSON payload
- Exponential backoff with ±25% jitter on flush retries (`PLEXUS_RETRY_BASE_MS`, `PLEXUS_RETRY_MAX_MS`)
- Automatic rate-limit cooldown: 429 responses suppress flushes for `PLEXUS_RATE_LIMIT_COOLDOWN_MS` (default 30s)
- `__attribute__((format))` on `plexus_hal_log()` for compile-time format string checking (GCC/Clang)
- ESP32 HAL: TCP keep-alive for connection reuse
- STM32 example: demonstrates `plexus_init_static()` with zero heap usage

### Fixed

- `plexus_value_t` union now conditionally compiles string/bool members — saves ~128 bytes per metric when disabled
- `plexus_tick()` returns `PLEXUS_OK` when idle instead of `PLEXUS_ERR_NO_DATA`
- Command handler stored with correct function pointer type — eliminates undefined behavior from incompatible cast
- JSON number formatting uses `%.10g` instead of `%.6f` — fixes precision loss for very small values (e.g., 1e-7 no longer rounds to 0)
- JSON control characters escaped as `\u00XX` instead of replaced with space
- `json_extract_int()` overflow protection for values exceeding INT32_MAX
- ESP32 NVS `plexus_hal_storage_read()` returns `PLEXUS_OK` (not error) when key not found
- HAL template storage contract updated to match implementation

### Changed

- HAL interface: `plexus_hal_http_post()` and `plexus_hal_http_get()` now take a `user_agent` parameter (**breaking for custom HAL implementations**)
- Retry config: replaced `PLEXUS_RETRY_DELAY_MS` with `PLEXUS_RETRY_BASE_MS` (500ms) and `PLEXUS_RETRY_MAX_MS` (8000ms)
- Time-based auto-flush removed from `plexus_send_*()` — only count-based auto-flush remains in the send path; time-based flush happens in `plexus_tick()`
- Test suite expanded from 39 to 54 tests (35 core + 19 JSON)

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

[0.1.1]: https://github.com/plexus-oss/c-sdk/releases/tag/v0.1.1
[0.1.0]: https://github.com/plexus-oss/c-sdk/releases/tag/v0.1.0
