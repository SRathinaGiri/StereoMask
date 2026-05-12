cmake_minimum_required(VERSION 3.16)

project(StereoMask VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core Concurrent)

if(WIN32 AND NOT OpenCV_DIR AND EXISTS "D:/vcpkg/installed/x64-mingw-dynamic/share/opencv4/OpenCVConfig.cmake")
    set(OpenCV_DIR "D:/vcpkg/installed/x64-mingw-dynamic/share/opencv4" CACHE PATH "OpenCV config directory")
endif()

if(WIN32 AND EXISTS "D:/vcpkg/installed/x64-mingw-dynamic")
    list(APPEND CMAKE_PREFIX_PATH "D:/vcpkg/installed/x64-mingw-dynamic")
    set(Protobuf_PROTOC_EXECUTABLE "D:/vcpkg/installed/x64-mingw-dynamic/tools/protobuf/protoc.exe" CACHE FILEPATH "vcpkg protoc")
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc calib3d)

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

target_link_libraries(StereoMask PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Concurrent ${OpenCV_LIBS})

if(WIN32)
    set_target_properties(StereoMask PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    if(EXISTS "D:/vcpkg/installed/x64-mingw-dynamic/bin")
        set(OPENCV_RUNTIME_DLLS
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

        add_custom_command(TARGET StereoMask POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${OPENCV_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:StereoMask>
        )
    endif()
endif()
