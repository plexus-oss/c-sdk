cmake_minimum_required(VERSION 3.16)

# Include the Plexus SDK as a component
set(EXTRA_COMPONENT_DIRS "../../")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(plexus_flashable)
