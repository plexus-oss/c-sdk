# Contributing to Plexus C SDK

## Prerequisites

- **CMake** 3.10+ and **GCC** or **Clang** (for host tests)
- **Python 3.8+** with PlatformIO (`pip install platformio`) — optional, for cross-compilation
- **ESP-IDF v5.0+** — optional, for ESP32 native builds
- **Doxygen** — optional, for generating API docs

## Building & Testing

### Host tests (Linux/macOS)

```bash
cmake -B build-test tests/
cmake --build build-test/
./build-test/test_core
./build-test/test_json
```

### ESP32 via ESP-IDF

```bash
cd examples/esp32_basic
idf.py build
idf.py flash monitor
```

### Arduino via PlatformIO (ESP32)

```bash
pio ci examples/arduino_basic/plexus_example.ino \
  --lib=. \
  --board=esp32dev \
  --project-option="framework=arduino"
```

### Arduino via PlatformIO (ESP8266)

```bash
pio ci examples/arduino_basic/plexus_example.ino \
  --lib=. \
  --board=nodemcuv2 \
  --project-option="framework=arduino"
```

### STM32 via PlatformIO

```bash
pio ci \
  examples/stm32_freertos/main.c \
  src/plexus.c src/plexus_json.c \
  hal/stm32/plexus_hal_stm32.c \
  --board=nucleo_f446re \
  --project-option="framework=stm32cube" \
  --project-option="build_flags=-DSTM32F4 -DSTM32_HAL -Iinclude -Isrc"
```

## Code Style

- **C99** standard — no VLAs, no C11+ features
- Use `#if PLEXUS_FEATURE` (not `#ifdef`) for feature flags so explicit `=0` disables them
- K&R brace style: opening brace on same line
- `static` for all file-local helpers
- No dynamic allocation in HAL functions (the core `plexus_init` uses `malloc` for the client struct only)
- Keep lines under 100 characters where practical

## PR Process

1. Fork the repository and create a feature branch
2. Make your changes
3. Ensure all tests pass (`./build-test/test_core && ./build-test/test_json`)
4. If you add new HAL functions, stub them in `tests/mock_hal.c`
5. Add Doxygen comments to any new public API functions
6. Add an entry to `CHANGELOG.md` under `[Unreleased]`
7. Open a PR against `main`

## Project Structure

```
include/plexus.h          — Public C API (opaque client handle)
include/plexus.hpp        — C++ wrapper class for Arduino / ESP-IDF C++ projects
include/plexus_config.h   — Compile-time configuration defaults
src/plexus_internal.h     — Private header (internal declarations)
src/plexus.c              — Core implementation
src/plexus_json.c         — JSON serializer
hal/                      — Platform-specific implementations
tests/                    — Host test suite with mock HAL
```

The public header (`include/plexus.h`) exposes only an opaque `plexus_client_t*` handle. All struct internals live in `include/plexus.h` (for compile-time sizing) but are not part of the public API.

## Adding a New Platform

1. Copy `hal/template/plexus_hal_template.c` to `hal/<your_platform>/`
2. Implement all required functions (see template for contract details):
   - `plexus_hal_http_post()` — send JSON telemetry over HTTP(S)
   - `plexus_hal_get_time_ms()` — wall-clock timestamp (or return 0)
   - `plexus_hal_get_tick_ms()` — monotonic milliseconds since boot
   - `plexus_hal_delay_ms()` — blocking delay for retry backoff
   - `plexus_hal_log()` — debug output (optional)
3. Add your platform to `CMakeLists.txt`:
   ```cmake
   elseif(PLEXUS_PLATFORM STREQUAL "your_platform")
       list(APPEND PLEXUS_SOURCES hal/your_platform/plexus_hal_your_platform.c)
   ```
4. Stub your new HAL functions in `tests/mock_hal.c`
5. If Arduino-compatible, add to `library.json` `srcFilter`
6. Add a CI build step in `.github/workflows/ci.yml`
7. Verify with the HAL template's verification checklist

## Generating Docs

```bash
doxygen Doxyfile
open docs/html/index.html
```

## Publishing (Maintainers)

### PlatformIO Registry

```bash
pio pkg publish
```

### ESP-IDF Component Registry

```bash
compote component upload
```
