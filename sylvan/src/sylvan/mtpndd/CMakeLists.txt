cmake_minimum_required(VERSION 3.5)
project(mtpndd C)

# Allow user to override Sylvan root (top-level sylvan directory)
if(NOT DEFINED SYLVAN_ROOT)
    # Default: three levels up from this directory (sylvan/ -> src/ -> sylvan root)
    set(SYLVAN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "Path to Sylvan root")
endif()

# Try to find sylvan include directory
find_path(SYLVAN_INCLUDE_DIR sylvan.h
    PATHS
        "${SYLVAN_ROOT}/include"
        "${SYLVAN_ROOT}/src"
        "${SYLVAN_ROOT}"
        ENV INCLUDE
    NO_DEFAULT_PATH
)

if(NOT SYLVAN_INCLUDE_DIR)
    message(WARNING "Sylvan include not found automatically. Set -DSYLVAN_ROOT to Sylvan top directory.")
else()
    message(STATUS "Found Sylvan include dir: ${SYLVAN_INCLUDE_DIR}")
endif()

# Locate Lace implementation source (prefer lace14.c then lace.c)
set(LACE_CANDIDATES
    "${SYLVAN_ROOT}/src/lace/lace14.c"
    "${SYLVAN_ROOT}/src/lace/lace.c"
    "${SYLVAN_ROOT}/src/lace14.c"
    "${SYLVAN_ROOT}/src/lace.c"
    "${SYLVAN_ROOT}/lace14.c"
    "${SYLVAN_ROOT}/lace.c"
)

set(LACE_SRC "")
foreach(f ${LACE_CANDIDATES})
    if(EXISTS "${f}")
        set(LACE_SRC "${f}")
        break()
    endif()
endforeach()

if(NOT LACE_SRC)
    message(WARNING "Could not find Lace implementation (lace14.c or lace.c) under SYLVAN_ROOT. You must compile/link Lace implementation yourself to resolve __lace_worker/__lace_dq_head.")
else()
    message(STATUS "Using Lace implementation: ${LACE_SRC}")
endif()

# Try to find prebuilt sylvan library (optional)
find_library(SYLVAN_LIBRARY NAMES sylvan HINTS
    "${SYLVAN_ROOT}/lib" "${SYLVAN_ROOT}/build" "${SYLVAN_ROOT}"
)

if(SYLVAN_LIBRARY)
    message(STATUS "Found Sylvan library: ${SYLVAN_LIBRARY}")
endif()

# Collect mtpndd sources in this directory
file(GLOB MTPNDD_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.c")

# If required headers are in parent directories, add them
include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
)
if(SYLVAN_INCLUDE_DIR)
    include_directories(${SYLVAN_INCLUDE_DIR})
endif()

# Create target (static lib)
add_library(mtpndd STATIC ${MTPNDD_SOURCES})
add_library(mtpndd::mtpndd ALIAS mtpndd)
target_compile_features(mtpndd PRIVATE c_std_99)

# If Lace source found, compile it into the target so Lace globals are defined
if(LACE_SRC)
    target_sources(mtpndd PRIVATE "${LACE_SRC}")
    # If lace14.c expects special defines, you can add here:
    # target_compile_definitions(mtpndd PRIVATE LACE_USE_14)
endif()

# Prefer linking against an existing Sylvan target; fall back to the found library path.
set(_MTPNDD_SYLVAN_LINK "")
foreach(_candidate sylvan::sylvan sylvan)
    if(TARGET ${_candidate})
        set(_MTPNDD_SYLVAN_LINK ${_candidate})
        break()
    endif()
endforeach()

if(_MTPNDD_SYLVAN_LINK)
    foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_prop_entries ${_MTPNDD_SYLVAN_LINK} ${_prop})
        if(_prop_entries AND NOT _prop_entries STREQUAL "NOTFOUND")
            foreach(_entry ${_prop_entries})
                string(FIND "${_entry}" "mtpndd" _entry_has_mtpndd)
                if(NOT _entry_has_mtpndd EQUAL -1)
                    message(FATAL_ERROR
                        "Circular dependency detected: ${_MTPNDD_SYLVAN_LINK} already depends on ${_entry}. "
                        "Linking mtpndd against ${_MTPNDD_SYLVAN_LINK} would create a cycle.")
                endif()
            endforeach()
        endif()
    endforeach()
    message(STATUS "Linking mtpndd with Sylvan target: ${_MTPNDD_SYLVAN_LINK}")
    target_link_libraries(mtpndd PRIVATE ${_MTPNDD_SYLVAN_LINK})
elseif(SYLVAN_LIBRARY)
    target_link_libraries(mtpndd PRIVATE ${SYLVAN_LIBRARY})
endif()

# Always link pthread (Lace uses threads)
find_package(Threads REQUIRED)
target_link_libraries(mtpndd PRIVATE Threads::Threads)

# Helpful compile options
target_compile_options(mtpndd PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Provide a fast check to ensure the generated Makefile knows how to build mtpndd.
if(CMAKE_GENERATOR MATCHES "Makefiles")
    if(NOT CMAKE_MAKE_PROGRAM)
        message(FATAL_ERROR "CMake reports a Makefile generator but no make tool is configured.")
    endif()
    add_custom_target(mtpndd_makefile_check
        COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}" --target mtpndd -- -n
        COMMENT "Checking that the generated Makefile can build mtpndd (dry run)"
        VERBATIM
    )
endif()

# Install rules (optional)
install(TARGETS mtpndd ARCHIVE DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/ DESTINATION include FILES_MATCHING PATTERN "*.h")

add_executable(mtpndd_nqueens_test
    test/nqueens.c
)
target_link_libraries(mtpndd_nqueens_test PRIVATE mtpndd::mtpndd sylvan::sylvan Threads::Threads)
target_compile_features(mtpndd_nqueens_test PRIVATE c_std_99)
target_compile_options(mtpndd_nqueens_test PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)

if(DEFINED BUILD_TESTING AND BUILD_TESTING)
    add_test(NAME mtpndd_nqueens COMMAND mtpndd_nqueens_test)
endif()
