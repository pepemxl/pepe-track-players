import QtQuick
import QtQuick.Controls

// Top-level Chunks view: lists the 1-minute chunks of the current video and
// runs, for the selected one, the per-chunk operations track players /
// infer IDs / tag players. Selecting a chunk seeks the main player to it;
// "Tag players" switches to the Video view for full tagging.
Item {
    id: view
    readonly property var match: App.match

    property int selected: -1     // selected chunk number, -1 = none
    property var chunks: []
    property bool showDetections: true   // detection overlay on the preview

    // Ask Main to switch to the Video tab (for tagging in the chunk).
    signal openVideoTab()

    function reload() {
        chunks = match.registered ? match.chunksList() : []
    }
    function seekToChunk(startSec) {
        App.pause()
        App.seekFrame(Math.round(startSec * App.fps))
    }
    function selectedChunk() {
        for (var i = 0; i < chunks.length; ++i)
            if (chunks[i].number === selected) return chunks[i]
        return null
    }

    Component.onCompleted: reload()
    Connections {
        target: App.match
        // Switching the current video (camera) rebuilds the chunk list.
        function onMatchChanged() { view.selected = -1; view.reload() }
        function onOpStateChanged() { if (!App.match.opRunning) view.reload() }
    }

    Row {
        anchors.fill: parent

        // ================= left rail: chunk list =================
        Rectangle {
            id: listRail
            width: 320
            height: parent.height
            color: Theme.bgDark
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }

            Column {
                anchors.fill: parent
                anchors.margins: 18
                anchors.topMargin: 20
                spacing: 10

                // ---- camera selector: picks which video the chunk ops run on ----
                Column {
                    id: camBlock
                    width: parent.width
                    spacing: 5

                    Text {
                        text: "CAMERA"
                        color: Theme.textDim
                        font { family: Theme.fontUi; pixelSize: 12; weight: Font.Bold; letterSpacing: 0.5 }
                    }
                    Repeater {
                        model: view.match.videos
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool isCurrent: modelData.current === true
                            width: camBlock.width
                            height: 38
                            radius: 7
                            color: isCurrent ? "#1c2b22"
                                 : (camMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi)
                            border.color: isCurrent ? "#5930d980" : Theme.border2
                            border.width: 1
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                spacing: 8
                                Text {
                                    text: isCurrent ? "▣" : "▷"
                                    color: isCurrent ? Theme.greenBright : Theme.textDim
                                    font.pixelSize: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 1
                                    Text {
                                        text: "video " + modelData.id + " · " + modelData.role
                                        color: Theme.textBright
                                        font { family: Theme.fontMono; pixelSize: 11; weight: Font.DemiBold }
                                    }
                                    Text {
                                        text: modelData.segment
                                              + (modelData.view ? " · ✂" + modelData.view : "")
                                        color: Theme.textDim
                                        font { family: Theme.fontMono; pixelSize: 9 }
                                    }
                                }
                            }
                            MouseArea {
                                id: camMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !parent.isCurrent
                                cursorShape: Qt.PointingHandCursor
                                // Make this video the current one: all chunk
                                // ops (track / infer / tag) target it.
                                onClicked: App.openProjectVideo(view.match.matchId,
                                                                modelData.id, modelData.path)
                            }
                        }
                    }
                    Text {
                        visible: view.match.videos.length === 0
                        text: "Open a project video first."
                        color: Theme.textFaint
                        font { family: Theme.fontUi; pixelSize: 11 }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: Theme.border }

                Row {
                    id: chunksHeader
                    width: parent.width
                    Text {
                        text: "CHUNKS"
                        color: Theme.textDim
                        font { family: Theme.fontUi; pixelSize: 12; weight: Font.Bold; letterSpacing: 0.5 }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Item { width: parent.width - 70 - reloadLbl.implicitWidth; height: 1 }
                    Text {
                        id: reloadLbl
                        text: "↻ reload"
                        color: reloadMouse.containsMouse ? Theme.greenBright : Theme.textDim
                        font { family: Theme.fontUi; pixelSize: 11; weight: Font.DemiBold }
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            id: reloadMouse
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: view.reload()
                        }
                    }
                }

                Text {
                    visible: view.chunks.length === 0
                    width: parent.width
                    text: view.match.registered
                        ? "No chunks yet. Run “Create chunks”\nin the Video · Ops panel first."
                        : "Open a project video first."
                    color: Theme.textFaint
                    wrapMode: Text.WordWrap
                    font { family: Theme.fontUi; pixelSize: 12 }
                    lineHeight: 1.3
                }

                ListView {
                    width: parent.width
                    height: parent.height - camBlock.height - chunksHeader.height - 80
                    clip: true
                    spacing: 5
                    model: view.chunks
                    delegate: Rectangle {
                        required property var modelData
                        readonly property int number: modelData.number
                        readonly property bool sel: view.selected === number
                        width: ListView.view.width
                        height: 44
                        radius: 7
                        color: sel ? "#1c2b22" : (chMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi)
                        border.color: sel ? "#5930d980" : Theme.border2
                        border.width: 1
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 11
                            spacing: 9
                            Text {
                                text: modelData.hasCsv === true ? "✓" : "▸"
                                color: modelData.hasCsv === true ? Theme.green : Theme.textDim
                                font.pixelSize: 13
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text {
                                    text: "chunk " + number
                                          + (modelData.hasCsv === true ? "  · tracked" : "")
                                    color: Theme.textBright
                                    font { family: Theme.fontMono; pixelSize: 12; weight: Font.DemiBold }
                                }
                                Text {
                                    text: App.timecode(modelData.start_sec) + " – "
                                          + App.timecode(modelData.end_sec)
                                    color: Theme.textDim
                                    font { family: Theme.fontMono; pixelSize: 10 }
                                }
                            }
                        }
                        Text {
                            visible: parent.sel
                            anchors.right: parent.right
                            anchors.rightMargin: 11
                            anchors.verticalCenter: parent.verticalCenter
                            text: "●"
                            color: Theme.greenBright
                            font.pixelSize: 11
                        }
                        MouseArea {
                            id: chMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                view.selected = number
                                view.seekToChunk(modelData.start_sec)
                            }
                        }
                    }
                }
            }
        }

        // ================= center: preview + operations =================
        Item {
            width: parent.width - listRail.width
            height: parent.height

            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                Item {
                    width: parent.width
                    height: 22

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: view.selected > 0
                            ? "CHUNK " + view.selected
                              + (view.selectedChunk()
                                 ? "  ·  " + App.timecode(view.selectedChunk().start_sec)
                                   + " – " + App.timecode(view.selectedChunk().end_sec) : "")
                            : "SELECT A CHUNK"
                        color: view.selected > 0 ? Theme.greenBright : Theme.textDim
                        font { family: Theme.fontUi; pixelSize: 13; weight: Font.Bold; letterSpacing: 0.5 }
                    }

                    // Undo / redo for tagging (same command stack as the Video
                    // view; tags/assignments made here are reversible too).
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Text {
                            text: "TAG HISTORY"
                            color: Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 10; weight: Font.Bold; letterSpacing: 0.5 }
                            anchors.verticalCenter: parent.verticalCenter
                            rightPadding: 4
                        }
                        Repeater {
                            model: [
                                { glyph: "↶", isUndo: true },
                                { glyph: "↷", isUndo: false },
                            ]
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool usable: modelData.isUndo
                                    ? App.canUndo : App.canRedo
                                width: 24; height: 22; radius: 6
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

                VideoSurface {
                    id: preview
                    width: parent.width
                    height: parent.height - 20 - 14 - 40 - 14 - 60

                    // Detected player boxes from the chunk-tracking CSVs,
                    // looked up by playback position (same as the Video view).
                    Repeater {
                        model: view.showDetections && App.videoLoaded
                               && App.tracking.hasDetections
                            ? App.tracking.detectionsAt(App.positionSec) : []
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool assigned: modelData.assigned === true
                            readonly property bool inferred: modelData.inferred === true
                            x: preview.fromVideoX(modelData.x)
                            y: preview.fromVideoY(modelData.y)
                            width: modelData.w * preview.videoScale
                            height: modelData.h * preview.videoScale
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

                    // Detections on/off chip (top-right of the preview).
                    Rectangle {
                        visible: App.tracking.hasDetections
                        z: 3
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 12
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

                    // Positional tag markers near the current frame (±1s).
                    Repeater {
                        model: App.tags
                        delegate: Rectangle {
                            required property var model
                            visible: App.videoLoaded
                                     && Math.abs(model.frame - App.currentFrame) <= App.fps
                            x: preview.fromVideoX(model.x) - 9
                            y: preview.fromVideoY(model.y) - 9
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

                    // Tagging: click a box to assign its track to a player,
                    // click empty pitch to drop a positional tag, right-click a
                    // box to unassign it (or reveal its track id).
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
                            if (!preview.insidePainted(mouse.x, mouse.y))
                                return
                            const vx = preview.toVideoX(mouse.x)
                            const vy = preview.toVideoY(mouse.y)

                            if (mouse.button === Qt.RightButton) {
                                const d = detectionAt(vx, vy)
                                if (!d)
                                    return
                                if (d.assigned === true) {
                                    App.clearTrackAssignment(d.key)
                                    chunkToast.show("track " + d.key + " unassigned")
                                } else {
                                    chunkToast.show("track " + d.key
                                                    + " · conf " + d.conf.toFixed(2))
                                }
                                return
                            }

                            App.pause()
                            const hit = detectionAt(vx, vy)
                            tagDropdown.pendingTrackKey = hit ? hit.key : ""
                            tagDropdown.pendingVx = vx
                            tagDropdown.pendingVy = vy
                            tagDropdown.x = Math.min(mouse.x, preview.width - tagDropdown.width - 8)
                            tagDropdown.y = Math.min(mouse.y, preview.height - tagDropdown.height - 8)
                            tagDropdown.open()
                        }
                    }

                    // Transient info toast (right-click on a box)
                    Rectangle {
                        id: chunkToast
                        property string message: ""
                        function show(msg) { message = msg; opacity = 1; toastTimer.restart() }
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
                            text: chunkToast.message
                            color: Theme.greenBright
                            font { family: Theme.fontMono; pixelSize: 11 }
                        }
                        Timer {
                            id: toastTimer
                            interval: 1800
                            onTriggered: chunkToast.opacity = 0
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
                                App.assignTrack(pendingTrackKey, p.number, p.name, team)
                            } else {
                                App.addTag(pendingVx, pendingVy, team, rosterRow)
                            }
                        }
                    }
                }

                // transport for the preview (main player)
                Row {
                    width: parent.width
                    height: 34
                    spacing: 12
                    Rectangle {
                        width: 34; height: 34; radius: 8
                        color: playMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
                        Text {
                            anchors.centerIn: parent
                            text: App.playing ? "❚❚" : "▶"
                            color: Theme.textBright
                            font.pixelSize: 12
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
                    Item {
                        width: parent.width - 34 - 12 * 3 - durTxt.implicitWidth
                        height: 34
                        anchors.verticalCenter: parent.verticalCenter
                        Rectangle {
                            id: pTrack
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width; height: 6; radius: 3
                            color: "#262b33"
                            Rectangle {
                                width: App.totalFrames > 1
                                    ? pTrack.width * App.currentFrame / (App.totalFrames - 1) : 0
                                height: parent.height; radius: 3
                                color: Theme.green
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: App.videoLoaded
                            onPressed: (mouse) => App.seekFrac(mouse.x / width)
                            onPositionChanged: (mouse) => { if (pressed) App.seekFrac(mouse.x / width) }
                        }
                    }
                    Text {
                        id: durTxt
                        text: App.timecode(App.durationSec)
                        color: Theme.textFaint
                        font { family: Theme.fontMono; pixelSize: 12 }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // per-chunk operations
                Row {
                    width: parent.width
                    height: 40
                    spacing: 10
                    ChunkOp {
                        width: (parent.width - 10 * 2) / 3
                        label: "Track players"
                        sub: "detect + track this chunk"
                        onTriggered: view.match.trackChunk(view.selected)
                    }
                    ChunkOp {
                        width: (parent.width - 10 * 2) / 3
                        label: "Infer IDs · here"
                        sub: "propagate tags at this frame"
                        enabled3: App.tracking.hasDetections
                        onTriggered: App.tracking.inferIdentities(false, App.positionSec)
                    }
                    ChunkOp {
                        width: (parent.width - 10 * 2) / 3
                        label: "Tag players"
                        sub: "open in the Video view"
                        onTriggered: view.openVideoTab()
                    }
                }

                // op progress mirror
                Column {
                    width: parent.width
                    spacing: 6
                    visible: view.match.opRunning || view.match.lastError.length > 0
                    Text {
                        width: parent.width
                        text: view.match.lastError.length > 0 ? view.match.lastError : view.match.opLabel
                        color: view.match.lastError.length > 0 ? Theme.red : Theme.greenBright
                        font { family: Theme.fontMono; pixelSize: 11 }
                        wrapMode: Text.WordWrap
                    }
                    Rectangle {
                        visible: view.match.opRunning
                        width: parent.width; height: 6; radius: 3
                        color: "#262b33"
                        Rectangle {
                            width: parent.width * view.match.opProgress
                            height: parent.height; radius: 3
                            color: Theme.green
                        }
                    }
                }
            }
        }
    }

    // A per-chunk action button (disabled until a chunk is selected).
    component ChunkOp: Rectangle {
        id: opBtn
        property string label: ""
        property string sub: ""
        property bool enabled3: true
        signal triggered()

        readonly property bool usable: view.selected > 0 && !view.match.opRunning && enabled3
        height: 40
        radius: 8
        color: opMouse.containsMouse && usable ? Theme.borderHi : Theme.surfaceHi
        border.color: Theme.border2
        border.width: 1
        opacity: usable ? 1 : 0.45

        Column {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 12
            spacing: 1
            Text {
                text: opBtn.label
                color: Theme.textBright
                font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
            }
            Text {
                text: opBtn.sub
                color: Theme.textDim
                font { family: Theme.fontUi; pixelSize: 10 }
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
