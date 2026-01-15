cmake_minimum_required(VERSION 3.17)
project(mtpndd_jni C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(JNI REQUIRED)

set(MTPNDD_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../sylvan/src/sylvan/mtpndd" CACHE PATH "Path to MTPNDD public headers")
set(MTPNDD_LIBRARY_PATH "${CMAKE_CURRENT_LIST_DIR}/../sylvan/build/src/sylvan/mtpndd/libmtpndd.a" CACHE FILEPATH "Path to libmtpndd (static or shared library)")
set(MTPNDD_SYLVAN_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../sylvan/src/sylvan" CACHE PATH "Path to Sylvan public headers")
set(MTPNDD_SYLVAN_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../sylvan/build/src/sylvan/lib/libsylvan.a" CACHE FILEPATH "Path to libsylvan library")
set(MTPNDD_LACE_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../sylvan/build/_deps/lace-src/src" CACHE PATH "Path to Lace headers")
set(MTPNDD_LACE_BUILD_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../sylvan/build/_deps/lace-build" CACHE PATH "Path to generated Lace headers")
set(MTPNDD_LACE_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../sylvan/build/_deps/lace-build/lib/liblace.a" CACHE FILEPATH "Path to liblace library")
set(MTPNDD_LACE14_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../sylvan/build/_deps/lace-build/lib/liblace14.a" CACHE FILEPATH "Path to liblace14 library")

if(NOT EXISTS "${MTPNDD_INCLUDE_DIR}/mtpndd.h")
    message(FATAL_ERROR "MTPNDD headers not found in ${MTPNDD_INCLUDE_DIR}. Set MTPNDD_INCLUDE_DIR to the directory containing mtpndd.h")
endif()

option(MTPNDD_ENABLE_RECORDING "Enable MTPNDD recording instrumentation for JNI bridge" OFF)

add_library(mtpnddjni SHARED src/main/native/mtpndd_jni.c)
set(MTPNDD_LACE_INCLUDE_DIRS "")
if(EXISTS "${MTPNDD_LACE_INCLUDE_DIR}")
    list(APPEND MTPNDD_LACE_INCLUDE_DIRS ${MTPNDD_LACE_INCLUDE_DIR})
endif()
if(EXISTS "${MTPNDD_LACE_BUILD_INCLUDE_DIR}")
    list(APPEND MTPNDD_LACE_INCLUDE_DIRS ${MTPNDD_LACE_BUILD_INCLUDE_DIR})
endif()
set(MTPNDD_LACE_LIBS "")
if(EXISTS "${MTPNDD_LACE_LIBRARY}")
    list(APPEND MTPNDD_LACE_LIBS ${MTPNDD_LACE_LIBRARY})
endif()
if(EXISTS "${MTPNDD_LACE14_LIBRARY}")
    list(APPEND MTPNDD_LACE_LIBS ${MTPNDD_LACE14_LIBRARY})
endif()
target_include_directories(mtpnddjni
    PRIVATE
        ${JNI_INCLUDE_DIRS}
        ${MTPNDD_INCLUDE_DIR}
        ${MTPNDD_SYLVAN_INCLUDE_DIR}
        ${MTPNDD_LACE_INCLUDE_DIRS}
)
target_link_libraries(mtpnddjni PRIVATE
    ${MTPNDD_LIBRARY_PATH}
    ${MTPNDD_SYLVAN_LIBRARY}
    ${MTPNDD_LACE_LIBS}
    pthread
    m
)

if(MTPNDD_ENABLE_RECORDING)
    target_compile_definitions(mtpnddjni PRIVATE ENABLE_RECORDING)
endif()

set_target_properties(mtpnddjni PROPERTIES OUTPUT_NAME "mtpnddjni")
