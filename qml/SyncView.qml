import QtQuick
import QtQuick.Controls

// Top-level Camera-sync view: plays the project's main video and a second
// camera side by side and, for both, marks the frame where each 5-minute
// mark of a half occurs (min 0..45). The per-video (period, minute) -> frame
// table is saved in sync_points_<NN>.json and used to align the cameras.
Item {
    id: view
    readonly property var match: App.match

    // Active half for the sync marks.
    property string period: "1T"
    property var mainPoints: []
    property var secPoints: []

    readonly property var minutes: [0, 5, 10, 15, 20, 25, 30, 35, 40, 45]

    function reload() {
        mainPoints = (match.registered && match.videoId > 0)
            ? match.syncPoints(match.videoId) : []
        secPoints = App.secLoaded ? match.syncPoints(App.secVideoId) : []
    }
    function findPoint(points, minute) {
        for (var i = 0; i < points.length; ++i) {
            if (points[i].period === view.period && points[i].minute === minute)
                return points[i]
        }
        return null
    }

    Component.onCompleted: reload()
    Connections {
        target: App.match
        function onSyncPointsChanged() { view.reload() }
        function onMatchChanged() { view.reload() }
    }
    Connections {
        target: App
        function onSecStateChanged() { view.reload() }
    }

    Row {
        anchors.fill: parent

        // ================= players area =================
        Item {
            width: parent.width - rightRail.width
            height: parent.height

            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                Text {
                    text: "CAMERA SYNC"
                    color: Theme.textDim
                    font { family: Theme.fontUi; pixelSize: 12; weight: Font.Bold; letterSpacing: 0.5 }
                }

                // two players side by side
                Row {
                    id: players
                    width: parent.width
                    height: parent.height - 20
                    spacing: 16

                    // ---- MAIN player (project video) ----
                    Column {
                        width: (players.width - 16) / 2
                        height: parent.height
                        spacing: 8

                        Text {
                            text: view.match.registered
                                ? "MAIN · video " + view.match.videoId + " · " + view.match.videoRole
                                : "MAIN · no video"
                            color: Theme.textBright
                            font { family: Theme.fontMono; pixelSize: 12; weight: Font.DemiBold }
                        }

                        VideoSurface {
                            id: mainSurface
                            width: parent.width
                            height: parent.height - 8 - 28 - 8 - 26
                        }

                        // transport
                        Row {
                            width: parent.width
                            height: 28
                            spacing: 10
                            SyncPlayButton {
                                playing: App.playing
                                onToggle: App.togglePlay()
                            }
                            Text {
                                text: App.timecode(App.positionSec)
                                color: Theme.textMid
                                font { family: Theme.fontMono; pixelSize: 11 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            SyncScrubber {
                                width: parent.width - 34 - 10 * 3 - durMain.implicitWidth
                                frac: App.totalFrames > 1 ? App.currentFrame / (App.totalFrames - 1) : 0
                                onSeek: (f) => App.seekFrac(f)
                            }
                            Text {
                                id: durMain
                                text: App.timecode(App.durationSec)
                                color: Theme.textFaint
                                font { family: Theme.fontMono; pixelSize: 11 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        // frame step
                        Row {
                            width: parent.width
                            spacing: 6
                            Repeater {
                                model: [{l:"−10f",d:-10},{l:"−1f",d:-1},{l:"+1f",d:1},{l:"+10f",d:10}]
                                delegate: StepButton {
                                    required property var modelData
                                    width: (parent.width - 6 * 3) / 4
                                    label: modelData.l
                                    onClicked2: App.stepFrames(modelData.d)
                                }
                            }
                        }
                    }

                    // ---- CAM 2 player (secondary) ----
                    Column {
                        width: (players.width - 16) / 2
                        height: parent.height
                        spacing: 8

                        // camera selector chips
                        Flow {
                            width: parent.width
                            spacing: 6
                            Text {
                                text: "CAM 2:"
                                color: Theme.textDim
                                font { family: Theme.fontMono; pixelSize: 12; weight: Font.DemiBold }
                                height: 24
                                verticalAlignment: Text.AlignVCenter
                            }
                            Repeater {
                                model: view.match.videos
                                delegate: Rectangle {
                                    required property var modelData
                                    readonly property bool isCurrent: modelData.current === true
                                    readonly property bool isOpen: App.secVideoId === modelData.id && App.secLoaded
                                    visible: !isCurrent
                                    width: visible ? chipRow.implicitWidth + 18 : 0
                                    height: 24
                                    radius: 6
                                    color: isOpen ? Theme.green
                                         : (chipMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi)
                                    border.color: isOpen ? "transparent" : Theme.border2
                                    border.width: 1
                                    Row {
                                        id: chipRow
                                        anchors.centerIn: parent
                                        spacing: 5
                                        Text {
                                            text: "video " + modelData.id + " · " + modelData.role
                                                  + (modelData.view ? " ✂" + modelData.view : "")
                                            color: isOpen ? "#10231a" : Theme.textBright
                                            font { family: Theme.fontMono; pixelSize: 10; weight: Font.DemiBold }
                                        }
                                    }
                                    MouseArea {
                                        id: chipMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: isOpen ? App.closeSecondary()
                                                          : App.openSecondary(modelData.id, modelData.path)
                                    }
                                }
                            }
                            Text {
                                visible: view.match.videos.length <= 1
                                text: "add another video to the project"
                                color: Theme.textFaint
                                height: 24
                                verticalAlignment: Text.AlignVCenter
                                font { family: Theme.fontUi; pixelSize: 11 }
                            }
                        }

                        Rectangle {
                            id: secSurface
                            width: parent.width
                            height: parent.height - 8 - 24 - 8 - 28 - 8 - 26
                            radius: 10
                            color: "#181b21"
                            border.color: Theme.border
                            border.width: 1
                            clip: true
                            Image {
                                anchors.fill: parent
                                fillMode: Image.PreserveAspectFit
                                cache: false
                                asynchronous: false
                                visible: App.secLoaded
                                source: App.secLoaded ? "image://videoframe2/" + App.secFrameSerial : ""
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: !App.secLoaded
                                text: "Pick a second camera above"
                                color: Theme.textFaint
                                font { family: Theme.fontMono; pixelSize: 13 }
                            }
                            Rectangle {
                                visible: App.secLoaded
                                x: 12; y: 12
                                width: secBadge.implicitWidth + 18; height: 24; radius: 6
                                color: Theme.overlayBg
                                Text {
                                    id: secBadge
                                    anchors.centerIn: parent
                                    text: "FRAME " + App.secCurrentFrame + " / " + App.secTotalFrames
                                    color: Theme.greenBright
                                    font { family: Theme.fontMono; pixelSize: 11 }
                                }
                            }
                        }

                        // transport
                        Row {
                            width: parent.width
                            height: 28
                            spacing: 10
                            visible: App.secLoaded
                            SyncPlayButton {
                                playing: App.secPlaying
                                onToggle: App.toggleSecPlay()
                            }
                            Text {
                                text: App.timecode(App.secPositionSec)
                                color: Theme.textMid
                                font { family: Theme.fontMono; pixelSize: 11 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            SyncScrubber {
                                width: parent.width - 34 - 10 * 3 - durSec.implicitWidth
                                frac: App.secTotalFrames > 1 ? App.secCurrentFrame / (App.secTotalFrames - 1) : 0
                                onSeek: (f) => App.seekSecFrac(f)
                            }
                            Text {
                                id: durSec
                                text: App.secTotalFrames > 0 && App.secFps > 0
                                    ? App.timecode(App.secTotalFrames / App.secFps) : "0:00"
                                color: Theme.textFaint
                                font { family: Theme.fontMono; pixelSize: 11 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        // frame step
                        Row {
                            width: parent.width
                            spacing: 6
                            visible: App.secLoaded
                            Repeater {
                                model: [{l:"−10f",d:-10},{l:"−1f",d:-1},{l:"+1f",d:1},{l:"+10f",d:10}]
                                delegate: StepButton {
                                    required property var modelData
                                    width: (parent.width - 6 * 3) / 4
                                    label: modelData.l
                                    onClicked2: App.seekSecFrame(App.secCurrentFrame + modelData.d)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ================= right rail: mark table =================
        Rectangle {
            id: rightRail
            width: 360
            height: parent.height
            color: "transparent"
            Rectangle { width: 1; height: parent.height; color: Theme.border }

            Column {
                anchors.fill: parent
                anchors.margins: 18
                anchors.topMargin: 20
                spacing: 12

                Text {
                    text: "SYNC POINTS · every 5 min"
                    color: Theme.textDim
                    font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                }

                // period selector
                Row {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: [{ key: "1T", label: "1st half" }, { key: "2T", label: "2nd half" }]
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool active: view.period === modelData.key
                            width: (rightRail.width - 36 - 6) / 2
                            height: 30
                            radius: 7
                            color: active ? Theme.green : (perMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi)
                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                color: active ? "#10231a" : Theme.textBright
                                font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                            }
                            MouseArea {
                                id: perMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: view.period = modelData.key
                            }
                        }
                    }
                }

                // table header
                Row {
                    width: parent.width
                    Text {
                        width: 56; text: "MIN"
                        color: Theme.textFaint
                        font { family: Theme.fontUi; pixelSize: 10; weight: Font.Bold }
                    }
                    Text {
                        width: (rightRail.width - 36 - 56) / 2
                        horizontalAlignment: Text.AlignHCenter
                        text: "MAIN"
                        color: Theme.textFaint
                        font { family: Theme.fontUi; pixelSize: 10; weight: Font.Bold }
                    }
                    Text {
                        width: (rightRail.width - 36 - 56) / 2
                        horizontalAlignment: Text.AlignHCenter
                        text: "CAM 2"
                        color: Theme.textFaint
                        font { family: Theme.fontUi; pixelSize: 10; weight: Font.Bold }
                    }
                }

                // rows
                Column {
                    width: parent.width
                    spacing: 5
                    Repeater {
                        model: view.minutes
                        delegate: Row {
                            required property var modelData
                            readonly property int minute: modelData
                            readonly property var mainPt: view.findPoint(view.mainPoints, minute)
                            readonly property var secPt: view.findPoint(view.secPoints, minute)
                            width: parent.width
                            height: 30

                            Text {
                                width: 56; height: 30
                                verticalAlignment: Text.AlignVCenter
                                text: "min " + minute
                                color: Theme.textMid
                                font { family: Theme.fontMono; pixelSize: 12 }
                            }
                            SyncCell {
                                width: (rightRail.width - 36 - 56) / 2 - 3
                                point: parent.mainPt
                                enabled2: view.match.registered && App.videoLoaded && view.match.videoId > 0
                                onSetPoint: view.match.setSyncPoint(view.match.videoId, view.period,
                                                                   parent.minute, App.currentFrame)
                                onClearPoint: view.match.removeSyncPoint(view.match.videoId, view.period, parent.minute)
                                onJumpTo: App.seekFrame(parent.mainPt.frame)
                            }
                            Item { width: 6; height: 1 }
                            SyncCell {
                                width: (rightRail.width - 36 - 56) / 2 - 3
                                point: parent.secPt
                                enabled2: App.secLoaded
                                onSetPoint: view.match.setSyncPoint(App.secVideoId, view.period,
                                                                   parent.minute, App.secCurrentFrame)
                                onClearPoint: view.match.removeSyncPoint(App.secVideoId, view.period, parent.minute)
                                onJumpTo: App.seekSecFrame(parent.secPt.frame)
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Click an empty cell to store that video's current frame; "
                          + "click a marked cell to jump there."
                    color: Theme.textFaint
                    font { family: Theme.fontUi; pixelSize: 11 }
                    lineHeight: 1.3
                }
            }
        }
    }

    // ---------- reusable pieces ----------
    component SyncPlayButton: Rectangle {
        property bool playing: false
        signal toggle()
        width: 34; height: 28; radius: 7
        color: pbMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
        anchors.verticalCenter: parent.verticalCenter
        Text {
            anchors.centerIn: parent
            text: playing ? "❚❚" : "▶"
            color: Theme.textBright
            font.pixelSize: 11
        }
        MouseArea {
            id: pbMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.toggle()
        }
    }

    component SyncScrubber: Item {
        id: scrub
        property double frac: 0
        signal seek(double f)
        height: 28
        anchors.verticalCenter: parent.verticalCenter
        Rectangle {
            id: sTrack
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 6; radius: 3
            color: "#262b33"
            Rectangle {
                width: sTrack.width * Math.max(0, Math.min(1, scrub.frac))
                height: parent.height; radius: 3
                color: Theme.green
            }
        }
        MouseArea {
            anchors.fill: parent
            onPressed: (mouse) => scrub.seek(mouse.x / width)
            onPositionChanged: (mouse) => { if (pressed) scrub.seek(mouse.x / width) }
        }
    }

    component StepButton: Rectangle {
        property string label: ""
        signal clicked2()
        height: 24; radius: 6
        color: stepMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
        Text {
            anchors.centerIn: parent
            text: parent.label
            color: Theme.textBright
            font { family: Theme.fontMono; pixelSize: 10 }
        }
        MouseArea {
            id: stepMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked2()
        }
    }

    component SyncCell: Rectangle {
        id: cell
        property var point: null
        property bool enabled2: true
        signal setPoint()
        signal clearPoint()
        signal jumpTo()

        readonly property bool marked: point !== null && point !== undefined
        height: 30
        radius: 6
        color: marked ? "#1c2b22" : (cellMouse.containsMouse && enabled2 ? Theme.borderHi : Theme.surfaceHi)
        border.color: marked ? "#5930d980" : Theme.border2
        border.width: 1
        opacity: enabled2 ? 1 : 0.4

        // Base click area (declared first so the clear "×" below sits on top
        // of it and receives its own clicks). Set an empty cell, or jump to a
        // marked one.
        MouseArea {
            id: cellMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: cell.enabled2
            cursorShape: Qt.PointingHandCursor
            onClicked: cell.marked ? cell.jumpTo() : cell.setPoint()
        }

        Text {
            visible: !cell.marked
            anchors.centerIn: parent
            text: "set"
            color: Theme.textDim
            font { family: Theme.fontUi; pixelSize: 11; weight: Font.DemiBold }
        }
        Row {
            visible: cell.marked
            anchors.centerIn: parent
            spacing: 6
            Text {
                text: cell.marked ? "f" + cell.point.frame : ""
                color: Theme.greenBright
                font { family: Theme.fontMono; pixelSize: 11 }
                anchors.verticalCenter: parent.verticalCenter
            }
            // Clear button: sits above cellMouse so its own click wins.
            Rectangle {
                width: 18; height: 18; radius: 5
                color: clearMouse.containsMouse ? "#3a1720" : "transparent"
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    anchors.centerIn: parent
                    text: "×"
                    color: clearMouse.containsMouse ? Theme.red : Theme.textDim
                    font.pixelSize: 14
                }
                MouseArea {
                    id: clearMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: cell.clearPoint()
                }
            }
        }
    }
}
