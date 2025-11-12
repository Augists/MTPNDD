project(mtpndd C)

file(GLOB MTPNDD_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.c")

add_library(mtpndd STATIC ${MTPNDD_SOURCES})
add_library(mtpndd::mtpndd ALIAS mtpndd)
target_compile_features(mtpndd PUBLIC c_std_11)
set_target_properties(mtpndd PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(mtpndd
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)
target_compile_options(mtpndd PRIVATE -Wall -Wextra -Wno-unused-parameter)

option(MTPNDD_ENABLE_RECORDING "Enable MTPNDD recording instrumentation" OFF)
if(MTPNDD_ENABLE_RECORDING)
    target_compile_definitions(mtpndd PUBLIC ENABLE_RECORDING)
endif()

if(TARGET sylvan::sylvan)
    target_link_libraries(mtpndd PUBLIC sylvan::sylvan)
else()
    if(NOT DEFINED SYLVAN_ROOT)
        set(SYLVAN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "Path to Sylvan root")
    endif()

    find_path(SYLVAN_INCLUDE_DIR sylvan.h
        PATHS
            "${SYLVAN_ROOT}/include"
            "${SYLVAN_ROOT}/src"
            "${SYLVAN_ROOT}"
            ENV INCLUDE
        NO_DEFAULT_PATH
    )

    if(SYLVAN_INCLUDE_DIR)
        target_include_directories(mtpndd PUBLIC ${SYLVAN_INCLUDE_DIR})
    else()
        message(WARNING "Sylvan headers not found. Set -DSYLVAN_ROOT to the Sylvan source tree.")
    endif()

    find_library(SYLVAN_LIBRARY NAMES sylvan HINTS
        "${SYLVAN_ROOT}/lib" "${SYLVAN_ROOT}/build" "${SYLVAN_ROOT}"
    )

    if(SYLVAN_LIBRARY)
        target_link_libraries(mtpndd PUBLIC ${SYLVAN_LIBRARY})
    else()
        set(LACE_CANDIDATES
            "${SYLVAN_ROOT}/src/lace/lace14.c"
            "${SYLVAN_ROOT}/src/lace/lace.c"
            "${SYLVAN_ROOT}/src/lace14.c"
            "${SYLVAN_ROOT}/src/lace.c"
            "${SYLVAN_ROOT}/lace14.c"
            "${SYLVAN_ROOT}/lace.c"
        )
        foreach(f ${LACE_CANDIDATES})
            if(EXISTS "${f}")
                target_sources(mtpndd PRIVATE "${f}")
                break()
            endif()
        endforeach()
    endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(mtpndd PUBLIC Threads::Threads)

install(TARGETS mtpndd ARCHIVE DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/ DESTINATION include FILES_MATCHING PATTERN "*.h")

add_executable(mtpndd_nqueens_test test/nqueens.c)
target_link_libraries(mtpndd_nqueens_test PRIVATE mtpndd::mtpndd)
target_compile_features(mtpndd_nqueens_test PRIVATE c_std_11)
target_compile_options(mtpndd_nqueens_test PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
option(MTPNDD_NQUEENS_ENABLE_DOT "Dump DOT graphs when building the N-Queens test formula" ON)
if(MTPNDD_NQUEENS_ENABLE_DOT)
    target_compile_definitions(mtpndd_nqueens_test PRIVATE MTPNDD_NQUEENS_ENABLE_DOT=1)
endif()

add_executable(mtpndd_conversion_test test/conversion.c)
target_link_libraries(mtpndd_conversion_test PRIVATE mtpndd::mtpndd)
target_compile_features(mtpndd_conversion_test PRIVATE c_std_11)
target_compile_options(mtpndd_conversion_test PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
