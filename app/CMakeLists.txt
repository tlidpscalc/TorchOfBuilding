if(DEFINED ENV{QT_DIR})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
endif()
find_package(Qt6 6.5 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    message(WARNING "Qt6 not found: skipping the desktop app. Set QT_DIR / CMAKE_PREFIX_PATH or use -DTOB_BUILD_APP=OFF.")
    return()
endif()

qt_standard_project_setup()

qt_add_executable(TorchOfBuilding
    main.cpp
    MainWindow.h
    MainWindow.cpp
    Editors.h
    Editors.cpp
    CraftDialog.h
    CraftDialog.cpp
    HeroEditor.h
    HeroEditor.cpp
)
target_link_libraries(TorchOfBuilding PRIVATE tob_engine Qt6::Widgets)
if(WIN32)
    target_link_libraries(TorchOfBuilding PRIVATE dwmapi)   # dark title bars
endif()
set_target_properties(TorchOfBuilding PROPERTIES WIN32_EXECUTABLE ON)

# Headless crafter check (not shipped): QT_QPA_PLATFORM=offscreen tob_craft_selftest <data dir>
qt_add_executable(tob_craft_selftest craft_selftest.cpp CraftDialog.h CraftDialog.cpp Editors.h Editors.cpp HeroEditor.h HeroEditor.cpp)
target_link_libraries(tob_craft_selftest PRIVATE tob_engine Qt6::Widgets)
