cmake_minimum_required(VERSION 3.20)
project(pull LANGUAGES C OBJC)

# Set C standard if needed
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Wpedantic)

# Find pkg-config packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(VENDOR REQUIRED raylib libavformat libavcodec libavdevice libavutil zlib)

# Include directories from pkg-config
include_directories(${VENDOR_INCLUDE_DIRS})

# Link directories from pkg-config
link_directories(${VENDOR_LIBRARY_DIRS})

# Set source files
set(SOURCES main.c)

# Add executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME}
    ${VENDOR_LIBRARIES}
    "-framework AVFoundation"
    "-framework Foundation"
    "-framework VideoToolbox"
    "-framework AudioToolbox"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework CoreGraphics"
)
