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
                    enabled: player.canPlay || player.canPause
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
                    enabled: player.canSeek
                    live: true
                    onMoved: player.previewSeek(value)
                    onPressedChanged: {
                        if (!pressed) {
                            player.endPreviewSeek(value)
                        }
                    }
                }

                Button {
                    text: "-1f"
                    enabled: player.canSeek
                    onClicked: player.stepBackward()
                }

                Button {
                    text: "+1f"
                    enabled: player.canSeek
                    onClicked: player.stepForward()
                }

                Button {
                    text: "Loop"
                    enabled: player.canSeek
                    checkable: true
                    onToggled: {
                        if (checked) {
                            player.setLoopRange(player.position, Math.min(player.duration, player.position + 2000))
                            player.loops = -1
                        } else {
                            player.clearLoopRange()
                            player.loops = 1
                        }
                    }
                }

                Button {
                    text: player.muted ? "Muted" : "Sound"
                    enabled: player.hasAudio
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
                    text: player.errorString.length > 0
                        ? player.errorString
                        : player.currentFrame + "/" + Math.max(player.frameCount - 1, 0)
                          + "  " + player.timecode
                          + "  cache " + player.previewCacheSize + "/" + player.previewCacheLimit
                          + "  " + player.videoCodecName
                          + (player.hasAudio ? "  " + player.audioCodecName : "")
                    width: 220
                    elide: Text.ElideRight
                }
            }
        }
    }
}
