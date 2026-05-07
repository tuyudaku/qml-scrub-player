include_guard(GLOBAL)

find_package(PkgConfig QUIET)

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
    find_package(FFMPEG QUIET COMPONENTS avformat avcodec avutil swscale swresample)
    if(TARGET FFMPEG::avformat AND TARGET FFMPEG::avcodec AND TARGET FFMPEG::avutil
        AND TARGET FFMPEG::swscale AND TARGET FFMPEG::swresample)
        add_library(QMLScrubPlayerFFmpeg INTERFACE)
        add_library(PkgConfig::FFMPEG ALIAS QMLScrubPlayerFFmpeg)
        target_link_libraries(QMLScrubPlayerFFmpeg INTERFACE
            FFMPEG::avformat
            FFMPEG::avcodec
            FFMPEG::avutil
            FFMPEG::swscale
            FFMPEG::swresample
        )
    endif()
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

    if(FFMPEG_INCLUDE_DIR AND FFMPEG_AVFORMAT_LIBRARY AND FFMPEG_AVCODEC_LIBRARY
        AND FFMPEG_AVUTIL_LIBRARY AND FFMPEG_SWSCALE_LIBRARY AND FFMPEG_SWRESAMPLE_LIBRARY)
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
endif()

if(NOT TARGET PkgConfig::FFMPEG)
    message(FATAL_ERROR "FFmpeg was not found. Install FFmpeg development libraries or set FFMPEG_ROOT.")
endif()
