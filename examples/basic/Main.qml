import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtMultimedia
import QMLScrubPlayer

ApplicationWindow {
    id: window
    width: 960
    height: 600
    visible: true
    title: "QMLScrubPlayer Basic"

    QmlScrubPlayer {
        id: player
        videoSink: videoOutput.videoSink
        volume: volumeSlider.value
    }

    FileDialog {
        id: fileDialog
        title: "Open video"
        nameFilters: ["Video files (*.mp4 *.mov *.m4v *.mkv *.webm)", "All files (*)"]
        onAccepted: {
            player.source = selectedFile
            player.play()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#111318"

        VideoOutput {
            id: videoOutput
            anchors.fill: parent
            anchors.bottomMargin: controls.height
            fillMode: VideoOutput.PreserveAspectFit
        }

        Text {
            anchors.centerIn: videoOutput
            visible: player.source.toString().length === 0
            color: "#d7dbe5"
            text: "Open a video"
            font.pixelSize: 28
        }

        Rectangle {
            id: controls
            height: 92
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            color: "#20242c"

            Row {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                Button {
                    text: "Open"
                    onClicked: fileDialog.open()
                }

                Button {
                    text: player.playing ? "Pause" : "Play"
                    enabled: player.source.toString().length > 0
                    onClicked: player.playing ? player.pause() : player.play()
                }

                Button {
                    text: "Stop"
                    enabled: player.source.toString().length > 0
                    onClicked: player.stop()
                }

                Slider {
                    id: positionSlider
                    width: 340
                    from: 0
                    to: Math.max(player.duration, 1)
                    value: player.position
                    enabled: player.duration > 0
                    live: true
                    onMoved: player.previewSeek(value)
                    onPressedChanged: {
                        if (!pressed) {
                            player.endPreviewSeek(value)
                        }
                    }
                }

                Button {
                    text: player.muted ? "Muted" : "Sound"
                    onClicked: player.muted = !player.muted
                }

                Slider {
                    id: volumeSlider
                    width: 110
                    from: 0
                    to: 1
                    value: 0.8
                }

                Label {
                    color: "#d7dbe5"
                    text: player.errorString
                    visible: player.errorString.length > 0
                    width: 180
                    elide: Text.ElideRight
                }
            }
        }
    }
}
