cmake_minimum_required(VERSION 3.17)
project(mtpndd_jni C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(JNI REQUIRED)

set(MTPNDD_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../sylvan/src/sylvan/mtpndd" CACHE PATH "Path to MTPNDD public headers")
set(MTPNDD_LIBRARY_PATH "${CMAKE_CURRENT_LIST_DIR}/../sylvan/build/src/sylvan/mtpndd/libmtpndd.a" CACHE FILEPATH "Path to libmtpndd (static or shared library)")
set(MTPNDD_SYLVAN_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../sylvan/src/sylvan" CACHE PATH "Path to Sylvan public headers")
set(MTPNDD_SYLVAN_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../sylvan/build/src/sylvan/lib/libsylvan.a" CACHE FILEPATH "Path to libsylvan library")
set(MTPNDD_LACE_INCLUDE_DIR "" CACHE PATH "Optional path to Lace source headers")
set(MTPNDD_LACE_BUILD_INCLUDE_DIR "" CACHE PATH "Optional path to Lace generated headers")
set(MTPNDD_LACE_LIBRARY "" CACHE FILEPATH "Optional path to liblace")
set(MTPNDD_LACE14_LIBRARY "" CACHE FILEPATH "Optional path to liblace14")

if(NOT EXISTS "${MTPNDD_INCLUDE_DIR}/mtpndd.h")
    message(FATAL_ERROR "MTPNDD headers not found in ${MTPNDD_INCLUDE_DIR}. Set MTPNDD_INCLUDE_DIR to the directory containing mtpndd.h")
endif()

set(_mtpndd_optional_include_dirs)
foreach(_dir IN ITEMS "${MTPNDD_LACE_INCLUDE_DIR}" "${MTPNDD_LACE_BUILD_INCLUDE_DIR}")
    if(_dir)
        if(EXISTS "${_dir}")
            list(APPEND _mtpndd_optional_include_dirs "${_dir}")
        else()
            message(WARNING "Ignoring missing optional Lace include directory: ${_dir}")
        endif()
    endif()
endforeach()

set(_mtpndd_optional_lace_libs)
foreach(_lib IN ITEMS "${MTPNDD_LACE_LIBRARY}" "${MTPNDD_LACE14_LIBRARY}")
    if(_lib)
        if(EXISTS "${_lib}")
            list(APPEND _mtpndd_optional_lace_libs "${_lib}")
        else()
            message(WARNING "Ignoring missing optional Lace library: ${_lib}")
        endif()
    endif()
endforeach()

set(MTPNDD_LOG_LEVEL "1" CACHE STRING "MTPNDD log level: 0=QUIET, 1=INFO, 2=DEBUG")
set_property(CACHE MTPNDD_LOG_LEVEL PROPERTY STRINGS 0 1 2)

add_library(mtpnddjni SHARED src/main/native/mtpndd_jni.c)
target_include_directories(mtpnddjni
    PRIVATE
        ${JNI_INCLUDE_DIRS}
        ${MTPNDD_INCLUDE_DIR}
        ${MTPNDD_SYLVAN_INCLUDE_DIR}
        ${_mtpndd_optional_include_dirs}
)
target_link_libraries(mtpnddjni PRIVATE
    ${MTPNDD_LIBRARY_PATH}
    ${MTPNDD_SYLVAN_LIBRARY}
    ${_mtpndd_optional_lace_libs}
    pthread
    m
)

target_compile_definitions(mtpnddjni PRIVATE MTPNDD_LOG_LEVEL=${MTPNDD_LOG_LEVEL})

set_target_properties(mtpnddjni PROPERTIES OUTPUT_NAME "mtpnddjni")
