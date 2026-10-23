cmake_minimum_required(VERSION 3.24)

project(TorchOfBuilding
    VERSION 0.1.0
    DESCRIPTION "Offline build planner and DPS calculator for Torchlight Infinite"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TOB_BUILD_APP   "Build the Qt desktop application" ON)
option(TOB_BUILD_TESTS "Build the engine unit tests"      ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(engine)
add_subdirectory(tools/coverage)

if(TOB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TOB_BUILD_APP)
    add_subdirectory(app)
endif()
