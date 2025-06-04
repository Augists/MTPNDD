# Install script for directory: /home/augists/PNDD/sylvan/src

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "0")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/augists/PNDD/build/src/lib/libsylvan.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
    "/home/augists/PNDD/sylvan/src/sylvan.h"
    "/home/augists/PNDD/sylvan/src/sylvan_bdd.h"
    "/home/augists/PNDD/sylvan/src/sylvan_cache.h"
    "/home/augists/PNDD/sylvan/src/sylvan_config.h"
    "/home/augists/PNDD/sylvan/src/sylvan_common.h"
    "/home/augists/PNDD/sylvan/src/sylvan_hash.h"
    "/home/augists/PNDD/sylvan/src/sylvan_int.h"
    "/home/augists/PNDD/sylvan/src/sylvan_ldd.h"
    "/home/augists/PNDD/sylvan/src/sylvan_ldd_int.h"
    "/home/augists/PNDD/sylvan/src/sylvan_mt.h"
    "/home/augists/PNDD/sylvan/src/sylvan_mtbdd.h"
    "/home/augists/PNDD/sylvan/src/sylvan_mtbdd_int.h"
    "/home/augists/PNDD/sylvan/src/sylvan_obj.hpp"
    "/home/augists/PNDD/sylvan/src/sylvan_stats.h"
    "/home/augists/PNDD/sylvan/src/sylvan_table.h"
    "/home/augists/PNDD/sylvan/src/sylvan_tls.h"
    "/home/augists/PNDD/sylvan/src/sylvan_zdd.h"
    "/home/augists/PNDD/sylvan/src/sylvan_zdd_int.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/sylvan/sylvan-targets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/sylvan/sylvan-targets.cmake"
         "/home/augists/PNDD/build/src/CMakeFiles/Export/a1f6a95d22d0092c14aac8d49b04c3d3/sylvan-targets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/sylvan/sylvan-targets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/sylvan/sylvan-targets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/sylvan" TYPE FILE FILES "/home/augists/PNDD/build/src/CMakeFiles/Export/a1f6a95d22d0092c14aac8d49b04c3d3/sylvan-targets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/sylvan" TYPE FILE FILES "/home/augists/PNDD/build/src/CMakeFiles/Export/a1f6a95d22d0092c14aac8d49b04c3d3/sylvan-targets-debug.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/sylvan" TYPE FILE FILES
    "/home/augists/PNDD/build/src/cmake/sylvan-config.cmake"
    "/home/augists/PNDD/build/src/cmake/sylvan-config-version.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/home/augists/PNDD/build/src/sylvan.pc")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/augists/PNDD/build/src/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
