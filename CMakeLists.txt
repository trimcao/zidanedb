cmake_minimum_required(VERSION 3.20)

project(
    ZidaneDB
    VERSION 0.1.0
    LANGUAGES CXX
)

add_library(zidanedb
    src/database.cpp
    include/zidanedb/database.h
)

target_include_directories(zidanedb
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(zidanedb
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(zidanedb
    PUBLIC
        cxx_std_20
)

add_executable(zidane
    apps/zidane.cpp
)

target_link_libraries(zidane
    PRIVATE
        zidanedb
)

add_executable(matrix
    apps/matrix.cpp
)

target_link_libraries(matrix
    PRIVATE
        zidanedb
)

include(CTest)

if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.15.0
    )

    FetchContent_MakeAvailable(Catch2)

    add_executable(db_tests
        tests/db_test.cpp
    )

    target_link_libraries(db_tests
        PRIVATE
            zidanedb
            Catch2::Catch2WithMain
    )

    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    include(Catch)
    catch_discover_tests(db_tests)
endif()