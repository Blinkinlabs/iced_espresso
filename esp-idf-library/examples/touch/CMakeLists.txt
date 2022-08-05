# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS
    ../../
    )

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(touch)

# Embed the FPGA file into the project binary
target_add_binary_data(touch.elf "fpga/top.bin" BINARY)
