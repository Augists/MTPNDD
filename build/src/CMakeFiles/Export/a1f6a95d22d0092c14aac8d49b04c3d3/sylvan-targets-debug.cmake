#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "sylvan::sylvan" for configuration "Debug"
set_property(TARGET sylvan::sylvan APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(sylvan::sylvan PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C;CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libsylvan.a"
  )

list(APPEND _cmake_import_check_targets sylvan::sylvan )
list(APPEND _cmake_import_check_files_for_sylvan::sylvan "${_IMPORT_PREFIX}/lib/libsylvan.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
