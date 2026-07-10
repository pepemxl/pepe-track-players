import QtQuick
import QtQuick.Controls

// Collapsible left panel of the Video view: frame markers (match events,
// commercial ranges) and offline video operations (20 fps preprocess,
// 1-min 10 fps chunks, per-chunk tracking CSVs).
Rectangle {
    id: panel
    property bool collapsed: false
    readonly property var match: App.match

    width: collapsed ? 32 : 252
    color: Theme.bgDark
    Behavior on width { NumberAnimation { duration: 140; easing.type: Easing.OutQuad } }

    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }

    // Collapse / expand toggle
    Rectangle {
        id: toggleBtn
        x: panel.collapsed ? 5 : panel.width - 27
        y: 14
        width: 22; height: 22; radius: 6
        color: toggleMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
        z: 2
        Text {
            anchors.centerIn: parent
            text: panel.collapsed ? "»" : "«"
            color: Theme.textBright
            font.pixelSize: 13
        }
        MouseArea {
            id: toggleMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: panel.collapsed = !panel.collapsed
        }
    }

    // Vertical hint when collapsed
    Text {
        visible: panel.collapsed
        anchors.horizontalCenter: parent.horizontalCenter
        y: 52
        rotation: 90
        transformOrigin: Item.TopLeft
        text: "VIDEO OPS"
        color: Theme.textFaint
        font { family: Theme.fontUi; pixelSize: 10; weight: Font.Bold; letterSpacing: 1 }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 14
        anchors.topMargin: 44
        contentHeight: content.height
        clip: true
        visible: !panel.collapsed

        Column {
            id: content
            width: parent.width
            spacing: 14

            // ---- match registry chip ----
            Rectangle {
                width: parent.width
                height: 44
                radius: 8
                color: Theme.surface
                border.color: Theme.border
                border.width: 1
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    spacing: 2
                    Text {
                        text: match.registered ? "MATCH #" + match.matchId : "NO MATCH"
                        color: Theme.text
                        font { family: Theme.fontMono; pixelSize: 12; weight: Font.Bold }
                    }
                    Text {
                        text: match.registered
                            ? match.status + (match.chunkCount > 0 ? " · " + match.chunkCount + " chunks" : "")
                            : "open a video to register"
                        color: match.status === "tracked" ? Theme.greenBright : Theme.textDim
                        font { family: Theme.fontMono; pixelSize: 10 }
                    }
                }
            }

            // ---- frame markers ----
            Text {
                text: "FRAME MARKERS · at frame " + App.currentFrame
                color: Theme.textDim
                font { family: Theme.fontUi; pixelSize: 10; weight: Font.Bold; letterSpacing: 0.5 }
            }

            Grid {
                columns: 2
                columnSpacing: 6
                rowSpacing: 6
                width: parent.width

                Repeater {
                    model: Theme.markerTypes
                    delegate: Rectangle {
                        required property var modelData
                        width: (content.width - 6) / 2
                        height: 28
                        radius: 6
                        color: mkMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
                        opacity: App.videoLoaded && match.registered ? 1 : 0.4
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            spacing: 6
                            Rectangle {
                                width: 7; height: 7; radius: 2
                                color: modelData.tint
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: modelData.label
                                color: Theme.textBright
                                font { family: Theme.fontUi; pixelSize: 11; weight: Font.DemiBold }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        MouseArea {
                            id: mkMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: App.videoLoaded && match.registered
                            cursorShape: Qt.PointingHandCursor
                            onClicked: match.addMarker(modelData.type, App.currentFrame)
                        }
                    }
                }
            }

            // Marker list
            Column {
                width: parent.width
                spacing: 4

                Repeater {
                    model: match.markers
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        readonly property var info: Theme.markerInfo(modelData.type)
                        width: content.width
                        height: 26
                        radius: 5
                        color: rowMouse.containsMouse ? Theme.surface : "transparent"

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            spacing: 7
                            Rectangle {
                                width: 7; height: 7; radius: 2
                                color: info.tint
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: info.label
                                color: Theme.text
                                font { family: Theme.fontUi; pixelSize: 11 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "f " + modelData.frame
                                color: Theme.textDim
                                font { family: Theme.fontMono; pixelSize: 10 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { App.pause(); App.seekFrame(modelData.frame) }
                        }
                        Text {
                            text: "×"
                            color: mDelMouse.containsMouse ? Theme.red : Theme.textDim
                            font.pixelSize: 13
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                id: mDelMouse
                                anchors.fill: parent
                                anchors.margins: -5
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: match.removeMarker(index)
                            }
                        }
                    }
                }

                Text {
                    visible: match.markers.length === 0
                    text: "No markers yet"
                    color: Theme.textFaint
                    font { family: Theme.fontUi; pixelSize: 11 }
                }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.border }

            // ---- operations ----
            Text {
                text: "OPERATIONS"
                color: Theme.textDim
                font { family: Theme.fontUi; pixelSize: 10; weight: Font.Bold; letterSpacing: 0.5 }
            }

            OpButton {
                width: parent.width
                label: "Preprocess → 20 fps"
                done: ["preprocessed", "chunked", "tracked"].indexOf(match.status) >= 0
                onTriggered: match.preprocess()
            }
            OpButton {
                width: parent.width
                label: "Create chunks · 1 min @ 10 fps"
                done: ["chunked", "tracked"].indexOf(match.status) >= 0
                onTriggered: match.createChunks()
            }
            OpButton {
                width: parent.width
                label: "Track chunks → CSV"
                done: match.status === "tracked"
                enabled2: match.chunkCount > 0
                onTriggered: match.trackChunks()
            }

            // Progress / error
            Column {
                width: parent.width
                spacing: 6
                visible: match.opRunning || match.lastError.length > 0

                Text {
                    width: parent.width
                    text: match.lastError.length > 0 ? match.lastError : match.opLabel
                    color: match.lastError.length > 0 ? Theme.red : Theme.greenBright
                    font { family: Theme.fontMono; pixelSize: 10 }
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    visible: match.opRunning
                    width: parent.width
                    height: 6
                    radius: 3
                    color: "#262b33"
                    Rectangle {
                        width: parent.width * match.opProgress
                        height: parent.height
                        radius: 3
                        color: Theme.green
                    }
                }
                Rectangle {
                    visible: match.opRunning
                    width: 60; height: 24; radius: 6
                    color: cancelMouse.containsMouse ? Theme.red : Theme.surfaceHi
                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        color: Theme.textBright
                        font { family: Theme.fontUi; pixelSize: 11; weight: Font.DemiBold }
                    }
                    MouseArea {
                        id: cancelMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: match.cancelOp()
                    }
                }
            }
        }
    }

    // Operation button with done-check state.
    component OpButton: Rectangle {
        id: opBtn
        property string label: ""
        property bool done: false
        property bool enabled2: true
        signal triggered()

        readonly property bool usable: App.videoLoaded && panel.match.registered
                                       && !panel.match.opRunning && enabled2
        height: 32
        radius: 7
        color: opMouse.containsMouse && usable ? Theme.borderHi : Theme.surfaceHi
        border.color: done ? "#5930d980" : Theme.border2
        border.width: 1
        opacity: usable ? 1 : 0.45

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 10
            spacing: 7
            Text {
                text: opBtn.done ? "✓" : "▸"
                color: opBtn.done ? Theme.green : Theme.textDim
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: opBtn.label
                color: Theme.textBright
                font { family: Theme.fontUi; pixelSize: 11; weight: Font.DemiBold }
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        MouseArea {
            id: opMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: opBtn.usable
            cursorShape: Qt.PointingHandCursor
            onClicked: opBtn.triggered()
        }
    }
}
