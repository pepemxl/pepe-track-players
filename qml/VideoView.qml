import QtQuick
import QtQuick.Controls

// Section 1 — central player with tagging. Click the video to place a tag:
// a dropdown asks which player; the tag stores frame + video/pitch coords.
// Clicking inside a tracked bounding box assigns the player to that track
// instead; right-click on a box unassigns it (or reveals its track id).
Item {
    id: view

    property bool showDetections: true

    // View-crop selection: 0 = off, 1 = awaiting top-left corner,
    // 2 = awaiting bottom-right corner.
    property int cropStep: 0
    property point cropTL: Qt.point(0, 0)

    // A freshly added video asks for its view corners right away.
    Connections {
        target: App.match
        function onMatchChanged() {
            if (App.match.cropPending && App.videoLoaded)
                view.cropStep = 1
        }
    }

    // Leave crop mode when the video is unloaded (e.g. project deleted).
    Connections {
        target: App
        function onVideoStateChanged() {
            if (!App.videoLoaded)
                view.cropStep = 0
        }
    }

    Row {
        anchors.fill: parent

        // ---- collapsible video-ops panel ----
        VideoOpsPanel {
            id: opsPanel
            height: parent.height
            onCropRequested: view.cropStep = 1
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

                    // Detected player boxes from the chunk-tracking CSVs,
                    // looked up by playback position (0.1 s slots).
                    Repeater {
                        model: view.showDetections && App.videoLoaded
                               && App.tracking.hasDetections
                            ? App.tracking.detectionsAt(App.positionSec) : []
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool assigned: modelData.assigned === true
                            readonly property bool inferred: modelData.inferred === true
                            x: surface.fromVideoX(modelData.x)
                            y: surface.fromVideoY(modelData.y)
                            width: modelData.w * surface.videoScale
                            height: modelData.h * surface.videoScale
                            color: "transparent"
                            border.color: assigned ? Theme.teamColor(modelData.team)
                                : modelData.conf >= 0.5 ? Theme.green : Theme.yellow
                            border.width: assigned && !inferred ? 3 : 2
                            opacity: inferred ? 0.85 : 1
                            radius: 3

                            Rectangle {
                                anchors.bottom: parent.top
                                anchors.bottomMargin: 1
                                anchors.left: parent.left
                                width: detLabel.implicitWidth + 8
                                height: assigned ? 16 : 14
                                radius: 3
                                color: parent.border.color
                                Text {
                                    id: detLabel
                                    anchors.centerIn: parent
                                    // "≈" marks identities inferred by
                                    // propagation rather than tagged by hand.
                                    text: assigned
                                        ? (inferred ? "≈P" : "P") + modelData.playerNumber
                                        : "T" + modelData.trackId
                                    color: assigned ? "white" : "#10231a"
                                    font { family: Theme.fontMono; pixelSize: assigned ? 10 : 9; weight: Font.Bold }
                                }
                            }
                        }
                    }

                    // Detections on/off chip (below the frame badge). Above
                    // the tagging MouseArea so it stays clickable.
                    Rectangle {
                        visible: App.tracking.hasDetections
                        z: 3
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: 42
                        anchors.rightMargin: 12
                        width: detToggleText.implicitWidth + 18
                        height: 24
                        radius: 6
                        color: Theme.overlayBg
                        border.color: view.showDetections ? "#5930d980" : Theme.border2
                        border.width: 1
                        Text {
                            id: detToggleText
                            anchors.centerIn: parent
                            text: view.showDetections ? "▣ DETECTIONS ON" : "□ DETECTIONS OFF"
                            color: view.showDetections ? Theme.greenBright : Theme.textDim
                            font { family: Theme.fontMono; pixelSize: 10 }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: view.showDetections = !view.showDetections
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
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: enabled ? Qt.CrossCursor : Qt.ArrowCursor

                        function detectionAt(vx, vy) {
                            if (!view.showDetections || !App.tracking.hasDetections)
                                return null
                            const dets = App.tracking.detectionsAt(App.positionSec)
                            for (let i = 0; i < dets.length; ++i) {
                                const d = dets[i]
                                if (vx >= d.x && vx <= d.x + d.w
                                        && vy >= d.y && vy <= d.y + d.h)
                                    return d
                            }
                            return null
                        }

                        onClicked: (mouse) => {
                            if (!surface.insidePainted(mouse.x, mouse.y))
                                return
                            const vx = surface.toVideoX(mouse.x)
                            const vy = surface.toVideoY(mouse.y)

                            // View-crop selection has priority over tagging.
                            if (view.cropStep === 1 && mouse.button === Qt.LeftButton) {
                                view.cropTL = Qt.point(vx, vy)
                                view.cropStep = 2
                                return
                            }
                            if (view.cropStep === 2 && mouse.button === Qt.LeftButton) {
                                App.setVideoCrop(view.cropTL.x, view.cropTL.y, vx, vy)
                                view.cropStep = 0
                                return
                            }

                            // Right click on a box: unassign it, or reveal
                            // its track id when it has no player yet.
                            if (mouse.button === Qt.RightButton) {
                                const d = detectionAt(vx, vy)
                                if (!d)
                                    return
                                if (d.assigned === true) {
                                    App.clearTrackAssignment(d.key)
                                    trackToast.show("track " + d.key + " unassigned")
                                } else {
                                    trackToast.show("track " + d.key
                                                    + " · conf " + d.conf.toFixed(2))
                                }
                                return
                            }

                            App.pause()

                            // Left click inside a tracked bounding box assigns
                            // the player to that track instead of dropping a
                            // positional tag marker.
                            const hit = detectionAt(vx, vy)
                            tagDropdown.pendingTrackKey = hit ? hit.key : ""

                            tagDropdown.pendingVx = vx
                            tagDropdown.pendingVy = vy
                            tagDropdown.x = Math.min(mouse.x, surface.width - tagDropdown.width - 8)
                            tagDropdown.y = Math.min(mouse.y, surface.height - tagDropdown.height - 8)
                            tagDropdown.open()
                        }
                    }

                    // Selected camera view (crop) outline
                    Rectangle {
                        visible: App.videoLoaded && App.match.hasCrop
                        x: surface.fromVideoX(App.match.crop.x)
                        y: surface.fromVideoY(App.match.crop.y)
                        width: App.match.crop.width * surface.videoScale
                        height: App.match.crop.height * surface.videoScale
                        color: "transparent"
                        border.color: "#b3ffffff"
                        border.width: 1
                        Text {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: 3
                            text: "VIEW"
                            color: "#b3ffffff"
                            font { family: Theme.fontMono; pixelSize: 9; weight: Font.Bold }
                        }
                    }

                    // Top-left marker while picking the second corner
                    Rectangle {
                        visible: view.cropStep === 2
                        x: surface.fromVideoX(view.cropTL.x) - 5
                        y: surface.fromVideoY(view.cropTL.y) - 5
                        width: 10; height: 10; radius: 5
                        color: Theme.orange
                        border.color: "white"
                        border.width: 1.5
                        z: 3
                    }

                    // Crop-mode hint (bottom-left, like homography)
                    Rectangle {
                        visible: view.cropStep > 0 && App.videoLoaded
                        z: 3
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.margins: 12
                        width: cropHintRow.implicitWidth + 22
                        height: 28
                        radius: 6
                        color: "#e6ffb46b"
                        Row {
                            id: cropHintRow
                            anchors.centerIn: parent
                            spacing: 10
                            Text {
                                text: view.cropStep === 1
                                    ? "View crop: click the TOP-LEFT corner"
                                    : "View crop: click the BOTTOM-RIGHT corner"
                                color: "#402508"
                                font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "full frame"
                                color: "#7a4a10"
                                font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold }
                                anchors.verticalCenter: parent.verticalCenter
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -5
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        App.clearVideoCrop()
                                        view.cropStep = 0
                                    }
                                }
                            }
                            Text {
                                text: "cancel"
                                color: "#7a4a10"
                                font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold }
                                anchors.verticalCenter: parent.verticalCenter
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -5
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: view.cropStep = 0
                                }
                            }
                        }
                    }

                    // Transient info toast (right-click on a box)
                    Rectangle {
                        id: trackToast
                        property string message: ""
                        function show(msg) {
                            message = msg
                            opacity = 1
                            toastTimer.restart()
                        }
                        visible: opacity > 0
                        opacity: 0
                        z: 4
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 14
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: toastText.implicitWidth + 26
                        height: 30
                        radius: 8
                        color: "#e6121419"
                        border.color: "#5930d980"
                        border.width: 1
                        Behavior on opacity { NumberAnimation { duration: 180 } }
                        Text {
                            id: toastText
                            anchors.centerIn: parent
                            text: trackToast.message
                            color: Theme.greenBright
                            font { family: Theme.fontMono; pixelSize: 11 }
                        }
                        Timer {
                            id: toastTimer
                            interval: 1800
                            onTriggered: trackToast.opacity = 0
                        }
                    }

                    PlayerDropdown {
                        id: tagDropdown
                        property double pendingVx: 0
                        property double pendingVy: 0
                        property string pendingTrackKey: ""
                        title: pendingTrackKey !== ""
                            ? "ASSIGN PLAYER — TRACK " + pendingTrackKey
                            : "TAG PLAYER — FRAME " + App.currentFrame
                        onPlayerPicked: (team, rosterRow) => {
                            if (pendingTrackKey !== "") {
                                const p = (team === 0 ? App.homeRoster
                                                      : App.awayRoster).get(rosterRow)
                                App.assignTrack(pendingTrackKey,
                                                p.number, p.name, team)
                            } else {
                                App.addTag(pendingVx, pendingVy, team, rosterRow)
                            }
                        }
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
                        width: parent.width - 34 - 14 * 4
                               - 100 - durText.implicitWidth - speedBtn.width
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

                    // Playback rate selector (native, or fixed frames/s)
                    Rectangle {
                        id: speedBtn
                        width: speedLabel.implicitWidth + 24
                        height: 26
                        radius: 6
                        color: speedMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
                        border.color: App.playbackFps > 0 ? "#5930d980" : Theme.border2
                        border.width: 1
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            id: speedLabel
                            anchors.centerIn: parent
                            text: App.playbackFps > 0
                                ? App.playbackFps.toFixed(0) + " fps ▾"
                                : "native ▾"
                            color: App.playbackFps > 0 ? Theme.greenBright : Theme.textMid
                            font { family: Theme.fontMono; pixelSize: 11 }
                        }
                        MouseArea {
                            id: speedMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: speedMenu.open()
                        }

                        Popup {
                            id: speedMenu
                            x: speedBtn.width - width
                            y: -height - 6
                            width: 168
                            padding: 6
                            background: Rectangle {
                                color: "#1b1f26"
                                border.color: Theme.borderHi
                                border.width: 1
                                radius: 8
                            }
                            contentItem: Column {
                                spacing: 2
                                Repeater {
                                    model: [0, 1, 5, 10, 20, 30, 60, 120]
                                    delegate: Rectangle {
                                        required property var modelData
                                        readonly property bool current:
                                            Math.abs(App.playbackFps - modelData) < 0.01
                                        width: parent.width
                                        height: 26
                                        radius: 5
                                        color: rateMouse.containsMouse ? Theme.surfaceHi : "transparent"
                                        Row {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: 8
                                            spacing: 7
                                            Text {
                                                text: current ? "✓" : " "
                                                color: Theme.green
                                                font { family: Theme.fontMono; pixelSize: 11 }
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Text {
                                                text: modelData === 0
                                                    ? "native" + (App.videoLoaded
                                                        ? " (" + App.fps.toFixed(0) + " fps)" : "")
                                                    : modelData + " frames/s"
                                                color: current ? Theme.greenBright : Theme.text
                                                font { family: Theme.fontMono; pixelSize: 11 }
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }
                                        MouseArea {
                                            id: rateMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                App.playbackFps = modelData
                                                speedMenu.close()
                                            }
                                        }
                                    }
                                }
                            }
                        }
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

                Item {
                    width: parent.width
                    height: 22

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "TAGS · " + App.tags.count
                        color: Theme.textDim
                        font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                    }

                    // Undo / redo for tagging (tags + track assignments)
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5

                        Repeater {
                            model: [
                                { glyph: "↶", isUndo: true },
                                { glyph: "↷", isUndo: false },
                            ]
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool usable: modelData.isUndo
                                    ? App.canUndo : App.canRedo
                                width: 22; height: 22; radius: 6
                                color: histMouse.containsMouse && usable
                                    ? Theme.borderHi : Theme.surfaceHi
                                opacity: usable ? 1 : 0.35
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.glyph
                                    color: Theme.textBright
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    id: histMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: usable
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: modelData.isUndo ? App.undo() : App.redo()
                                }
                            }
                        }
                    }
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
                                onClicked: App.removeTag(index)
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
