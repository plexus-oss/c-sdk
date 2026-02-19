# Plexus Generic Firmware for ESP32
#
# This is an ESP-IDF project that builds a ready-to-flash firmware binary.
# The binary includes the Plexus C SDK with all features enabled.
#
# Build:
#   idf.py set-target esp32
#   idf.py build
#
# Flash:
#   idf.py flash monitor
#
# Or flash via browser using the Plexus dashboard (Chrome/Edge).

cmake_minimum_required(VERSION 3.16)

# Include the parent c-sdk directory as an extra component
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(plexus-firmware VERSION 0.2.1)
