cmake_minimum_required(VERSION 3.10)
project(plexus-tests LANGUAGES C)

enable_testing()

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

# Sanitizers (enabled by default in debug builds, disabled with -DPLEXUS_NO_SANITIZERS=ON)
option(PLEXUS_NO_SANITIZERS "Disable ASan/UBSan" OFF)
set(SANITIZER_FLAGS "")
if(NOT PLEXUS_NO_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# Minimal config build option (tests that the stripped-down config compiles)
option(PLEXUS_MINIMAL_CONFIG "Build with minimal memory configuration" OFF)
set(MINIMAL_FLAGS "")
if(PLEXUS_MINIMAL_CONFIG)
    set(MINIMAL_FLAGS
        -DPLEXUS_MAX_METRICS=8
        -DPLEXUS_JSON_BUFFER_SIZE=512
        -DPLEXUS_ENABLE_TAGS=0
        -DPLEXUS_ENABLE_STRING_VALUES=0
        -DPLEXUS_ENABLE_BOOL_VALUES=0
        -DPLEXUS_MAX_ENDPOINT_LEN=128
        -DPLEXUS_MAX_API_KEY_LEN=64
    )
endif()

# ---- test_core ----
add_executable(test_core
    test_core.c
    ${SDK_SOURCES}
    ${MOCK_HAL}
)
target_include_directories(test_core PRIVATE ${SDK_INCLUDE} ${SDK_SRC_INCLUDE})
target_compile_features(test_core PRIVATE c_std_99)
target_compile_options(test_core PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-result ${SANITIZER_FLAGS} ${MINIMAL_FLAGS})
target_link_options(test_core PRIVATE ${SANITIZER_FLAGS})
target_link_libraries(test_core PRIVATE m)

add_test(NAME test_core COMMAND test_core)

# ---- test_json ----
add_executable(test_json
    test_json.c
    ${SDK_SOURCES}
    ${MOCK_HAL}
)
target_include_directories(test_json PRIVATE ${SDK_INCLUDE} ${SDK_SRC_INCLUDE})
target_compile_features(test_json PRIVATE c_std_99)
target_compile_options(test_json PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-result ${SANITIZER_FLAGS} ${MINIMAL_FLAGS})
target_link_options(test_json PRIVATE ${SANITIZER_FLAGS})
target_link_libraries(test_json PRIVATE m)

add_test(NAME test_json COMMAND test_json)

# ---- test_status ----
add_executable(test_status
    test_status.c
    ${SDK_SOURCES}
    ${MOCK_HAL}
)
target_include_directories(test_status PRIVATE ${SDK_INCLUDE} ${SDK_SRC_INCLUDE})
target_compile_features(test_status PRIVATE c_std_99)
target_compile_options(test_status PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-result ${SANITIZER_FLAGS} -DPLEXUS_ENABLE_STATUS_CALLBACK=1)
target_link_options(test_status PRIVATE ${SANITIZER_FLAGS})
target_link_libraries(test_status PRIVATE m)

add_test(NAME test_status COMMAND test_status)

# ---- test_threadsafe ----
add_executable(test_threadsafe
    test_threadsafe.c
    ${SDK_SOURCES}
    ${MOCK_HAL}
)
target_include_directories(test_threadsafe PRIVATE ${SDK_INCLUDE} ${SDK_SRC_INCLUDE})
target_compile_features(test_threadsafe PRIVATE c_std_99)
target_compile_options(test_threadsafe PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-result ${SANITIZER_FLAGS} -DPLEXUS_ENABLE_THREAD_SAFE=1)
target_link_options(test_threadsafe PRIVATE ${SANITIZER_FLAGS})
target_link_libraries(test_threadsafe PRIVATE m)

add_test(NAME test_threadsafe COMMAND test_threadsafe)

# ---- test_persist ----
add_executable(test_persist
    test_persist.c
    ${SDK_SOURCES}
    ${MOCK_HAL}
)
target_include_directories(test_persist PRIVATE ${SDK_INCLUDE} ${SDK_SRC_INCLUDE})
target_compile_features(test_persist PRIVATE c_std_99)
target_compile_options(test_persist PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-result ${SANITIZER_FLAGS} -DPLEXUS_ENABLE_PERSISTENT_BUFFER=1)
target_link_options(test_persist PRIVATE ${SANITIZER_FLAGS})
target_link_libraries(test_persist PRIVATE m)

add_test(NAME test_persist COMMAND test_persist)

# ---- test_heartbeat ----
add_executable(test_heartbeat
    test_heartbeat.c
    ${SDK_SOURCES}
    ${MOCK_HAL}
)
target_include_directories(test_heartbeat PRIVATE ${SDK_INCLUDE} ${SDK_SRC_INCLUDE})
target_compile_features(test_heartbeat PRIVATE c_std_99)
target_compile_options(test_heartbeat PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-result ${SANITIZER_FLAGS} -DPLEXUS_ENABLE_HEARTBEAT=1)
target_link_options(test_heartbeat PRIVATE ${SANITIZER_FLAGS})
target_link_libraries(test_heartbeat PRIVATE m)

add_test(NAME test_heartbeat COMMAND test_heartbeat)

# ---- test_mqtt ----
add_executable(test_mqtt
    test_mqtt.c
    ${SDK_SOURCES}
    ${MOCK_HAL}
)
target_include_directories(test_mqtt PRIVATE ${SDK_INCLUDE} ${SDK_SRC_INCLUDE})
target_compile_features(test_mqtt PRIVATE c_std_99)
target_compile_options(test_mqtt PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-result ${SANITIZER_FLAGS} -DPLEXUS_ENABLE_MQTT=1)
target_link_options(test_mqtt PRIVATE ${SANITIZER_FLAGS})
target_link_libraries(test_mqtt PRIVATE m)

add_test(NAME test_mqtt COMMAND test_mqtt)
