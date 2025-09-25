cmake_minimum_required(VERSION 3.17)

# NDD project configuration
project(ndd
    VERSION 1.0.0
    DESCRIPTION "Network Decision Diagram with Parallel Support"
    LANGUAGES C
)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compilation options
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -g")

# Find dependencies
find_package(Threads REQUIRED)

# Check if Lace and Sylvan are available
if(TARGET lace::lace AND TARGET sylvan::sylvan)
    message(STATUS "✅ Found Lace and Sylvan dependencies")
    set(HAVE_LACE_SYLVAN TRUE)
else()
    message(STATUS "⚠️  Lace or Sylvan not found, using simplified mode")
    set(HAVE_LACE_SYLVAN FALSE)
endif()

# Source files
set(NDD_SOURCES
    common.c
    ndd.c
    ndd_nodetable.c
    operation_cache.c
    ndd_advanced.c
    mtpndd.c
    ndd_performance_monitor.c
)

set(NDD_HEADERS
    common.h
    ndd.h
    ndd_nodetable.h
    operation_cache.h
    ndd_advanced.h
    mtpndd.h
    ndd_performance_monitor.h
)

# Always add parallel implementation (includes simplified mode fallback)
list(APPEND NDD_SOURCES ndd_parallel.c)
if(HAVE_LACE_SYLVAN)
    message(STATUS "✅ Adding parallel implementation with Lace + Sylvan")
else()
    message(STATUS "✅ Adding parallel implementation with simplified mode")
endif()

# Create NDD library
add_library(ndd STATIC ${NDD_SOURCES} ${NDD_HEADERS})

# Set include directories
target_include_directories(ndd PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(ndd PUBLIC Threads::Threads)

if(HAVE_LACE_SYLVAN)
    target_link_libraries(ndd PUBLIC lace::lace sylvan::sylvan)
    target_compile_definitions(ndd PUBLIC HAVE_LACE_SYLVAN=1)
    message(STATUS "✅ NDD linked with Lace + Sylvan")
endif()

# Set output directories
set_target_properties(ndd PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
)

# Test programs
set(TEST_PROGRAMS
    test_memory_management
    test_parallel_verification
    test_parallel_logic
    test_error_handling
    test_cache_performance
    test_performance_benchmark
    test_simple_performance
    test_advanced_operations
    test_mtpndd_structure
    test_mtpndd_operations
    test_mtpndd_encoding
    test_mtpndd_serialization
    test_mtpndd_logic
    test_mtpndd_custom_operations
    test_mtpndd_constraints
    test_performance_monitoring
)

foreach(test_prog ${TEST_PROGRAMS})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/${test_prog}.c)
        add_executable(${test_prog} tests/${test_prog}.c)
        target_link_libraries(${test_prog} PRIVATE ndd)
        
        # If Lace + Sylvan available, enable full parallel testing
        if(HAVE_LACE_SYLVAN)
            target_compile_definitions(${test_prog} PRIVATE HAVE_LACE_SYLVAN=1)
        endif()
        
        message(STATUS "✅ Added test program: ${test_prog}")
    endif()
endforeach()

# Example programs
add_executable(nqueens_mtpndd examples/nqueens_mtpndd.c)
target_link_libraries(nqueens_mtpndd PRIVATE ndd)

add_executable(nqueens_ndd examples/nqueens_ndd.c)
target_link_libraries(nqueens_ndd PRIVATE ndd)


# If Lace + Sylvan available, enable full parallel support
if(HAVE_LACE_SYLVAN)
    target_compile_definitions(nqueens_mtpndd PRIVATE HAVE_LACE_SYLVAN=1)
    target_compile_definitions(nqueens_ndd PRIVATE HAVE_LACE_SYLVAN=1)
endif()

message(STATUS "✅ Added example program: nqueens_mtpndd")
message(STATUS "✅ Added example program: nqueens_ndd")

# Custom targets
add_custom_target(run_tests
    COMMAND echo "🚀 Running NDD tests..."
    COMMAND $<TARGET_FILE:test_memory_management>
    DEPENDS test_memory_management
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running NDD memory management tests"
)

if(TARGET test_parallel_verification)
    add_custom_command(TARGET run_tests POST_BUILD
        COMMAND echo "🚀 Running parallel verification tests..."
        COMMAND $<TARGET_FILE:test_parallel_verification>
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running NDD parallel verification tests"
    )
endif()

# Status report
message(STATUS "")
message(STATUS "🔧 NDD Project Configuration Summary:")
message(STATUS "   C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "   Parallel Support: ${HAVE_LACE_SYLVAN}")
if(HAVE_LACE_SYLVAN)
    message(STATUS "   🚀 Full parallel implementation enabled")
else()
    message(STATUS "   ⚡ Simplified mode (no Lace/Sylvan)")
endif()
message(STATUS "   Test Programs: ${TEST_PROGRAMS}")
message(STATUS "")