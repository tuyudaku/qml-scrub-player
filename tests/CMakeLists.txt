qt_add_executable(qmlscrubplayer_tests
    tst_qmlscrubplayer.cpp
)

target_link_libraries(qmlscrubplayer_tests
    PRIVATE
        QMLScrubPlayer
        Qt6::Core
        Qt6::Qml
        Qt6::Test
        Qt6::Multimedia
        PkgConfig::FFMPEG
)

target_compile_definitions(qmlscrubplayer_tests
    PRIVATE
        QMLSCRUBPLAYER_BUILD_DIR="${CMAKE_BINARY_DIR}"
)

add_test(NAME qmlscrubplayer_tests COMMAND qmlscrubplayer_tests)

set_tests_properties(qmlscrubplayer_tests
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)

add_test(
    NAME qmlscrubplayer_install_tree
    COMMAND
        "${CMAKE_COMMAND}"
        --install "${CMAKE_BINARY_DIR}"
        --prefix "${CMAKE_BINARY_DIR}/test-install"
)

add_test(
    NAME qmlscrubplayer_find_package
    COMMAND
        "${CMAKE_COMMAND}"
        -S "${CMAKE_CURRENT_SOURCE_DIR}/consumer"
        -B "${CMAKE_BINARY_DIR}/tests/consumer-build"
        "-DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/test-install;${CMAKE_PREFIX_PATH}"
        -DCMAKE_BUILD_TYPE=$<CONFIG>
)

set_tests_properties(qmlscrubplayer_find_package
    PROPERTIES DEPENDS qmlscrubplayer_install_tree
)

add_test(
    NAME qmlscrubplayer_verify_install_tree
    COMMAND
        "${CMAKE_COMMAND}"
        "-DQMLSCRUBPLAYER_TEST_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/test-install"
        "-DCMAKE_INSTALL_INCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}"
        "-DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
        "-DCMAKE_INSTALL_DATAROOTDIR=${CMAKE_INSTALL_DATAROOTDIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_install_tree.cmake"
)

set_tests_properties(qmlscrubplayer_verify_install_tree
    PROPERTIES DEPENDS qmlscrubplayer_install_tree
)
