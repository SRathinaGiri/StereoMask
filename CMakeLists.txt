cmake_minimum_required(VERSION 3.16)

project(StereoMask VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core Concurrent)

add_executable(StereoMask
    main.cpp
    mainwindow.cpp
    mainwindow.h
    stereoviewwidget.cpp
    stereoviewwidget.h
    stereoimageprocessor.cpp
    stereoimageprocessor.h
    undocommand.cpp
    undocommand.h
    resources.qrc
    icon.rc
)

target_link_libraries(StereoMask PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Concurrent)

if(WIN32)
    set_target_properties(StereoMask PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
