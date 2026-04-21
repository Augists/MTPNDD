cmake_minimum_required(VERSION 3.23)

project(mtpndd-c
    DESCRIPTION "MTPNDD on upstream Sylvan"
    LANGUAGES C CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Sylvan as submodule (upstream Augists/sylvan tracking trolando/sylvan master)
add_subdirectory(sylvan)

# MTPNDD core + tests + benchmarks
add_subdirectory(mtpndd)
