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
)
target_link_libraries(TorchOfBuilding PRIVATE tob_engine Qt6::Widgets)
set_target_properties(TorchOfBuilding PROPERTIES WIN32_EXECUTABLE ON)
