cmake_minimum_required(VERSION 3.23)

project(mtpndd-c
    DESCRIPTION "MTPNDD on upstream Sylvan"
    LANGUAGES C CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# The JNI shared library needs to link Sylvan/MTPNDD as PIC static archives.
# Must be set before add_subdirectory(sylvan) so Sylvan + Lace pick it up.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(SYLVAN_ENABLE_PIC ON CACHE BOOL "Build Sylvan with -fPIC" FORCE)

# Sylvan as submodule (upstream Augists/sylvan tracking trolando/sylvan master)
add_subdirectory(sylvan)

# MTPNDD core + tests + benchmarks
add_subdirectory(mtpndd)

# JNI bindings (optional — needs JDK)
option(MTPNDD_BUILD_JNI "Build the mtpnddjni shared library" ON)
if(MTPNDD_BUILD_JNI)
    add_subdirectory(jni)
endif()
