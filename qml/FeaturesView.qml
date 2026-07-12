import QtQuick
import QtQuick.Controls

// Section 6 — Features. Generates and visualizes the per-frame feature masks
// that guide the homography flow / RANSAC: the grass (pitch) mask and the
// static broadcast-graphic mask (scoreboard, logos, banners). Masks are
// written under match_<id>/green_mask and match_<id>/static_mask.
Item {
    id: view

    // Chunk selected for showing a saved static mask.
    property int staticChunk: 1
    // Cached summary counts, refreshed on show / after a generation run.
    property var summary: ({ chunks: 0, greenChunks: 0, staticChunks: 0, greenFrames: 0 })

    // Manual logo-box definition (right-click a corner at a time). Corners are
    // stored in normalized [0,1] video coordinates; when both are set the box
    // is committed as a graphics region (reused for the RANSAC exclusion).
    property bool hasTL: false
    property bool hasBR: false
    property real tlX: 0
    property real tlY: 0
    property real brX: 0
    property real brY: 0
    // Last right-click position (normalized), used by the corner menu.
    property real menuX: 0
    property real menuY: 0

    function commitBoxIfReady() {
        if (hasTL && hasBR) {
            const x = Math.min(tlX, brX), y = Math.min(tlY, brY)
            App.homography.addGraphicsRegion(x, y, Math.abs(brX - tlX), Math.abs(brY - tlY))
            hasTL = false; hasBR = false
        }
    }

    // When the grass preview is active it re-computes on every frame so the
    // mask follows the picture as you step / play through the video.
    property bool greenFollow: false

    function refresh() { summary = App.maskSummary() }

    // Pause playback when the tab is opened so the first frame is stable
    // (use the transport below to step or play through it).
    onVisibleChanged: if (visible && App.playing) App.pause()

    Component.onCompleted: refresh()
    Connections {
        target: App
        function onMaskGenChanged() { if (!App.maskGenRunning) view.refresh() }
        function onVideoStateChanged() { view.refresh() }
        // Keep the grass overlay in sync with the currently displayed frame.
        function onFrameSerialChanged() {
            if (view.greenFollow && App.videoLoaded)
                App.previewGreenMask()
        }
    }

    Row {
        anchors.fill: parent

        // ---- left: frame + mask overlay ----
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
                    height: parent.height - transport.height - legend.height - 28

                    // Tinted mask overlay (green = grass, red = static graphics).
                    // The provider returns a pre-tinted ARGB image at the source
                    // resolution, so PreserveAspectFit aligns it with the frame.
                    Image {
                        anchors.fill: parent
                        visible: App.maskShown && maskToggle.checked
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        smooth: false
                        opacity: opacitySlider.value
                        source: App.maskShown
                            ? "image://featuremask/" + App.maskSerial : ""
                    }

                    // Mask description badge.
                    Rectangle {
                        visible: App.maskShown && App.maskInfo.length > 0
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 12
                        width: maskInfoText.implicitWidth + 20
                        height: 26
                        radius: 6
                        color: Theme.overlayBg
                        Text {
                            id: maskInfoText
                            anchors.centerIn: parent
                            text: App.maskInfo
                            color: Theme.textBright
                            font { family: Theme.fontMono; pixelSize: 11 }
                        }
                    }

                    // Manual logo boxes (reused graphics regions): red box + delete.
                    Repeater {
                        model: App.homography.graphics
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            visible: App.videoLoaded
                            x: surface.fromVideoX(modelData.x * App.videoWidth)
                            y: surface.fromVideoY(modelData.y * App.videoHeight)
                            width: surface.fromVideoX((modelData.x + modelData.w) * App.videoWidth) - x
                            height: surface.fromVideoY((modelData.y + modelData.h) * App.videoHeight) - y
                            color: "#2ee3544f"
                            border.color: Theme.red
                            border.width: 1.5
                            Text {
                                anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 2
                                text: "LOGO"
                                color: "#ffb59e"
                                font { family: Theme.fontMono; pixelSize: 9; weight: Font.Bold }
                            }
                            Rectangle {
                                anchors.top: parent.top; anchors.right: parent.right
                                width: 16; height: 16; radius: 8
                                color: boxDel.containsMouse ? Theme.red : "#b0503b"
                                Text { anchors.centerIn: parent; text: "×"; color: "white"; font.pixelSize: 12 }
                                MouseArea {
                                    id: boxDel
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: App.homography.removeGraphicsRegion(index)
                                }
                            }
                        }
                    }

                    // Pending corner markers (crosshairs) while defining a box.
                    Repeater {
                        model: [
                            { on: view.hasTL, nx: view.tlX, ny: view.tlY, tag: "TL" },
                            { on: view.hasBR, nx: view.brX, ny: view.brY, tag: "BR" },
                        ]
                        delegate: Item {
                            required property var modelData
                            visible: modelData.on && App.videoLoaded
                            x: surface.fromVideoX(modelData.nx * App.videoWidth)
                            y: surface.fromVideoY(modelData.ny * App.videoHeight)
                            Rectangle { x: -7; y: -1; width: 14; height: 2; color: Theme.orange }
                            Rectangle { x: -1; y: -7; width: 2; height: 14; color: Theme.orange }
                            Rectangle {
                                x: 6; y: -7; width: tagT.implicitWidth + 6; height: 13; radius: 3
                                color: Theme.overlayBg
                                Text { id: tagT; anchors.centerIn: parent; text: modelData.tag
                                       color: Theme.orange; font { family: Theme.fontMono; pixelSize: 9; weight: Font.Bold } }
                            }
                        }
                    }

                    // Right-click surface: pick a corner for a manual logo box.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        enabled: App.videoLoaded
                        onClicked: (mouse) => {
                            if (!surface.insidePainted(mouse.x, mouse.y)) return
                            if (App.videoWidth <= 0 || App.videoHeight <= 0) return
                            view.menuX = surface.toVideoX(mouse.x) / App.videoWidth
                            view.menuY = surface.toVideoY(mouse.y) / App.videoHeight
                            cornerMenu.popup()
                        }
                    }

                    Menu {
                        id: cornerMenu
                        MenuItem {
                            text: "Set top-left corner here"
                            onTriggered: {
                                view.tlX = view.menuX; view.tlY = view.menuY; view.hasTL = true
                                view.commitBoxIfReady()
                            }
                        }
                        MenuItem {
                            text: "Set bottom-right corner here"
                            onTriggered: {
                                view.brX = view.menuX; view.brY = view.menuY; view.hasBR = true
                                view.commitBoxIfReady()
                            }
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Clear pending corner"
                            enabled: view.hasTL || view.hasBR
                            onTriggered: { view.hasTL = false; view.hasBR = false }
                        }
                    }
                }

                // ---- transport: step / play through frames ----
                Row {
                    id: transport
                    width: parent.width
                    height: 34
                    spacing: 12
                    enabled: App.videoLoaded
                    opacity: App.videoLoaded ? 1 : 0.4

                    // Step back one frame.
                    Rectangle {
                        width: 34; height: 34; radius: 8
                        color: prevMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
                        anchors.verticalCenter: parent.verticalCenter
                        Text { anchors.centerIn: parent; text: "◀"; color: Theme.textBright; font.pixelSize: 12 }
                        MouseArea {
                            id: prevMouse; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: App.stepFrames(-1)
                        }
                    }

                    // Play / pause.
                    Rectangle {
                        width: 34; height: 34; radius: 8
                        color: playMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
                        anchors.verticalCenter: parent.verticalCenter
                        Row {
                            visible: App.playing
                            anchors.centerIn: parent
                            spacing: 3
                            Rectangle { width: 3; height: 12; color: Theme.textBright }
                            Rectangle { width: 3; height: 12; color: Theme.textBright }
                        }
                        Text {
                            visible: !App.playing
                            anchors.centerIn: parent
                            text: "▶"; color: Theme.textBright; font.pixelSize: 13
                        }
                        MouseArea {
                            id: playMouse; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: App.togglePlay()
                        }
                    }

                    // Step forward one frame.
                    Rectangle {
                        width: 34; height: 34; radius: 8
                        color: nextMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
                        anchors.verticalCenter: parent.verticalCenter
                        Text { anchors.centerIn: parent; text: "▶▏"; color: Theme.textBright; font.pixelSize: 11 }
                        MouseArea {
                            id: nextMouse; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: App.stepFrames(1)
                        }
                    }

                    Text {
                        id: tcLeft
                        text: App.timecode(App.positionSec)
                        color: Theme.textMid
                        font { family: Theme.fontMono; pixelSize: 12 }
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    // Scrubber.
                    Item {
                        width: parent.width - 34 * 3 - 12 * 5
                               - tcLeft.width - frameText.width
                        height: 34
                        anchors.verticalCenter: parent.verticalCenter

                        Rectangle {
                            id: ftrack
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 6; radius: 3
                            color: "#262b33"
                            Rectangle {
                                width: App.totalFrames > 1
                                    ? ftrack.width * App.currentFrame / (App.totalFrames - 1) : 0
                                height: parent.height; radius: 3
                                color: Theme.green
                            }
                            Rectangle {
                                x: (App.totalFrames > 1
                                    ? ftrack.width * App.currentFrame / (App.totalFrames - 1) : 0) - 6
                                anchors.verticalCenter: parent.verticalCenter
                                width: 12; height: 12; radius: 6
                                color: Theme.text
                                border.color: "#4d30d980"; border.width: 3
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
                        id: frameText
                        text: "frame " + App.currentFrame + " / " + App.totalFrames
                        color: Theme.textFaint
                        font { family: Theme.fontMono; pixelSize: 12 }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // ---- legend / overlay controls ----
                Row {
                    id: legend
                    width: parent.width
                    height: 34
                    spacing: 18

                    Row {
                        spacing: 8
                        anchors.verticalCenter: parent.verticalCenter
                        CheckBox {
                            id: maskToggle
                            checked: true
                            text: "Show overlay"
                            font { family: Theme.fontUi; pixelSize: 12 }
                            contentItem: Text {
                                text: maskToggle.text
                                color: Theme.textMuted
                                font: maskToggle.font
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: maskToggle.indicator.width + 6
                            }
                        }
                    }

                    Row {
                        spacing: 8
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            text: "Opacity"
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 12 }
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Slider {
                            id: opacitySlider
                            width: 120
                            from: 0.2; to: 1.0; value: 0.8
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Item { width: 1; height: 1 }

                    Row {
                        spacing: 14
                        anchors.verticalCenter: parent.verticalCenter
                        Repeater {
                            model: [
                                { c: Theme.green, t: "Grass / pitch" },
                                { c: Theme.red,   t: "Static graphics" },
                            ]
                            delegate: Row {
                                required property var modelData
                                spacing: 6
                                Rectangle {
                                    width: 10; height: 10; radius: 2
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
            }
        }

        // ---- right: control panel ----
        Rectangle {
            id: rightPanel
            width: 340
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
                    spacing: 20

                    Text {
                        text: "FEATURE MASKS"
                        color: Theme.text
                        font { family: Theme.fontUi; pixelSize: 15; weight: Font.Bold; letterSpacing: 0.5 }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Per-frame masks that tell the homography flow and "
                            + "RANSAC which pixels are pitch and which are burnt-in "
                            + "broadcast graphics. Saved under the match folder."
                        color: Theme.textDim
                        font { family: Theme.fontUi; pixelSize: 12 }
                    }

                    // ---- progress (shared) ----
                    Rectangle {
                        visible: App.maskGenRunning || App.maskGenLabel.length > 0
                        width: parent.width
                        height: genCol.height + 24
                        radius: 10
                        color: Theme.surface
                        border.color: Theme.border
                        border.width: 1
                        Column {
                            id: genCol
                            x: 14; y: 12
                            width: parent.width - 28
                            spacing: 8
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: App.maskGenLabel
                                color: App.maskGenRunning ? Theme.greenBright : Theme.textMuted
                                font { family: Theme.fontMono; pixelSize: 12 }
                            }
                            Rectangle {
                                visible: App.maskGenRunning
                                width: parent.width; height: 8; radius: 4
                                color: "#262b33"
                                Rectangle {
                                    width: parent.width * App.maskGenProgress
                                    height: parent.height; radius: 4
                                    color: Theme.green
                                }
                            }
                            Rectangle {
                                visible: App.maskGenRunning
                                width: cancelText.implicitWidth + 24; height: 28; radius: 7
                                color: Theme.surfaceHi
                                border.color: Theme.border2; border.width: 1
                                Text {
                                    id: cancelText
                                    anchors.centerIn: parent
                                    text: "Cancel"
                                    color: Theme.red
                                    font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: App.cancelMaskGen()
                                }
                            }
                        }
                    }

                    // ---- green mask ----
                    Column {
                        width: parent.width
                        spacing: 10

                        Row {
                            spacing: 8
                            Rectangle { width: 12; height: 12; radius: 3; color: Theme.green
                                        anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                text: "GRASS MASK"
                                color: Theme.textDim
                                font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: "Detects the field (green in HSV, largest blob). "
                                + view.summary.greenFrames + " frame mask(s) over "
                                + view.summary.greenChunks + "/" + view.summary.chunks + " chunk(s)."
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }

                        FeatureButton {
                            width: parent.width
                            label: "Preview current frame"
                            accent: Theme.green
                            enabled: App.videoLoaded && !App.maskGenRunning
                            onClicked: App.previewGreenMask()
                        }
                        FeatureButton {
                            width: parent.width
                            label: App.maskGenRunning && App.maskGenKind === "green"
                                   ? "Generating…" : "Generate all chunks → disk"
                            filled: true
                            accent: Theme.green
                            enabled: App.videoLoaded && !App.maskGenRunning && view.summary.chunks > 0
                            onClicked: App.generateGreenMasks()
                        }
                        Text {
                            visible: view.summary.chunks === 0
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: "No chunks yet — create them in the Chunks tab first."
                            color: Theme.yellow
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.border }

                    // ---- static mask ----
                    Column {
                        width: parent.width
                        spacing: 10

                        Row {
                            spacing: 8
                            Rectangle { width: 12; height: 12; radius: 3; color: Theme.red
                                        anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                text: "STATIC GRAPHICS MASK"
                                color: Theme.textDim
                                font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: "Finds pixels that barely change across a chunk while "
                                + "the scene pans — scoreboards, logos, banners burnt "
                                + "into the feed. " + view.summary.staticChunks + "/"
                                + view.summary.chunks + " chunk(s) done."
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }

                        FeatureButton {
                            width: parent.width
                            label: App.maskGenRunning && App.maskGenKind === "static"
                                   ? "Generating…" : "Generate all chunks → disk"
                            filled: true
                            accent: Theme.red
                            enabled: App.videoLoaded && !App.maskGenRunning && view.summary.chunks > 0
                            onClicked: App.generateStaticMasks()
                        }

                        // Chunk picker + show.
                        Row {
                            width: parent.width
                            spacing: 8
                            visible: view.summary.staticChunks > 0

                            Text {
                                text: "Chunk"
                                color: Theme.textMuted
                                font { family: Theme.fontUi; pixelSize: 12 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Rectangle {
                                width: 34; height: 30; radius: 6
                                color: Theme.surfaceHi; border.color: Theme.border2; border.width: 1
                                anchors.verticalCenter: parent.verticalCenter
                                Text { anchors.centerIn: parent; text: "−"; color: Theme.text
                                       font { family: Theme.fontUi; pixelSize: 16 } }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: view.staticChunk = Math.max(1, view.staticChunk - 1) }
                            }
                            Rectangle {
                                width: 46; height: 30; radius: 6
                                color: Theme.surface; border.color: Theme.border2; border.width: 1
                                anchors.verticalCenter: parent.verticalCenter
                                Text { anchors.centerIn: parent
                                       text: String(view.staticChunk).padStart(3, "0")
                                       color: Theme.text; font { family: Theme.fontMono; pixelSize: 13 } }
                            }
                            Rectangle {
                                width: 34; height: 30; radius: 6
                                color: Theme.surfaceHi; border.color: Theme.border2; border.width: 1
                                anchors.verticalCenter: parent.verticalCenter
                                Text { anchors.centerIn: parent; text: "+"; color: Theme.text
                                       font { family: Theme.fontUi; pixelSize: 16 } }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: view.staticChunk = Math.min(view.summary.chunks, view.staticChunk + 1) }
                            }
                            FeatureButton {
                                label: "Show"
                                accent: Theme.red
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: App.showStaticMask(view.staticChunk)
                            }
                        }

                        // Vote fraction: how many chunks a pixel must be static
                        // in to survive into the combined RANSAC-exclusion mask.
                        Column {
                            width: parent.width
                            spacing: 4
                            Item {
                                width: parent.width
                                height: voteLbl.implicitHeight
                                Text {
                                    id: voteLbl
                                    anchors.left: parent.left
                                    text: "Combine vote"
                                    color: Theme.textMuted
                                    font { family: Theme.fontUi; pixelSize: 12 }
                                }
                                Text {
                                    anchors.right: parent.right
                                    text: Math.round(App.homography.staticVoteFrac * 100) + "%"
                                    color: Theme.textBright
                                    font { family: Theme.fontMono; pixelSize: 12 }
                                }
                            }
                            Slider {
                                id: voteSlider
                                width: parent.width
                                from: 0.05; to: 1.0
                                value: App.homography.staticVoteFrac
                                onMoved: App.homography.staticVoteFrac = value
                            }
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: "Lower = more inclusive (catches graphics seen in "
                                    + "few chunks); higher = only ever-present overlays."
                                color: Theme.textDim
                                font { family: Theme.fontUi; pixelSize: 10 }
                            }
                        }

                        FeatureButton {
                            width: parent.width
                            label: "Show combined RANSAC mask"
                            accent: Theme.red
                            enabled: App.videoLoaded
                            onClicked: App.showStaticUnion()
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.border }

                    // ---- manual logo boxes ----
                    Column {
                        width: parent.width
                        spacing: 10

                        Row {
                            spacing: 8
                            Rectangle { width: 12; height: 12; radius: 3; color: "transparent"
                                        border.color: Theme.red; border.width: 2
                                        anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                text: "MANUAL LOGO BOXES"
                                color: Theme.textDim
                                font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: App.homography.graphicsCount + " box(es)"
                                color: App.homography.graphicsCount > 0 ? "#ffb59e" : Theme.textFaint
                                font { family: Theme.fontMono; pixelSize: 11 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: "Refine static graphics by hand: right-click the video "
                                + "and pick a corner (top-left, then bottom-right). These "
                                + "boxes are excluded from the flow too, and are shared "
                                + "with the Homography tab."
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }
                        Text {
                            visible: view.hasTL !== view.hasBR   // exactly one corner set
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: view.hasTL ? "Top-left set — right-click the bottom-right corner."
                                             : "Bottom-right set — right-click the top-left corner."
                            color: Theme.orange
                            font { family: Theme.fontUi; pixelSize: 11; weight: Font.DemiBold }
                        }
                        FeatureButton {
                            width: parent.width
                            label: "Clear all boxes"
                            enabled: App.homography.graphicsCount > 0
                            onClicked: { view.hasTL = false; view.hasBR = false; App.homography.clearGraphics() }
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.border }

                    FeatureButton {
                        width: parent.width
                        label: "Hide / clear overlay"
                        enabled: App.maskShown
                        onClicked: App.clearMaskPreview()
                    }
                }
            }
        }
    }

    // Small local button style so this view stays self-contained.
    component FeatureButton: Rectangle {
        id: btn
        property string label: ""
        property color accent: Theme.green
        property bool filled: false
        property bool enabled: true
        signal clicked()

        implicitWidth: btnText.implicitWidth + 32
        width: implicitWidth
        height: 38
        radius: 9
        opacity: enabled ? 1 : 0.4
        color: filled ? accent : Theme.surfaceHi
        border.color: filled ? accent : Theme.border2
        border.width: 1

        Text {
            id: btnText
            anchors.centerIn: parent
            text: btn.label
            color: btn.filled ? "#10231a" : Theme.text
            font { family: Theme.fontUi; pixelSize: 13; weight: Font.DemiBold }
        }
        MouseArea {
            anchors.fill: parent
            enabled: btn.enabled
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: btn.clicked()
        }
    }
}
