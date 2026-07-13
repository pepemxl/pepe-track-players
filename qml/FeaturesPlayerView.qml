import QtQuick
import QtQuick.Controls

// Section 7 — Features Player. Capture reference appearance samples for each
// role (Team A/B player, central & assistant referee, both goalkeepers) by
// dragging a box on any frame. Samples are persisted per video and help
// identify players / referees / goalkeepers and seed the id inference.
Item {
    id: view

    // Role currently being sampled (-1 = none). Matches PlayerSamples::Role.
    property int captureRole: -1
    property bool dragging: false
    property real dx0: 0
    property real dy0: 0
    property real dx1: 0
    property real dy1: 0

    // Reactive copy of the sample list (refreshes on PlayerSamples::changed).
    property var allSamples: App.playerSamples.samples

    readonly property var roles: [
        { i: 0, name: "Team A player",     color: Theme.homeColor },
        { i: 1, name: "Team B player",     color: Theme.awayColor },
        { i: 2, name: "Central referee",   color: "#f5d020" },
        { i: 3, name: "Assistant referee", color: "#c98a2a" },
        { i: 4, name: "Goalkeeper A",      color: "#6b77c9" },
        { i: 5, name: "Goalkeeper B",      color: "#c9976b" },
    ]
    // Line-up graphic roles (PlayerSamples::TeamALineup / TeamBLineup).
    readonly property var lineupRoles: [
        { i: 6, name: "Team A line-up", color: Theme.homeColor },
        { i: 7, name: "Team B line-up", color: Theme.awayColor },
    ]
    readonly property var allRoles: roles.concat(lineupRoles)
    function roleColor(i) { const r = allRoles.find(x => x.i === i); return r ? r.color : Theme.textDim }
    function roleName(i) { const r = allRoles.find(x => x.i === i); return r ? r.name : "" }

    // Opening the tab pauses playback so the sampling frame is stable.
    onVisibleChanged: if (visible && App.playing) App.pause()

    Row {
        anchors.fill: parent

        // ---- left: frame + capture + transport ----
        Item {
            width: parent.width - rightPanel.width
            height: parent.height

            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                VideoSurface {
                    id: surface
                    width: parent.width
                    height: parent.height - transport.height - 14

                    // Rubber-band while dragging a sample box.
                    Rectangle {
                        visible: view.dragging
                        x: Math.min(view.dx0, view.dx1)
                        y: Math.min(view.dy0, view.dy1)
                        width: Math.abs(view.dx1 - view.dx0)
                        height: Math.abs(view.dy1 - view.dy0)
                        color: Qt.rgba(1, 1, 1, 0.12)
                        border.color: view.captureRole >= 0 ? view.roleColor(view.captureRole) : "white"
                        border.width: 2
                    }

                    // Capture drag (only when a role is selected).
                    MouseArea {
                        anchors.fill: parent
                        enabled: view.captureRole >= 0 && App.videoLoaded
                        visible: enabled
                        z: 20
                        cursorShape: Qt.CrossCursor
                        onPressed: (mouse) => {
                            view.dx0 = mouse.x; view.dy0 = mouse.y
                            view.dx1 = mouse.x; view.dy1 = mouse.y
                            view.dragging = true
                        }
                        onPositionChanged: (mouse) => {
                            if (view.dragging) { view.dx1 = mouse.x; view.dy1 = mouse.y }
                        }
                        onReleased: {
                            view.dragging = false
                            const x0 = surface.toVideoX(Math.min(view.dx0, view.dx1))
                            const y0 = surface.toVideoY(Math.min(view.dy0, view.dy1))
                            const x1 = surface.toVideoX(Math.max(view.dx0, view.dx1))
                            const y1 = surface.toVideoY(Math.max(view.dy0, view.dy1))
                            App.capturePlayerSample(view.captureRole, x0, y0, x1 - x0, y1 - y0)
                        }
                    }

                    // Capture hint.
                    Rectangle {
                        visible: view.captureRole >= 0
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 12
                        width: hintRow.implicitWidth + 22
                        height: 30
                        radius: 8
                        color: Theme.overlayBg
                        border.color: view.captureRole >= 0 ? view.roleColor(view.captureRole) : Theme.border
                        border.width: 1
                        Row {
                            id: hintRow
                            anchors.centerIn: parent
                            spacing: 8
                            Rectangle {
                                width: 10; height: 10; radius: 2
                                color: view.captureRole >= 0 ? view.roleColor(view.captureRole) : Theme.textDim
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "Drag a box around a " + view.roleName(view.captureRole)
                                color: Theme.textBright
                                font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "done"
                                color: Theme.textMuted
                                font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold }
                                anchors.verticalCenter: parent.verticalCenter
                                MouseArea {
                                    anchors.fill: parent; anchors.margins: -6
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: view.captureRole = -1
                                }
                            }
                        }
                    }
                }

                FrameTransport { id: transport; width: parent.width }
            }
        }

        // ---- right: role list ----
        Rectangle {
            id: rightPanel
            width: 360
            height: parent.height
            color: Theme.bgDark
            border.color: Theme.border
            border.width: 1

            Flickable {
                anchors.fill: parent
                anchors.margins: 20
                contentHeight: panel.height + 20
                clip: true

                Column {
                    id: panel
                    width: parent.width
                    spacing: 16

                    Text {
                        text: "FEATURES PLAYER"
                        color: Theme.text
                        font { family: Theme.fontUi; pixelSize: 15; weight: Font.Bold; letterSpacing: 0.5 }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Pick a role, then drag a box on the video to capture a "
                            + "reference sample. Samples are saved per video and help "
                            + "identify players, referees and goalkeepers, and seed the "
                            + "player-id inference."
                        color: Theme.textDim
                        font { family: Theme.fontUi; pixelSize: 12 }
                    }

                    Text {
                        text: "APPEARANCE SAMPLES"
                        color: Theme.text
                        font { family: Theme.fontUi; pixelSize: 13; weight: Font.Bold; letterSpacing: 0.5 }
                    }

                    Repeater {
                        model: view.roles
                        delegate: Rectangle {
                            required property var modelData
                            readonly property int roleIndex: modelData.i
                            readonly property var mine:
                                view.allSamples.filter(s => s.role === roleIndex)
                            readonly property bool active: view.captureRole === roleIndex

                            width: panel.width
                            implicitHeight: rows.height + 20
                            radius: 10
                            color: Theme.surface
                            border.color: active ? modelData.color : Theme.border
                            border.width: active ? 2 : 1

                            Column {
                                id: rows
                                x: 14; y: 10
                                width: parent.width - 28
                                spacing: 10

                                Row {
                                    width: parent.width
                                    spacing: 10
                                    Rectangle {
                                        width: 14; height: 14; radius: 4
                                        color: modelData.color
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Column {
                                        width: parent.width - 14 - 10 - sampleBtn.width - 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text {
                                            text: modelData.name
                                            color: Theme.text
                                            font { family: Theme.fontUi; pixelSize: 13; weight: Font.DemiBold }
                                        }
                                        Text {
                                            text: mine.length + " sample(s)"
                                            color: Theme.textDim
                                            font { family: Theme.fontUi; pixelSize: 11 }
                                        }
                                    }
                                    Rectangle {
                                        id: sampleBtn
                                        width: 92; height: 32; radius: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: active ? modelData.color : Theme.surfaceHi
                                        border.color: active ? modelData.color : Theme.border2
                                        border.width: 1
                                        Text {
                                            anchors.centerIn: parent
                                            text: active ? "Sampling…" : "Sample"
                                            color: active ? "#10231a" : Theme.text
                                            font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: App.videoLoaded
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: view.captureRole = active ? -1 : roleIndex
                                        }
                                    }
                                }

                                // Thumbnails for this role.
                                Flow {
                                    width: parent.width
                                    spacing: 6
                                    visible: mine.length > 0
                                    Repeater {
                                        model: mine
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: 48; height: 66; radius: 4
                                            color: "#0e1116"
                                            border.color: Theme.border2; border.width: 1
                                            clip: true
                                            Image {
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                source: modelData.thumbUrl
                                                fillMode: Image.PreserveAspectCrop
                                                cache: false
                                                asynchronous: true
                                            }
                                            // Frame number tag.
                                            Rectangle {
                                                anchors.bottom: parent.bottom
                                                width: parent.width; height: 12
                                                color: "#b0000000"
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "f" + modelData.frame
                                                    color: Theme.textBright
                                                    font { family: Theme.fontMono; pixelSize: 8 }
                                                }
                                            }
                                            // Jump to the sampled frame (whole thumb).
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: App.seekFrame(modelData.frame)
                                            }
                                            // Delete handle (larger hit area than the glyph).
                                            Rectangle {
                                                anchors.top: parent.top; anchors.right: parent.right
                                                width: 17; height: 17; radius: 8
                                                color: delMouse.containsMouse ? Theme.red : "#c0121419"
                                                Text { anchors.centerIn: parent; text: "×"; color: "white"; font.pixelSize: 12 }
                                                MouseArea {
                                                    id: delMouse
                                                    anchors.fill: parent
                                                    anchors.margins: -6   // easier to hit
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: App.playerSamples.remove(modelData.id)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ---- team line-up graphics ----
                    Rectangle {
                        width: parent.width; height: 1; color: Theme.border
                    }
                    Text {
                        text: "TEAM LINE-UPS"
                        color: Theme.text
                        font { family: Theme.fontUi; pixelSize: 13; weight: Font.Bold; letterSpacing: 0.5 }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Grab the broadcast line-up graphic for each team "
                            + "(usually one or two frames per team). \"Grab frame\" "
                            + "saves the whole frame; \"Crop\" lets you drag a box. "
                            + "Then run OCR to read each shirt number and where it "
                            + "sits on the graphic — that position helps infer who "
                            + "is who by where they stand on the pitch."
                        color: Theme.textDim
                        font { family: Theme.fontUi; pixelSize: 12 }
                    }

                    // Run OCR over the captured line-up graphics.
                    Row {
                        width: parent.width
                        spacing: 8
                        readonly property bool anyImages:
                            view.allSamples.some(s => s.role === 6 || s.role === 7)

                        Rectangle {
                            width: parent.width - clearBtn.width - 8; height: 34; radius: 8
                            color: App.lineupOcrRunning ? Theme.surfaceHi : Theme.green
                            opacity: (parent.anyImages && !App.lineupOcrRunning) ? 1 : 0.5
                            Text {
                                anchors.centerIn: parent
                                text: App.lineupOcrRunning
                                    ? (App.lineupOcrLabel || "OCR…")
                                    : "Extract positions (OCR)"
                                color: App.lineupOcrRunning ? Theme.text : "#10231a"
                                font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: parent.parent.anyImages && !App.lineupOcrRunning
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.extractLineupPositions()
                            }
                        }
                        Rectangle {
                            id: clearBtn
                            width: 64; height: 34; radius: 8
                            color: Theme.surfaceHi
                            border.color: Theme.border2; border.width: 1
                            Text {
                                anchors.centerIn: parent; text: "Clear"
                                color: Theme.text
                                font { family: Theme.fontUi; pixelSize: 12 }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.clearLineupPositions()
                            }
                        }
                    }
                    // OCR status / result summary.
                    Text {
                        width: parent.width
                        visible: App.lineupOcrLabel.length > 0 && !App.lineupOcrRunning
                        wrapMode: Text.WordWrap
                        text: App.lineupOcrLabel
                        color: Theme.textDim
                        font { family: Theme.fontMono; pixelSize: 11 }
                    }

                    Repeater {
                        model: view.lineupRoles
                        delegate: Rectangle {
                            id: lupCard
                            required property var modelData
                            readonly property int roleIndex: modelData.i
                            readonly property var mine:
                                view.allSamples.filter(s => s.role === roleIndex)
                            readonly property bool active: view.captureRole === roleIndex
                            // OCR'd shirt-number positions for this team.
                            readonly property var positions: roleIndex === 6
                                ? (App.lineupPositions.teamA || [])
                                : (App.lineupPositions.teamB || [])

                            width: panel.width
                            implicitHeight: lrows.height + 20
                            radius: 10
                            color: Theme.surface
                            border.color: active ? modelData.color : Theme.border
                            border.width: active ? 2 : 1

                            Column {
                                id: lrows
                                x: 14; y: 10
                                width: parent.width - 28
                                spacing: 10

                                Row {
                                    width: parent.width
                                    spacing: 10
                                    Rectangle {
                                        width: 14; height: 14; radius: 4
                                        color: modelData.color
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Column {
                                        width: parent.width - 14 - 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text {
                                            text: modelData.name
                                            color: Theme.text
                                            font { family: Theme.fontUi; pixelSize: 13; weight: Font.DemiBold }
                                        }
                                        Text {
                                            text: mine.length + " image(s)"
                                            color: Theme.textDim
                                            font { family: Theme.fontUi; pixelSize: 11 }
                                        }
                                    }
                                }

                                // Grab-frame / Crop actions.
                                Row {
                                    width: parent.width
                                    spacing: 8
                                    Rectangle {
                                        width: (parent.width - 8) / 2; height: 32; radius: 8
                                        color: Theme.surfaceHi
                                        border.color: Theme.border2; border.width: 1
                                        opacity: App.videoLoaded ? 1 : 0.45
                                        Text {
                                            anchors.centerIn: parent
                                            text: "Grab frame"
                                            color: Theme.text
                                            font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: App.videoLoaded
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: App.captureFullFrameSample(roleIndex)
                                        }
                                    }
                                    Rectangle {
                                        width: (parent.width - 8) / 2; height: 32; radius: 8
                                        color: active ? modelData.color : Theme.surfaceHi
                                        border.color: active ? modelData.color : Theme.border2
                                        border.width: 1
                                        opacity: App.videoLoaded ? 1 : 0.45
                                        Text {
                                            anchors.centerIn: parent
                                            text: active ? "Cropping…" : "Crop"
                                            color: active ? "#10231a" : Theme.text
                                            font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: App.videoLoaded
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: view.captureRole = active ? -1 : roleIndex
                                        }
                                    }
                                }

                                // Landscape thumbnails of the captured line-up graphics.
                                Flow {
                                    width: parent.width
                                    spacing: 6
                                    visible: mine.length > 0
                                    Repeater {
                                        model: mine
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: 104; height: 62; radius: 4
                                            color: "#0e1116"
                                            border.color: Theme.border2; border.width: 1
                                            clip: true
                                            Image {
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                source: modelData.thumbUrl
                                                fillMode: Image.PreserveAspectCrop
                                                cache: false
                                                asynchronous: true
                                            }
                                            Rectangle {
                                                anchors.bottom: parent.bottom
                                                width: parent.width; height: 12
                                                color: "#b0000000"
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "f" + modelData.frame
                                                    color: Theme.textBright
                                                    font { family: Theme.fontMono; pixelSize: 8 }
                                                }
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: App.seekFrame(modelData.frame)
                                            }
                                            Rectangle {
                                                anchors.top: parent.top; anchors.right: parent.right
                                                width: 17; height: 17; radius: 8
                                                color: lupDel.containsMouse ? Theme.red : "#c0121419"
                                                Text { anchors.centerIn: parent; text: "×"; color: "white"; font.pixelSize: 12 }
                                                MouseArea {
                                                    id: lupDel
                                                    anchors.fill: parent
                                                    anchors.margins: -6
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: App.playerSamples.remove(modelData.id)
                                                }
                                            }
                                        }
                                    }
                                }

                                // Formation map: OCR'd shirt numbers placed at
                                // their position on the graphic (normalised).
                                Column {
                                    width: parent.width
                                    spacing: 5
                                    visible: lupCard.positions.length > 0
                                    Text {
                                        text: lupCard.positions.length + " position(s)"
                                        color: Theme.textDim
                                        font { family: Theme.fontUi; pixelSize: 11 }
                                    }
                                    Rectangle {
                                        id: pitchBox
                                        width: parent.width
                                        height: width * 9 / 16
                                        radius: 6
                                        color: "#12321f"
                                        border.color: Theme.border2; border.width: 1
                                        clip: true
                                        // Centre line, for orientation.
                                        Rectangle {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width; height: 1
                                            color: "#204a30"
                                        }
                                        Repeater {
                                            model: lupCard.positions
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: 20; height: 20; radius: 10
                                                color: lupCard.modelData.color
                                                x: modelData.x * pitchBox.width - width / 2
                                                y: modelData.y * pitchBox.height - height / 2
                                                border.color: "#10231a"; border.width: 1
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: modelData.number
                                                    color: "#10231a"
                                                    font { family: Theme.fontUi; pixelSize: 10; weight: Font.Bold }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: !App.videoLoaded
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Open a video to start sampling."
                        color: Theme.textFaint
                        font { family: Theme.fontUi; pixelSize: 12 }
                    }
                }
            }
        }
    }
}
