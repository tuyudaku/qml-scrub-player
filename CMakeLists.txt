cmake_minimum_required(VERSION 3.21)

project(QMLScrubPlayer
    VERSION 0.1.0
    DESCRIPTION "A small Qt 6 QML video playback module"
    HOMEPAGE_URL "https://github.com/tuyudaku/qml-scrub-player"
    LANGUAGES CXX
)

option(QMLSCRUBPLAYER_BUILD_EXAMPLES "Build QMLScrubPlayer examples" ON)
option(QMLSCRUBPLAYER_BUILD_TESTS "Build QMLScrubPlayer tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Qml Quick Multimedia)
include(cmake/QMLScrubPlayerFindFFmpeg.cmake)

qt_standard_project_setup(REQUIRES 6.10)

set(QMLSCRUBPLAYER_INSTALL_QMLDIR "${CMAKE_INSTALL_LIBDIR}/qml" CACHE STRING "QML module install directory")

add_subdirectory(src)

if(QMLSCRUBPLAYER_BUILD_EXAMPLES)
    add_subdirectory(examples/basic)
endif()

if(QMLSCRUBPLAYER_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.10 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
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
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QMLScrubPlayerFindFFmpeg.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QMLScrubPlayer"
)

install(
    EXPORT QMLScrubPlayerTargets
    NAMESPACE QMLScrubPlayer::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QMLScrubPlayer"
)

install(
    FILES README.md README.ja.md LICENSE
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "tuyudaku")
set(CPACK_PACKAGE_CONTACT "https://github.com/tuyudaku/qml-scrub-player")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/build[^/]*/"
    "/\\\\.DS_Store$"
)

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
