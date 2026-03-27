# Contributing to Plexus C SDK

Thanks for your interest in contributing! Here's how to get started.

## Development Setup

```bash
git clone https://github.com/plexus-oss/c-sdk.git
cd c-sdk
mkdir build && cd build
cmake .. -DPLEXUS_PLATFORM=generic -DPLEXUS_BUILD_TESTS=ON
make
```

## Running Tests

```bash
cd build
ctest --output-on-failure
```

Tests run with AddressSanitizer and UndefinedBehaviorSanitizer enabled by default in the CI pipeline.

## Submitting Changes

1. Fork the repo and create a branch from `main`
2. Make your changes — add tests for new functionality
3. Run the test suite and make sure it passes
4. Keep memory usage in mind — this SDK targets devices with as little as 1.5 KB RAM
5. Open a pull request with a clear description of what and why

## Reporting Bugs

Open an issue at [GitHub Issues](https://github.com/plexus-oss/c-sdk/issues) with:
- Platform (ESP32, STM32, Arduino, etc.) and toolchain version
- SDK version and configuration (`plexus_config.h` overrides)
- Steps to reproduce
- Expected vs actual behavior

## Code Style

- C99, no compiler extensions
- Follow existing naming: `plexus_` prefix for public API, `plexus__` for internal
- No dynamic allocation in the core library
- Keep the public API surface small — add to `plexus.h` only when necessary

## Adding Platform Support

If you're porting to a new platform, start from the HAL template at `hal/template/plexus_hal_template.c` and follow the checklist in the README porting guide.

## License

By contributing, you agree that your contributions will be licensed under the [Apache License 2.0](LICENSE).
