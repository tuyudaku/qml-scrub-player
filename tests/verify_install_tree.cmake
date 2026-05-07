if(NOT DEFINED QMLSCRUBPLAYER_TEST_INSTALL_PREFIX)
    message(FATAL_ERROR "QMLSCRUBPLAYER_TEST_INSTALL_PREFIX is required")
endif()

set(prefix "${QMLSCRUBPLAYER_TEST_INSTALL_PREFIX}")

set(expected_files
    "${prefix}/${CMAKE_INSTALL_INCLUDEDIR}/QMLScrubPlayer/qmlscrubplayer.h"
    "${prefix}/${CMAKE_INSTALL_LIBDIR}/cmake/QMLScrubPlayer/QMLScrubPlayerConfig.cmake"
    "${prefix}/${CMAKE_INSTALL_LIBDIR}/cmake/QMLScrubPlayer/QMLScrubPlayerConfigVersion.cmake"
    "${prefix}/${CMAKE_INSTALL_LIBDIR}/cmake/QMLScrubPlayer/QMLScrubPlayerFindFFmpeg.cmake"
    "${prefix}/${CMAKE_INSTALL_LIBDIR}/cmake/QMLScrubPlayer/QMLScrubPlayerTargets.cmake"
    "${prefix}/${CMAKE_INSTALL_LIBDIR}/qml/QMLScrubPlayer/qmldir"
    "${prefix}/${CMAKE_INSTALL_DATAROOTDIR}/doc/QMLScrubPlayer/README.md"
    "${prefix}/${CMAKE_INSTALL_DATAROOTDIR}/doc/QMLScrubPlayer/README.ja.md"
    "${prefix}/${CMAKE_INSTALL_DATAROOTDIR}/doc/QMLScrubPlayer/LICENSE"
)

foreach(expected_file IN LISTS expected_files)
    if(NOT EXISTS "${expected_file}")
        message(FATAL_ERROR "Expected installed file is missing: ${expected_file}")
    endif()
endforeach()

file(GLOB qmltype_files "${prefix}/${CMAKE_INSTALL_LIBDIR}/qml/QMLScrubPlayer/*.qmltypes")
if(NOT qmltype_files)
    message(FATAL_ERROR "Expected at least one installed QML type description file")
endif()
