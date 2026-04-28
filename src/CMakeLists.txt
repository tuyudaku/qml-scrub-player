qt_add_qml_module(QMLScrubPlayer
    URI QMLScrubPlayer
    VERSION 1.0
    CLASS_NAME QMLScrubPlayerPlugin
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/QMLScrubPlayer"
    SOURCES
        qmlscrubplayer.cpp
        qmlscrubplayer.h
)

add_library(QMLScrubPlayer::QMLScrubPlayer ALIAS QMLScrubPlayer)

target_link_libraries(QMLScrubPlayer
    PUBLIC
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::Multimedia
        PkgConfig::FFMPEG
)

target_compile_definitions(QMLScrubPlayer
    PRIVATE QMLSCRUBPLAYER_LIBRARY
)

target_include_directories(QMLScrubPlayer
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/QMLScrubPlayer>"
)

set_target_properties(QMLScrubPlayer PROPERTIES
    OUTPUT_NAME QMLScrubPlayer
    EXPORT_NAME QMLScrubPlayer
)

install(
    TARGETS QMLScrubPlayer
    EXPORT QMLScrubPlayerTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/QMLScrubPlayer"
)

install(
    FILES qmlscrubplayer.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/QMLScrubPlayer"
)

if(TARGET QMLScrubPlayerplugin)
    install(
        TARGETS QMLScrubPlayerplugin
        LIBRARY DESTINATION "${QMLSCRUBPLAYER_INSTALL_QMLDIR}/QMLScrubPlayer"
        RUNTIME DESTINATION "${QMLSCRUBPLAYER_INSTALL_QMLDIR}/QMLScrubPlayer"
        BUNDLE DESTINATION "${QMLSCRUBPLAYER_INSTALL_QMLDIR}/QMLScrubPlayer"
    )
endif()

install(
    DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/QMLScrubPlayer/"
    DESTINATION "${QMLSCRUBPLAYER_INSTALL_QMLDIR}/QMLScrubPlayer"
    FILES_MATCHING
        PATTERN "qmldir"
        PATTERN "*.qmltypes"
)
