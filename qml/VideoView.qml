import QtQuick
import QtQuick.Controls

// Section 1 — central player with tagging. Click the video to place a tag:
// a dropdown asks which player; the tag stores frame + video/pitch coords.
Item {
    id: view

    Row {
        anchors.fill: parent

        // ---- collapsible video-ops panel ----
        VideoOpsPanel {
            id: opsPanel
            height: parent.height
        }

        // ---- left column: video + transport + quality strip ----
        Item {
            width: parent.width - rightRail.width - opsPanel.width
            height: parent.height

            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                VideoSurface {
                    id: surface
                    width: parent.width
                    height: parent.height - transport.height - qualityStrip.height - 28

                    // Badge top-right: placeholder vs. current file
                    Rectangle {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 12
                        width: stateText.implicitWidth + 18; height: 24; radius: 6
                        color: Theme.overlayBg
                        Text {
                            id: stateText
                            anchors.centerIn: parent
                            text: App.videoLoaded
                                ? "FRAME " + App.currentFrame + " / " + App.totalFrames
                                : "VIDEO PLACEHOLDER"
                            color: Theme.greenBright
                            font { family: Theme.fontMono; pixelSize: 11 }
                        }
                    }

                    // Tag markers near the current frame (±1s window).
                    // Roles are read through `model.*` because the tag's
                    // x/y roles would shadow the Item's own geometry.
                    Repeater {
                        model: App.tags
                        delegate: Rectangle {
                            required property var model
                            visible: App.videoLoaded
                                     && Math.abs(model.frame - App.currentFrame) <= App.fps
                            x: surface.fromVideoX(model.x) - 9
                            y: surface.fromVideoY(model.y) - 9
                            width: 18; height: 18; radius: 9
                            color: "transparent"
                            border.color: Theme.teamColor(model.team)
                            border.width: 2

                            Rectangle {
                                anchors.bottom: parent.top
                                anchors.bottomMargin: 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: numText.implicitWidth + 10; height: 16; radius: 3
                                color: Theme.teamColor(model.team)
                                Text {
                                    id: numText
                                    anchors.centerIn: parent
                                    text: "P" + model.playerNumber
                                    color: "white"
                                    font { family: Theme.fontMono; pixelSize: 10; weight: Font.Bold }
                                }
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: App.videoLoaded
                        cursorShape: enabled ? Qt.CrossCursor : Qt.ArrowCursor
                        onClicked: (mouse) => {
                            if (!surface.insidePainted(mouse.x, mouse.y))
                                return
                            App.pause()
                            tagDropdown.pendingVx = surface.toVideoX(mouse.x)
                            tagDropdown.pendingVy = surface.toVideoY(mouse.y)
                            tagDropdown.x = Math.min(mouse.x, surface.width - tagDropdown.width - 8)
                            tagDropdown.y = Math.min(mouse.y, surface.height - tagDropdown.height - 8)
                            tagDropdown.open()
                        }
                    }

                    PlayerDropdown {
                        id: tagDropdown
                        property double pendingVx: 0
                        property double pendingVy: 0
                        title: "TAG PLAYER — FRAME " + App.currentFrame
                        onPlayerPicked: (team, rosterRow) =>
                            App.addTag(pendingVx, pendingVy, team, rosterRow)
                    }
                }

                // ---- transport ----
                Row {
                    id: transport
                    width: parent.width
                    height: 34
                    spacing: 14

                    Rectangle {
                        width: 34; height: 34; radius: 8
                        color: playMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
                        // Pause bars
                        Row {
                            visible: App.playing
                            anchors.centerIn: parent
                            spacing: 3
                            Rectangle { width: 3; height: 12; color: Theme.textBright }
                            Rectangle { width: 3; height: 12; color: Theme.textBright }
                        }
                        // Play triangle
                        Canvas {
                            visible: !App.playing
                            anchors.centerIn: parent
                            width: 11; height: 12
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = Theme.textBright
                                ctx.beginPath()
                                ctx.moveTo(2, 0); ctx.lineTo(11, 6); ctx.lineTo(2, 12)
                                ctx.closePath(); ctx.fill()
                            }
                        }
                        MouseArea {
                            id: playMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: App.togglePlay()
                        }
                    }

                    Text {
                        text: App.timecode(App.positionSec)
                        color: Theme.textMid
                        font { family: Theme.fontMono; pixelSize: 12 }
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    // Scrubber
                    Item {
                        width: parent.width - 34 - 14 * 3
                               - 100 - durText.implicitWidth
                        height: 34
                        anchors.verticalCenter: parent.verticalCenter

                        Rectangle {
                            id: track
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 6
                            radius: 3
                            color: "#262b33"

                            Rectangle {
                                width: App.totalFrames > 1
                                    ? track.width * App.currentFrame / (App.totalFrames - 1) : 0
                                height: parent.height
                                radius: 3
                                color: Theme.green
                            }

                            // Frame-marker ticks
                            Repeater {
                                model: App.match.markers
                                delegate: Rectangle {
                                    required property var modelData
                                    x: (App.totalFrames > 1
                                        ? track.width * modelData.frame / (App.totalFrames - 1) : 0) - 1
                                    y: -4
                                    width: 2; height: 14
                                    color: Theme.markerInfo(modelData.type).tint
                                }
                            }

                            Rectangle {
                                x: (App.totalFrames > 1
                                    ? track.width * App.currentFrame / (App.totalFrames - 1) : 0) - 6
                                anchors.verticalCenter: parent.verticalCenter
                                width: 12; height: 12; radius: 6
                                color: Theme.text
                                border.color: "#4d30d980"
                                border.width: 3
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: App.videoLoaded
                            onPressed: (mouse) => App.seekFrac(mouse.x / width)
                            onPositionChanged: (mouse) => {
                                if (pressed) App.seekFrac(mouse.x / width)
                            }
                        }
                    }

                    Text {
                        id: durText
                        text: App.timecode(App.durationSec)
                        color: Theme.textFaint
                        font { family: Theme.fontMono; pixelSize: 12 }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // ---- frame tracking quality strip ----
                Column {
                    id: qualityStrip
                    width: parent.width
                    spacing: 6

                    Text {
                        text: "FRAME TRACKING QUALITY"
                        color: Theme.textDim
                        font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                    }
                    Row {
                        width: parent.width
                        height: 22
                        spacing: 2
                        Repeater {
                            model: App.tracking.frameChips
                            delegate: Rectangle {
                                required property var modelData
                                width: (parent.width - 89 * 2) / 90
                                height: 22
                                radius: 2
                                color: modelData === 1 ? Theme.green
                                     : modelData === 2 ? Theme.yellow
                                     : modelData === 3 ? Theme.red
                                     : "#262b33"
                                opacity: modelData === 1 ? 0.7 : 1
                            }
                        }
                    }
                }
            }
        }

        // ---- right rail: tag list ----
        Rectangle {
            id: rightRail
            width: 280
            height: parent.height
            color: "transparent"

            Rectangle { width: 1; height: parent.height; color: Theme.border }

            Column {
                anchors.fill: parent
                anchors.margins: 16
                anchors.topMargin: 20
                spacing: 12

                Text {
                    text: "TAGS · " + App.tags.count
                    color: Theme.textDim
                    font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                }

                ListView {
                    width: parent.width
                    height: parent.height - 40
                    clip: true
                    spacing: 8
                    model: App.tags

                    delegate: Rectangle {
                        required property int index
                        required property int frame
                        required property string timecode
                        required property int playerNumber
                        required property string playerName
                        required property int team
                        required property bool hasPitch
                        required property double pitchX
                        required property double pitchY

                        width: ListView.view.width
                        height: 52
                        radius: 8
                        color: Theme.surface
                        border.width: 1
                        border.color: Math.abs(frame - App.currentFrame) <= App.fps
                            ? Theme.greenDim : Theme.border

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            spacing: 10

                            Rectangle {
                                width: 26; height: 26; radius: 6
                                color: Theme.teamColor(team)
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    text: playerNumber
                                    color: "white"
                                    font { family: Theme.fontMono; pixelSize: 11; weight: Font.Bold }
                                    anchors.centerIn: parent
                                }
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text {
                                    text: playerName
                                    color: Theme.text
                                    font { family: Theme.fontUi; pixelSize: 13; weight: Font.DemiBold }
                                }
                                Text {
                                    text: "frame " + frame + " · " + (hasPitch
                                        ? "pitch (" + pitchX.toFixed(1) + ", " + pitchY.toFixed(1) + ")m"
                                        : timecode)
                                    color: Theme.textDim
                                    font { family: Theme.fontMono; pixelSize: 11 }
                                }
                            }
                        }

                        // jump to tag frame / delete
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: App.seekFrame(frame)
                        }
                        Text {
                            text: "×"
                            color: delMouse.containsMouse ? Theme.red : Theme.textDim
                            font.pixelSize: 15
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                id: delMouse
                                anchors.fill: parent
                                anchors.margins: -6
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.tags.removeTag(index)
                            }
                        }
                    }

                    Text {
                        visible: App.tags.count === 0
                        anchors.centerIn: parent
                        width: parent.width - 20
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: "No tags yet.\nClick a player on the video to tag them."
                        color: Theme.textFaint
                        font { family: Theme.fontUi; pixelSize: 12 }
                        lineHeight: 1.4
                    }
                }
            }
        }
    }
}
