include(FetchContent)

FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(tob_tests
    test_moddb.cpp
    test_calc.cpp
    test_parser.cpp
    test_build.cpp
)
target_link_libraries(tob_tests PRIVATE tob_engine Catch2::Catch2WithMain)
target_compile_definitions(tob_tests PRIVATE TOB_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(tob_tests)
