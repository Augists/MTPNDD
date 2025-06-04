# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/augists/PNDD/build/_deps/lace-src")
  file(MAKE_DIRECTORY "/home/augists/PNDD/build/_deps/lace-src")
endif()
file(MAKE_DIRECTORY
  "/home/augists/PNDD/build/_deps/lace-build"
  "/home/augists/PNDD/build/_deps/lace-subbuild/lace-populate-prefix"
  "/home/augists/PNDD/build/_deps/lace-subbuild/lace-populate-prefix/tmp"
  "/home/augists/PNDD/build/_deps/lace-subbuild/lace-populate-prefix/src/lace-populate-stamp"
  "/home/augists/PNDD/build/_deps/lace-subbuild/lace-populate-prefix/src"
  "/home/augists/PNDD/build/_deps/lace-subbuild/lace-populate-prefix/src/lace-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/augists/PNDD/build/_deps/lace-subbuild/lace-populate-prefix/src/lace-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/augists/PNDD/build/_deps/lace-subbuild/lace-populate-prefix/src/lace-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
