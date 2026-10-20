cmake_minimum_required(VERSION 3.16)

# appversion.h is the single source of the current application version.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/appversion.h")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/appversion.h" version_declaration
    REGEX "^inline constexpr const char \\*STEREOMASK_VERSION = \"[0-9]+\\.[0-9]+\\.[0-9]+\";")
if(NOT version_declaration)
    message(FATAL_ERROR "Missing numeric STEREOMASK_VERSION in appversion.h")
endif()
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" app_version "${version_declaration}")
project(StereoMask VERSION ${app_version} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core Concurrent Network)

if(WIN32 AND NOT OpenCV_DIR AND EXISTS "D:/vcpkg/installed/x64-mingw-dynamic/share/opencv4/OpenCVConfig.cmake")
    set(OpenCV_DIR "D:/vcpkg/installed/x64-mingw-dynamic/share/opencv4" CACHE PATH "OpenCV config directory")
endif()

if(WIN32 AND EXISTS "D:/vcpkg/installed/x64-mingw-dynamic")
    list(APPEND CMAKE_PREFIX_PATH "D:/vcpkg/installed/x64-mingw-dynamic")
    set(Protobuf_PROTOC_EXECUTABLE "D:/vcpkg/installed/x64-mingw-dynamic/tools/protobuf/protoc.exe" CACHE FILEPATH "vcpkg protoc")
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc)
if(OpenCV_VERSION VERSION_GREATER_EQUAL "5")
    find_package(OpenCV REQUIRED COMPONENTS core imgproc stereo)
else()
    find_package(OpenCV REQUIRED COMPONENTS core imgproc calib3d)
endif()

add_executable(StereoMask
    main.cpp
    updatechecker.cpp
    updatechecker.h
    appversion.h
    operationprogress.h
    smartselection.cpp
    smartselection.h
    mainwindow.cpp
    mainwindow.h
    quickaccess.cpp
    quickaccess.h
    creativetools.cpp
    guidedprocess.cpp
    keyboardshortcuts.cpp
    decorations.cpp
    decorations.h
    stereoviewwidget.cpp
    stereoviewwidget.h
    stereowindowview.h
    stereoimageprocessor.cpp
    stereoimageprocessor.h
    undocommand.cpp
    undocommand.h
    resources.qrc
)

if(WIN32)
    target_sources(StereoMask PRIVATE icon.rc)
endif()
if(APPLE)
    set_target_properties(StereoMask PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_GUI_IDENTIFIER "org.stereomask.brush" MACOSX_BUNDLE_BUNDLE_NAME "StereoMask Mac" MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}" MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
endif()
target_include_directories(StereoMask PRIVATE ${OpenCV_INCLUDE_DIRS})

target_link_libraries(StereoMask PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Concurrent Qt6::Network ${OpenCV_LIBS})

if(WIN32)
    set_target_properties(StereoMask PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    if(EXISTS "D:/vcpkg/installed/x64-mingw-dynamic/bin")
        set(OPENCV_RUNTIME_CANDIDATES
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libopencv_world4.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libabseil_dll.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libjpeg-62.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/liblzma.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libpng16.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libprotobuf-lite.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libprotobuf.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libsharpyuv.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libtiff.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libturbojpeg.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libwebp.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libwebpdecoder.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libwebpdemux.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libwebpmux.dll"
            "D:/vcpkg/installed/x64-mingw-dynamic/bin/libz.dll"
        )

        set(OPENCV_RUNTIME_DLLS)
        foreach(runtime_dll IN LISTS OPENCV_RUNTIME_CANDIDATES)
            if(EXISTS "${runtime_dll}")
                list(APPEND OPENCV_RUNTIME_DLLS "${runtime_dll}")
            endif()
        endforeach()

        if(OPENCV_RUNTIME_DLLS)
            add_custom_command(TARGET StereoMask POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${OPENCV_RUNTIME_DLLS}
                    $<TARGET_FILE_DIR:StereoMask>
            )
        endif()
    endif()
endif()

option(STEREOMASK_BUILD_TESTS "Build smart selection integration checks" OFF)
if(STEREOMASK_BUILD_TESTS)
    enable_testing()
    add_executable(StereoMaskSmartTest tests/smartselection_test.cpp decorations.cpp smartselection.cpp
        stereoviewwidget.cpp stereoviewwidget.h stereoimageprocessor.cpp
        undocommand.cpp undocommand.h)
    target_include_directories(StereoMaskSmartTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(StereoMaskSmartTest PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Concurrent Qt6::Network ${OpenCV_LIBS})
    add_test(NAME smart-selection COMMAND StereoMaskSmartTest)
    set_tests_properties(smart-selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(StereoMaskHelpTest tests/help_test.cpp mainwindow.cpp mainwindow.h quickaccess.cpp quickaccess.h creativetools.cpp guidedprocess.cpp keyboardshortcuts.cpp decorations.cpp updatechecker.cpp updatechecker.h
        smartselection.cpp stereoviewwidget.cpp stereoviewwidget.h
        stereoimageprocessor.cpp undocommand.cpp undocommand.h resources.qrc)
    target_include_directories(StereoMaskHelpTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(StereoMaskHelpTest PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Concurrent Qt6::Network ${OpenCV_LIBS})
    add_test(NAME user-guide COMMAND StereoMaskHelpTest)
    set_tests_properties(user-guide PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(StereoMaskUpdateTest tests/update_test.cpp updatechecker.cpp updatechecker.h)
    target_include_directories(StereoMaskUpdateTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(StereoMaskUpdateTest PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME update-checker COMMAND StereoMaskUpdateTest)
    add_executable(StereoMaskCreativeTest tests/creative_test.cpp decorations.cpp smartselection.cpp
        stereoviewwidget.cpp stereoviewwidget.h stereoimageprocessor.cpp
        undocommand.cpp undocommand.h)
    target_include_directories(StereoMaskCreativeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(StereoMaskCreativeTest PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Concurrent Qt6::Network ${OpenCV_LIBS})
    add_test(NAME creative-tools COMMAND StereoMaskCreativeTest)
    add_executable(StereoMaskQuickAccessTest tests/quickaccess_test.cpp quickaccess.cpp quickaccess.h)
    target_include_directories(StereoMaskQuickAccessTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(StereoMaskQuickAccessTest PRIVATE Qt6::Widgets)
    add_test(NAME quick-access COMMAND StereoMaskQuickAccessTest)
    get_target_property(qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin_directory "${qt_qmake_executable}" DIRECTORY)
    set_tests_properties(smart-selection user-guide creative-tools quick-access PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${qt_bin_directory}/../plugins/platforms")
    if(WIN32)
        set_tests_properties(user-guide creative-tools quick-access PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${qt_bin_directory}/../plugins/platforms")
    endif()
endif()
