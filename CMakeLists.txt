cmake_minimum_required(VERSION 3.21)

project(QMLScrubPlayer
    VERSION 0.1.0
    DESCRIPTION "A small Qt 6 QML video playback module"
    HOMEPAGE_URL "https://github.com/yourname/QMLScrubPlayer"
    LANGUAGES CXX
)

option(QMLSCRUBPLAYER_BUILD_EXAMPLES "Build QMLScrubPlayer examples" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(PkgConfig)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Qml Quick Multimedia)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
        libswscale
        libswresample
    )
endif()

if(NOT TARGET PkgConfig::FFMPEG)
    find_path(FFMPEG_INCLUDE_DIR
        NAMES libavformat/avformat.h
        HINTS "${FFMPEG_ROOT}/include"
    )
    find_library(FFMPEG_AVFORMAT_LIBRARY avformat HINTS "${FFMPEG_ROOT}/lib")
    find_library(FFMPEG_AVCODEC_LIBRARY avcodec HINTS "${FFMPEG_ROOT}/lib")
    find_library(FFMPEG_AVUTIL_LIBRARY avutil HINTS "${FFMPEG_ROOT}/lib")
    find_library(FFMPEG_SWSCALE_LIBRARY swscale HINTS "${FFMPEG_ROOT}/lib")
    find_library(FFMPEG_SWRESAMPLE_LIBRARY swresample HINTS "${FFMPEG_ROOT}/lib")

    if(NOT FFMPEG_INCLUDE_DIR OR NOT FFMPEG_AVFORMAT_LIBRARY OR NOT FFMPEG_AVCODEC_LIBRARY
        OR NOT FFMPEG_AVUTIL_LIBRARY OR NOT FFMPEG_SWSCALE_LIBRARY OR NOT FFMPEG_SWRESAMPLE_LIBRARY)
        message(FATAL_ERROR "FFmpeg was not found. Install FFmpeg development libraries or set FFMPEG_ROOT.")
    endif()

    add_library(QMLScrubPlayerFFmpeg INTERFACE)
    add_library(PkgConfig::FFMPEG ALIAS QMLScrubPlayerFFmpeg)
    target_include_directories(QMLScrubPlayerFFmpeg INTERFACE "${FFMPEG_INCLUDE_DIR}")
    target_link_libraries(QMLScrubPlayerFFmpeg INTERFACE
        "${FFMPEG_AVFORMAT_LIBRARY}"
        "${FFMPEG_AVCODEC_LIBRARY}"
        "${FFMPEG_AVUTIL_LIBRARY}"
        "${FFMPEG_SWSCALE_LIBRARY}"
        "${FFMPEG_SWRESAMPLE_LIBRARY}"
    )
endif()

qt_standard_project_setup(REQUIRES 6.10)

set(QMLSCRUBPLAYER_INSTALL_QMLDIR "${CMAKE_INSTALL_LIBDIR}/qml" CACHE STRING "QML module install directory")

add_subdirectory(src)

if(QMLSCRUBPLAYER_BUILD_EXAMPLES)
    add_subdirectory(examples/basic)
endif()

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/QMLScrubPlayerConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QMLScrubPlayerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/QMLScrubPlayerConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QMLScrubPlayer"
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/QMLScrubPlayerConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/QMLScrubPlayerConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QMLScrubPlayer"
)

install(
    EXPORT QMLScrubPlayerTargets
    NAMESPACE QMLScrubPlayer::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QMLScrubPlayer"
)
