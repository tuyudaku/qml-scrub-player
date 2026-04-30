qt_add_executable(qmlscrubplayer_tests
    tst_qmlscrubplayer.cpp
)

target_link_libraries(qmlscrubplayer_tests
    PRIVATE
        QMLScrubPlayer
        Qt6::Core
        Qt6::Test
        Qt6::Multimedia
)

add_test(NAME qmlscrubplayer_tests COMMAND qmlscrubplayer_tests)
