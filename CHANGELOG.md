# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.4.0] - 2026-02-19

### Fixed

- **Zero-size array compile error** — `s_builtin_sensors[]` in `plexus_sensors.c` failed to compile when no `PLEXUS_SENSOR_*` flags were enabled; added NULL sentinel so the array is never empty
- **STM32 HAL missing LwIP headers** — `plexus_hal_stm32.c` now uses `__has_include` to detect LwIP availability and provides stub implementations when absent, fixing PlatformIO CI builds on boards without LwIP middleware
- **`warn_unused_result` warnings in STM32 example** — added `(void)` casts for `plexus_set_endpoint`, `plexus_set_flush_interval`, `plexus_send`, and `plexus_send_bool` calls
- **test_sensors build flags** — added missing `-DPLEXUS_SENSOR_BME280=1 -DPLEXUS_SENSOR_MPU6050=1` to CMake test target so sensor detection tests actually run

### Removed

- `publish.yml` workflow — SDK is distributed via the frontend dashboard, not package registries
- `idf_component.yml` — ESP-IDF Component Registry manifest (no longer publishing there)

## [0.3.0] - 2026-02-19

### Breaking

- Per-sensor compile flags now default to OFF. Enable specific sensors with
  `-DPLEXUS_SENSOR_BME280=1` etc. The dashboard wizard generates these automatically.
- Removed `@plexus/configure` CLI tool. Use the dashboard wizard instead.

### Added

- Per-sensor compile guards (`PLEXUS_SENSOR_BME280`, `PLEXUS_SENSOR_MPU6050`, etc.)
  strip unused drivers at compile time, saving flash.

## [0.2.1] - 2026-02-18

### Fixed

- **Security: command ID URL injection** — command IDs from server responses are now validated with `plexus_internal_is_url_safe()` before embedding in result URLs, preventing path traversal attacks (e.g., `../../admin`)
- **STM32 HTTP header buffer overflow** — increased header buffer from 512 to 768 bytes (`PLEXUS_STM32_HEADER_BUF_SIZE`) to fit worst-case config values (256-byte path + 128-byte host + 128-byte API key)
- **Command JSON parser unescaping** — `json_extract_string()` now unescapes `\"`, `\\`, `\n`, `\r`, `\t`, `\/` sequences instead of copying raw escaped content to command handlers

### Added

- Compile-time configuration validation via `_Static_assert` (C11) with C99/GCC fallback — catches invalid `PLEXUS_MAX_METRICS`, `PLEXUS_JSON_BUFFER_SIZE`, `PLEXUS_MAX_RETRIES`, and other misconfigured values at build time
- `plexus_internal_is_url_safe()` shared validator in `plexus_internal.h` — used by both source ID and command ID validation
- `PlexusClient` C++ class moved to `plexus.h` — now visible to Arduino sketches and any C++ project that includes the header (was previously hidden inside `plexus_hal_arduino.cpp`)
- Documentation: persistent buffer single-batch limitation noted in README

### Changed

- `is_valid_source_id()` renamed to `plexus_internal_is_url_safe()` and made non-static for cross-module reuse
- STM32 example: compiles with or without FreeRTOS (uses `__has_include` detection, falls back to bare-metal `HAL_Delay`)
- STM32 CI: verifies example + SDK core compile against STM32 headers (HAL excluded — requires LwIP which PlatformIO doesn't ship)
- Test suite: 62 tests (43 core + 19 JSON)

## [0.2.0] - 2026-02-18

### Added

- `plexus_send()` convenience macro — alias for `plexus_send_number()`, matches Python SDK's `px.send()` pattern
- `PLEXUS_SDK_VERSION` constant in public header — single source of truth for version string
- `PLEXUS_CLIENT_STATIC_BUF(name)` macro — declares a static client variable with correct type
- `_heap_allocated` flag on client struct — enables safe `plexus_free()` on both heap and static clients
- Arduino `PlexusClient::send()` method — shorter alias for `sendNumber()`
- Arduino `PlexusClient::sendNumberTs()` method
- ASan + UBSan enabled in test builds (disable with `-DPLEXUS_NO_SANITIZERS=ON`)
- CTest integration — `ctest --test-dir build-test` runs all tests
- `PLEXUS_MINIMAL_CONFIG` CMake option — builds with all optional features disabled
- CI job for minimal-config build validation
- `plexus_init_static()` runtime alignment check — rejects misaligned buffers (prevents Cortex-M0 hard faults)
- Tick wraparound regression tests for auto-flush and rate limit cooldown
- Auto-flush count trigger test
- Explicit timestamp (`plexus_send_number_ts`) round-trip test
- Misaligned buffer rejection test

### Fixed

- **PLEXUS_CLIENT_STATIC_SIZE now compiles for end users** — struct definition moved to public header (FreeRTOS `StaticTask_t` pattern) so `sizeof(plexus_client_t)` resolves at compile time
- **plexus_free() on static clients no longer causes undefined behavior** — checks `_heap_allocated` before calling `free()`
- **Infinity detection** — uses `isinf()` / `isnan()` from `<math.h>` instead of fragile `> 1e308` comparison
- **Tick wraparound logic** — replaced unreliable `* 2` heuristic with signed comparison pattern `(int32_t)(now - deadline) >= 0` for correct uint32_t wrap handling
- **json_extract_int INT_MIN overflow** — separate checks for positive (`> 2147483647`) and negative (`> 2147483648`) ranges, plus `digits == 0` guard
- **Duplicate version strings eliminated** — `PLEXUS_SDK_VERSION` in `plexus.h` used everywhere (removed separate `PLEXUS_VERSION` in plexus.c and `PLEXUS_VERSION_STR` in plexus_internal.h)
- **Duplicate PLEXUS_USER_AGENT** — single definition in `plexus_internal.h`, removed from plexus_commands.c
- **STM32 HAL hardcoded peripherals** — `huart2` and `hrtc` now configurable via `PLEXUS_STM32_DEBUG_UART` and `PLEXUS_STM32_RTC` defines
- **STM32 HTTP GET 2KB stack allocation** — reads directly into caller's buffer with `memmove()` for body extraction
- **STM32 `HAL_Delay` blocks FreeRTOS tasks** — now uses `osDelay()` when FreeRTOS is detected, falling back to `HAL_Delay()` on bare-metal
- **STM32 `parse_url` port overflow** — validates port range 1–65535 instead of blind `atoi` cast to `uint16_t`
- **STM32 `read_http_status` performance** — reads first chunk instead of byte-by-byte, reduced drain timeout from 100ms to 10ms
- **CI paths filter** — fixed for standalone repo (was referencing monorepo paths)
- **`plexus_send_number_tagged` duplicated queuing logic** — refactored to share validation and auto-flush via `maybe_auto_flush()`

### Changed

- Client struct fields exposed in public header (members prefixed with `_` — not part of public API)
- Arduino `PlexusClient` is now non-copyable (deleted copy constructor and assignment operator)
- Test suite expanded from 54 to 61 tests (42 core + 19 JSON)
- Examples updated to use `plexus_send()` macro and `PLEXUS_CLIENT_STATIC_BUF()`
- README rewritten for "3 clicks" developer experience matching Python SDK flow

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

[0.4.0]: https://github.com/plexus-oss/c-sdk/releases/tag/v0.4.0
[0.3.0]: https://github.com/plexus-oss/c-sdk/releases/tag/v0.3.0
[0.2.1]: https://github.com/plexus-oss/c-sdk/releases/tag/v0.2.1
[0.2.0]: https://github.com/plexus-oss/c-sdk/releases/tag/v0.2.0
[0.1.1]: https://github.com/plexus-oss/c-sdk/releases/tag/v0.1.1
[0.1.0]: https://github.com/plexus-oss/c-sdk/releases/tag/v0.1.0
