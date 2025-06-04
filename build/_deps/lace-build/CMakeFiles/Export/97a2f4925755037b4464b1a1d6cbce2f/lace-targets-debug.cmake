#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "lace::lace" for configuration "Debug"
set_property(TARGET lace::lace APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(lace::lace PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/liblace.a"
  )

list(APPEND _cmake_import_check_targets lace::lace )
list(APPEND _cmake_import_check_files_for_lace::lace "${_IMPORT_PREFIX}/lib/liblace.a" )

# Import target "lace::lace14" for configuration "Debug"
set_property(TARGET lace::lace14 APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(lace::lace14 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/liblace14.a"
  )

list(APPEND _cmake_import_check_targets lace::lace14 )
list(APPEND _cmake_import_check_files_for_lace::lace14 "${_IMPORT_PREFIX}/lib/liblace14.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
