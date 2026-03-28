# Plexus C SDK CMakeLists.txt
#
# Usage for standalone build:
#   mkdir build && cd build
#   cmake .. -DPLEXUS_PLATFORM=<platform>
#   make
#
# Usage with ESP-IDF (as component):
#   Add to your project's components directory
#   Or add to EXTRA_COMPONENT_DIRS in CMakeLists.txt
#
# Supported platforms: esp32, stm32, generic

cmake_minimum_required(VERSION 3.10)

# Check if building as ESP-IDF component
if(DEFINED ENV{IDF_PATH} OR IDF_TARGET)
    # ESP-IDF component mode
    #
    # WebSocket files are always included — they compile to empty when
    # PLEXUS_ENABLE_WEBSOCKET=0 (the default) thanks to #if guards.
    # This avoids CMake ordering issues where the define isn't visible
    # at component registration time.
    idf_component_register(
        SRCS
            "src/plexus.c"
            "src/plexus_json.c"
            "src/plexus_ws.c"
            "hal/esp32/plexus_hal_esp32.c"
            "hal/esp32/plexus_hal_storage_esp32.c"
            "hal/esp32/plexus_hal_ws_esp32.c"
        INCLUDE_DIRS
            "src"
        REQUIRES
            esp_http_client
            esp_timer
            nvs_flash
            mbedtls
    )
    return()
endif()

# Standalone CMake build
project(plexus-sdk VERSION 0.8.0 LANGUAGES C CXX)

# Options
option(PLEXUS_BUILD_EXAMPLES "Build example applications" OFF)
option(PLEXUS_BUILD_TESTS "Build unit tests" OFF)
option(PLEXUS_DEBUG "Enable debug logging" OFF)

set(PLEXUS_PLATFORM "generic" CACHE STRING "Target platform (esp32, stm32, generic)")
set_property(CACHE PLEXUS_PLATFORM PROPERTY STRINGS esp32 stm32 generic arduino)

# Source files
set(PLEXUS_SOURCES
    src/plexus.c
    src/plexus_json.c
)

# WebSocket sources (only if enabled)
option(PLEXUS_ENABLE_WEBSOCKET "Enable WebSocket transport" OFF)
if(PLEXUS_ENABLE_WEBSOCKET)
    list(APPEND PLEXUS_SOURCES src/plexus_ws.c)
endif()

# Platform-specific HAL
if(PLEXUS_PLATFORM STREQUAL "esp32")
    list(APPEND PLEXUS_SOURCES hal/esp32/plexus_hal_esp32.c)
    list(APPEND PLEXUS_SOURCES hal/esp32/plexus_hal_storage_esp32.c)
elseif(PLEXUS_PLATFORM STREQUAL "stm32")
    list(APPEND PLEXUS_SOURCES hal/stm32/plexus_hal_stm32.c)
elseif(PLEXUS_PLATFORM STREQUAL "arduino")
    list(APPEND PLEXUS_SOURCES hal/arduino/plexus_hal_arduino.cpp)
else()
    # Generic/stub HAL for testing
    message(WARNING "No HAL selected. Using stub implementation.")
endif()

# Library target
add_library(plexus STATIC ${PLEXUS_SOURCES})

target_include_directories(plexus PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(plexus PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compile definitions
if(PLEXUS_DEBUG)
    target_compile_definitions(plexus PUBLIC PLEXUS_DEBUG=1)
endif()

# C standard
target_compile_features(plexus PUBLIC c_std_99)

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(plexus PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Install
install(TARGETS plexus
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY src/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Examples
if(PLEXUS_BUILD_EXAMPLES)
    message(STATUS "ESP32 examples should be built with ESP-IDF, not standalone CMake")
endif()

# Print configuration
message(STATUS "Plexus SDK Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Platform: ${PLEXUS_PLATFORM}")
message(STATUS "  Debug: ${PLEXUS_DEBUG}")
