cmake_minimum_required(VERSION 3.10)
project(EndoscopeSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# === 依赖 ===
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio calib3d)

# === 选项 ===
option(ENABLE_V4L2 "Enable V4L2 camera capture (Linux only)" ON)

# === 源文件 ===
set(SOURCES
    src/main.cpp
    src/ui/mainwindow.cpp
    src/ui/presetmanager.cpp
    src/capture/filecapture.cpp
    src/processing/imageprocessor.cpp
    src/processing/neon_accel.cpp
    src/utils/imageconvert.cpp
)
set(HEADERS
    src/capture/videosource.h
    src/capture/filecapture.h
    src/ui/mainwindow.h
    src/ui/presetmanager.h
    src/processing/imageprocessor.h
    src/processing/processthread.h
    src/processing/neon_accel.h
    src/utils/imageconvert.h
    src/utils/framequeue.h
)

if(ENABLE_V4L2 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SOURCES src/capture/v4l2capture.cpp)
    list(APPEND HEADERS src/capture/v4l2capture.h)
    add_definitions(-DENABLE_V4L2)
    message(STATUS "V4L2 capture: ENABLED")
else()
    message(STATUS "V4L2 capture: DISABLED (using file capture only)")
endif()

# === 主程序 ===
add_executable(endoscope ${SOURCES} ${HEADERS})

target_include_directories(endoscope PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(endoscope
    Qt5::Core Qt5::Gui Qt5::Widgets
    ${OpenCV_LIBS}
    pthread
)

# === 安装 ===
install(TARGETS endoscope DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/endoscope)
