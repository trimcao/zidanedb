cmake_minimum_required(VERSION 3.20)

project(
    ZidaneDB
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(ZIDANEDB_SOURCES
    src/database.cpp
    include/zidanedb/database.h
    include/zidanedb/index_stats.h
    src/index.h
    src/index.cpp
    src/utils.h
    src/utils.cpp
    src/constants.h
    src/record.h
    src/record.cpp
    src/failpoints.h
)

# Dependencies are checked into vendor/ so configuring a fresh build tree does
# not require network access.
set(CRC32C_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CRC32C_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(CRC32C_USE_GLOG OFF CACHE BOOL "" FORCE)
set(CRC32C_INSTALL OFF CACHE BOOL "" FORCE)

# Compatibility with this dependency's older CMake policy version.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

add_subdirectory(vendor/crc32c EXCLUDE_FROM_ALL)
add_subdirectory(vendor/CLI11 EXCLUDE_FROM_ALL)

function(add_zidanedb_library target_name)
    add_library(${target_name}
        ${ZIDANEDB_SOURCES}
    )

    target_link_libraries(${target_name}
        PRIVATE crc32c
    )

    target_include_directories(${target_name}
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    target_compile_features(${target_name}
        PUBLIC cxx_std_20
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )
    endif()
endfunction()

add_zidanedb_library(zidanedb)

if(BUILD_TESTING)
    add_zidanedb_library(zidanedb_with_failpoints)
    target_compile_definitions(zidanedb_with_failpoints
        PRIVATE ZIDANEDB_ENABLE_FAILPOINTS
    )
endif()

set(ZIDANE_CLI_SOURCES
    apps/zidane/commands.cpp
    apps/zidane/commands.h
)

function(add_zidane_cli_library target_name database_target)
    add_library(${target_name}
        ${ZIDANE_CLI_SOURCES}
    )

    target_include_directories(${target_name}
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/apps/zidane"
    )

    target_link_libraries(${target_name}
        PUBLIC ${database_target}
    )
endfunction()

add_zidane_cli_library(zidane_cli zidanedb)

if(BUILD_TESTING)
    add_zidane_cli_library(zidane_cli_with_failpoints zidanedb_with_failpoints)
endif()

add_executable(zidane
    apps/zidane/zidane.cpp
)

target_link_libraries(zidane
    PRIVATE
        zidane_cli
        CLI11::CLI11
)

add_executable(matrix
    apps/matrix/matrix.cpp
    apps/matrix/basic_tests.cpp
    apps/matrix/perf_basic.cpp
    apps/matrix/perf_large_values.cpp
    apps/matrix/perf_overwrite.cpp
    apps/matrix/utils.cpp
    apps/matrix/hash_collision.cpp
)

if(BUILD_TESTING)
    target_sources(matrix
        PRIVATE apps/matrix/crash_tests.cpp
    )
    target_compile_definitions(matrix
        PRIVATE ZIDANEDB_CRASH_TESTS_AVAILABLE
    )
endif()

target_link_libraries(matrix
    PRIVATE
        zidanedb
        CLI11::CLI11
)

if(BUILD_TESTING)
    add_executable(zidane_crash_worker
        apps/zidane/zidane.cpp
    )

    target_link_libraries(
        zidane_crash_worker
        PRIVATE
            zidane_cli_with_failpoints
            CLI11::CLI11
    )

    target_compile_definitions(matrix
        PRIVATE
            ZIDANEDB_CRASH_WORKER_PATH="$<TARGET_FILE:zidane_crash_worker>"
    )

    add_dependencies(matrix zidane_crash_worker)

endif()

if(BUILD_TESTING)
    add_subdirectory(vendor/Catch2 EXCLUDE_FROM_ALL)

    add_executable(database_tests
        tests/database_test.cpp
        tests/index_test.cpp
        tests/record_test.cpp
        tests/utils_test.cpp
    )

    target_include_directories(database_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    target_link_libraries(database_tests
        PRIVATE
            zidanedb
            crc32c
            Catch2::Catch2WithMain
    )

    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vendor/Catch2/extras")
    include(Catch)
    catch_discover_tests(database_tests)
endif()
