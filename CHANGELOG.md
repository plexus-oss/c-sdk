# Changelog

## [0.1.0] - Initial release

- HTTP ingest to `https://plexus-gateway.fly.dev/ingest`
- Platform HALs: ESP32 (ESP-IDF), Arduino, STM32 (FreeRTOS + LwIP)
- Optional WebSocket transport (`PLEXUS_ENABLE_WEBSOCKET=1`)
- Static allocation path for MISRA-C environments
- Minimal footprint: ~1.5KB RAM (minimal config), ~17KB (default)
