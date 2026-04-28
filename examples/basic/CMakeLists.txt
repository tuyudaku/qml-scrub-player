qt_add_executable(qmlscrubplayer_basic
    main.cpp
)

qt_add_qml_module(qmlscrubplayer_basic
    URI QMLScrubPlayer.Basic
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(qmlscrubplayer_basic
    PRIVATE
        QMLScrubPlayer
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::Multimedia
)
