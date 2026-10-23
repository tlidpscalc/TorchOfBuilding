include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_library(tob_engine STATIC
    src/Tags.cpp
    src/ModDB.cpp
    src/ModParser.cpp
    src/Calc.cpp
    src/GameData.cpp
    src/Build.cpp
    src/SkillDps.cpp
)

target_include_directories(tob_engine PUBLIC include)
target_compile_features(tob_engine PUBLIC cxx_std_20)
target_link_libraries(tob_engine PUBLIC nlohmann_json::nlohmann_json)
