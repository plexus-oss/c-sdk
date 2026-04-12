cmake_minimum_required(VERSION 3.16)

# Enable WebSocket + thread safety in the Plexus C SDK
# Must be before project() so defines propagate to all components
add_compile_definitions(PLEXUS_ENABLE_WEBSOCKET=1)
add_compile_definitions(PLEXUS_ENABLE_THREAD_SAFE=1)

# Include the Plexus SDK as a component
set(EXTRA_COMPONENT_DIRS "../../")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(plexus_flashable)
