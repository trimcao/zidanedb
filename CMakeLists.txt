cmake_minimum_required(VERSION 3.20)

project(
    ZidaneDB
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies are checked into vendor/ so configuring a fresh build tree does
# not require network access.
add_subdirectory(vendor/CLI11 EXCLUDE_FROM_ALL)

add_library(zidanedb
    src/database.cpp
    include/zidanedb/database.h
    include/zidanedb/index_stats.h
    src/index.h
    src/index.cpp
    src/utils.h
    src/utils.cpp
    src/constants.h
)

target_include_directories(zidanedb
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(zidanedb
    PUBLIC
        cxx_std_20
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(zidanedb PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_library(zidane_cli
    apps/zidane/commands.cpp
    apps/zidane/commands.h
)

target_include_directories(zidane_cli
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/apps/zidane"
)

target_link_libraries(zidane_cli
    PUBLIC zidanedb
)

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

target_link_libraries(matrix
    PRIVATE
        zidanedb
        CLI11::CLI11
)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(vendor/Catch2 EXCLUDE_FROM_ALL)

    add_executable(database_tests
        tests/database_test.cpp
        tests/utils_test.cpp
    )

    target_include_directories(database_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    target_link_libraries(database_tests
        PRIVATE
            zidanedb
            Catch2::Catch2WithMain
    )

    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vendor/Catch2/extras")
    include(Catch)
    catch_discover_tests(database_tests)
endif()
