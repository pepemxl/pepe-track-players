import QtQuick
import QtQuick.Controls

// Section 4 — offline inference: Start/Stop, progress, stats, per-frame
// status strip, and the resulting tracks table.
Flickable {
    id: view
    contentHeight: content.height + 56
    clip: true

    property string selectedTrack: ""
    readonly property var tracking: App.tracking

    Column {
        id: content
        x: 32
        y: 28
        width: parent.width - 64
        spacing: 22

        // ---- control bar ----
        Rectangle {
            width: parent.width
            height: 66
            radius: 10
            color: Theme.surface
            border.color: Theme.border
            border.width: 1

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 20
                spacing: 16

                Text {
                    text: "Model"
                    color: Theme.text
                    font { family: Theme.fontUi; pixelSize: 13; weight: Font.DemiBold }
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    width: modelText.implicitWidth + 28; height: 34; radius: 7
                    color: Theme.surfaceHi
                    border.color: Theme.border2
                    border.width: 1
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        id: modelText
                        text: tracking.modelName
                        color: Theme.text
                        font { family: Theme.fontMono; pixelSize: 12.5 }
                        anchors.centerIn: parent
                    }
                }
                Rectangle { width: 1; height: 22; color: Theme.border; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: "Range"
                    color: Theme.text
                    font { family: Theme.fontUi; pixelSize: 13; weight: Font.DemiBold }
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    width: rangeText.implicitWidth + 28; height: 34; radius: 7
                    color: Theme.surfaceHi
                    border.color: Theme.border2
                    border.width: 1
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        id: rangeText
                        text: App.videoLoaded ? "frame 0 — " + App.totalFrames : "no video"
                        color: Theme.text
                        font { family: Theme.fontMono; pixelSize: 12.5 }
                        anchors.centerIn: parent
                    }
                }
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 20
                spacing: 10

                Text {
                    text: {
                        const pct = (tracking.progress * 100).toFixed(0)
                        if (tracking.completed) return "100% · complete"
                        if (tracking.framesProcessed > 0)
                            return pct + "% · frame "
                                + tracking.framesProcessed.toLocaleString(Qt.locale(), 'f', 0)
                                + " / " + App.totalFrames.toLocaleString(Qt.locale(), 'f', 0)
                        return "idle"
                    }
                    color: tracking.completed ? Theme.green
                         : tracking.running ? Theme.greenBright : Theme.textFaint
                    font { family: Theme.fontMono; pixelSize: 12.5 }
                    anchors.verticalCenter: parent.verticalCenter
                }

                Rectangle {
                    width: 180; height: 8; radius: 4
                    color: "#262b33"
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        width: parent.width * tracking.progress
                        height: parent.height
                        radius: 4
                        color: Theme.green
                    }
                }

                Rectangle {
                    width: Math.max(70, runText.implicitWidth + 36); height: 36; radius: 8
                    color: !App.videoLoaded ? Theme.surfaceHi
                         : tracking.running ? Theme.red : Theme.green
                    opacity: App.videoLoaded ? 1 : 0.5
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        id: runText
                        text: tracking.running ? "Stop" : (tracking.completed ? "Rerun" : "Start")
                        color: tracking.running ? "white" : "#10231a"
                        font { family: Theme.fontUi; pixelSize: 12.5; weight: Font.Bold }
                        anchors.centerIn: parent
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: App.videoLoaded
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: tracking.toggleRun()
                    }
                }
            }
        }

        // ---- stat cards ----
        Row {
            width: parent.width
            spacing: 16

            StatCard {
                width: (parent.width - 3 * 16) / 4
                label: "Frames processed"
                value: tracking.framesProcessed.toLocaleString(Qt.locale(), 'f', 0)
            }
            StatCard {
                width: (parent.width - 3 * 16) / 4
                label: "Players tracked"
                value: tracking.playersTracked
                valueColor: Theme.greenBright
            }
            StatCard {
                width: (parent.width - 3 * 16) / 4
                label: "Avg. confidence"
                value: tracking.framesProcessed > 0 ? tracking.avgConfidence.toFixed(2) : "—"
                valueColor: Theme.greenBright
            }
            StatCard {
                width: (parent.width - 3 * 16) / 4
                label: "Frames w/ lost track"
                value: tracking.lostFrames.toLocaleString(Qt.locale(), 'f', 0)
                valueColor: Theme.yellow
            }
        }

        // ---- per-frame status strip ----
        Column {
            width: parent.width
            spacing: 8

            Text {
                text: "PER-FRAME TRACKING STATUS"
                color: Theme.textDim
                font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
            }

            Row {
                width: parent.width
                height: 34
                spacing: 1
                Repeater {
                    model: tracking.frameChips
                    delegate: Rectangle {
                        required property var modelData
                        width: (parent.width - 89) / 90
                        height: 34
                        color: modelData === 1 ? Theme.green
                             : modelData === 2 ? Theme.yellow
                             : modelData === 3 ? Theme.red
                             : "#2c313a"
                    }
                }
            }

            Row {
                spacing: 18
                Repeater {
                    model: [
                        { c: Theme.green,  t: "Good tracking" },
                        { c: Theme.yellow, t: "Low confidence" },
                        { c: Theme.red,    t: "Track lost" },
                        { c: "#2c313a",    t: "Not processed" },
                    ]
                    delegate: Row {
                        required property var modelData
                        spacing: 6
                        Rectangle {
                            width: 9; height: 9; radius: 2
                            color: modelData.c
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: modelData.t
                            color: Theme.textMuted
                            font { family: Theme.fontUi; pixelSize: 11 }
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }

        // ---- tracks table ----
        Column {
            width: parent.width
            spacing: 8

            Text {
                text: "TRACKS · click a row to inspect"
                color: Theme.textDim
                font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
            }

            Rectangle {
                width: parent.width
                height: tracksHeader.height + tracksList.contentHeight
                radius: 10
                color: "transparent"
                border.color: Theme.border
                border.width: 1
                clip: true

                Column {
                    anchors.fill: parent

                    Rectangle {
                        id: tracksHeader
                        width: parent.width
                        height: 32
                        color: Theme.surface2
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            Repeater {
                                model: [
                                    { w: 70,  t: "ID" },
                                    { w: tracksHeader.width - 70 - 130 - 130 - 110 - 16, t: "PLAYER" },
                                    { w: 130, t: "FRAMES TRACKED" },
                                    { w: 130, t: "AVG CONF." },
                                    { w: 110, t: "STATUS" },
                                ]
                                delegate: Text {
                                    required property var modelData
                                    width: modelData.w
                                    text: modelData.t
                                    color: Theme.textDim
                                    font { family: Theme.fontUi; pixelSize: 10.5; weight: Font.Bold; letterSpacing: 0.4 }
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }

                    ListView {
                        id: tracksList
                        width: parent.width
                        height: contentHeight
                        interactive: false
                        model: App.tracksModel

                        delegate: Rectangle {
                            required property string trackId
                            required property string name
                            required property int framesTracked
                            required property string avgConf
                            required property string status
                            required property int statusKind

                            width: tracksList.width
                            height: 42
                            color: view.selectedTrack === trackId ? "#141d3a2b" : "transparent"

                            Rectangle { width: parent.width; height: 1; color: "#242830" }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 16

                                Text {
                                    width: 70
                                    text: trackId
                                    color: Theme.textDim
                                    font { family: Theme.fontMono; pixelSize: 13 }
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Row {
                                    width: parent.width - 70 - 130 - 130 - 110 - 16
                                    spacing: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    Rectangle {
                                        width: 20; height: 20; radius: 5
                                        color: Theme.surfaceHi
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: name.length > 0 ? name : "unassigned"
                                        color: name.length > 0 ? Theme.text : Theme.textFaint
                                        font { family: Theme.fontUi; pixelSize: 13 }
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                                Text {
                                    width: 130
                                    text: framesTracked.toLocaleString(Qt.locale(), 'f', 0)
                                    color: Theme.textMuted
                                    font { family: Theme.fontMono; pixelSize: 13 }
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    width: 130
                                    text: avgConf
                                    color: Theme.textMuted
                                    font { family: Theme.fontMono; pixelSize: 13 }
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Rectangle {
                                    width: statusRow.implicitWidth + 18
                                    height: 22
                                    radius: 11
                                    color: statusKind === 0 ? "#401d3a2b"
                                         : statusKind === 1 ? "#40382d12" : "#40381512"
                                    anchors.verticalCenter: parent.verticalCenter
                                    Row {
                                        id: statusRow
                                        anchors.centerIn: parent
                                        spacing: 6
                                        Rectangle {
                                            width: 6; height: 6; radius: 3
                                            color: statusKind === 0 ? Theme.green
                                                 : statusKind === 1 ? Theme.yellow : Theme.red
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Text {
                                            text: status
                                            color: statusKind === 0 ? Theme.greenBright
                                                 : statusKind === 1 ? Theme.yellow : "#f0a49c"
                                            font { family: Theme.fontUi; pixelSize: 11; weight: Font.DemiBold }
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: view.selectedTrack =
                                    view.selectedTrack === trackId ? "" : trackId
                            }
                        }
                    }

                    Rectangle {
                        visible: tracksList.count === 0
                        width: parent.width
                        height: 60
                        color: "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: App.videoLoaded
                                ? "No tracks yet — press Start to run inference."
                                : "Open a video, then run inference to build tracks."
                            color: Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 12 }
                        }
                    }
                }
            }
        }
    }
}
