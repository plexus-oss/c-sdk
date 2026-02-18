cmake_minimum_required(VERSION 3.10)
project(plexus-tests LANGUAGES C)

# Source files (SDK core + JSON serializer + commands)
set(SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(SDK_SOURCES
    ${SDK_DIR}/src/plexus.c
    ${SDK_DIR}/src/plexus_json.c
    ${SDK_DIR}/src/plexus_commands.c
)

# Mock HAL
set(MOCK_HAL ${CMAKE_CURRENT_SOURCE_DIR}/mock_hal.c)

# Include paths: public headers + private internal header
set(SDK_INCLUDE ${SDK_DIR}/include)
set(SDK_SRC_INCLUDE ${SDK_DIR}/src)

# ---- test_core ----
add_executable(test_core
    test_core.c
    ${SDK_SOURCES}
    ${MOCK_HAL}
)
target_include_directories(test_core PRIVATE ${SDK_INCLUDE} ${SDK_SRC_INCLUDE})
target_compile_features(test_core PRIVATE c_std_99)
target_compile_options(test_core PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)

# ---- test_json ----
add_executable(test_json
    test_json.c
    ${SDK_SOURCES}
    ${MOCK_HAL}
)
target_include_directories(test_json PRIVATE ${SDK_INCLUDE} ${SDK_SRC_INCLUDE})
target_compile_features(test_json PRIVATE c_std_99)
target_compile_options(test_json PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
target_link_libraries(test_json PRIVATE m)
