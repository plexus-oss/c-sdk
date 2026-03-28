cmake_minimum_required(VERSION 3.16)

# Enable WebSocket in the Plexus C SDK — must be set before project()
# so the define propagates to all components including c-sdk
add_compile_definitions(PLEXUS_ENABLE_WEBSOCKET=1)
add_compile_definitions(PLEXUS_ENABLE_THREAD_SAFE=1)

# Include the Plexus SDK as a component
set(EXTRA_COMPONENT_DIRS "../../")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(plexus_websocket)
