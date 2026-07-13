import QtQuick
import QtQuick.Controls

// Section 2 — pitch calibration. Select a corner (A/B/C/D) in the right
// panel, click the video to place it, then Recompute H (cv::findHomography).
Item {
    id: view

    property string selectedPointId: ""
    // Which reference point (A/B/C/D) the mini-pitch is assigning a landmark to.
    property string pitchEditId: "A"
    // Last result message from the per-frame homography export.
    property string exportResult: ""
    // Graphics-region draw mode (phase F5): drag on the video to mark a static
    // on-screen overlay (scoreboard/logo) to exclude from the flow estimation.
    property bool graphicsMode: false
    property bool graphicsDragging: false
    property real gx0: 0
    property real gy0: 0
    property real gx1: 0
    property real gy1: 0

    function pointById(id) {
        const pts = App.homography.points
        for (let i = 0; i < pts.length; ++i)
            if (pts[i].id === id) return pts[i]
        return null
    }

    Row {
        anchors.fill: parent

        // ---- left: frame + quad overlay + frame stepper ----
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
                    height: parent.height - stepper.height - 14

                    // Quadrilateral + diagonals + midline drawn from the 4 points.
                    Canvas {
                        id: quadCanvas
                        anchors.fill: parent
                        property var pts: App.homography.points
                        property bool overlayOn: App.homography.overlayEnabled
                        // Repaint whenever the letterboxed geometry moves.
                        property real paintedW: surface.paintedWidth
                        property real paintedH: surface.paintedHeight
                        property bool loaded: App.videoLoaded
                        onPtsChanged: requestPaint()
                        onOverlayOnChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaintedWChanged: requestPaint()
                        onPaintedHChanged: requestPaint()
                        onLoadedChanged: requestPaint()

                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            if (!App.videoLoaded || pts.length !== 4)
                                return
                            const P = {}
                            for (const p of pts)
                                P[p.id] = { x: surface.fromVideoX(p.ix), y: surface.fromVideoY(p.iy) }

                            ctx.beginPath()
                            ctx.moveTo(P.A.x, P.A.y); ctx.lineTo(P.B.x, P.B.y)
                            ctx.lineTo(P.C.x, P.C.y); ctx.lineTo(P.D.x, P.D.y)
                            ctx.closePath()
                            ctx.fillStyle = "#1230d980"
                            ctx.fill()
                            ctx.strokeStyle = Theme.green
                            ctx.lineWidth = 2
                            ctx.stroke()

                            if (overlayOn) {
                                ctx.strokeStyle = "#8030d980"
                                ctx.lineWidth = 1
                                ctx.beginPath()
                                ctx.moveTo(P.A.x, P.A.y); ctx.lineTo(P.C.x, P.C.y)
                                ctx.moveTo(P.B.x, P.B.y); ctx.lineTo(P.D.x, P.D.y)
                                const mtx = (P.A.x + P.B.x) / 2, mty = (P.A.y + P.B.y) / 2
                                const mbx = (P.C.x + P.D.x) / 2, mby = (P.C.y + P.D.y) / 2
                                ctx.moveTo(mtx, mty); ctx.lineTo(mbx, mby)
                                ctx.stroke()
                            }
                        }
                    }

                    // Full pitch-model reprojection: every pitch line and
                    // reference landmark projected onto the image via H^-1,
                    // to eyeball how well the calibration fits the field.
                    Canvas {
                        id: modelCanvas
                        anchors.fill: parent
                        visible: App.homography.modelOverlayEnabled && App.videoLoaded
                        property int frame: App.homography.currentFrame
                        property var pts: App.homography.points
                        property real paintedW: surface.paintedWidth
                        property real paintedH: surface.paintedHeight
                        onVisibleChanged: requestPaint()
                        onFrameChanged: requestPaint()
                        onPtsChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaintedWChanged: requestPaint()
                        onPaintedHChanged: requestPaint()

                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            if (!visible)
                                return
                            const model = App.homography.projectedPitchModel(frame)
                            if (!model || !model.valid)
                                return
                            // Projected lines (cyan).
                            ctx.strokeStyle = "#22e0ff"
                            ctx.lineWidth = 1.6
                            ctx.lineJoin = "round"
                            for (const poly of model.lines) {
                                if (poly.length < 2) continue
                                ctx.beginPath()
                                ctx.moveTo(surface.fromVideoX(poly[0].x), surface.fromVideoY(poly[0].y))
                                for (let i = 1; i < poly.length; ++i)
                                    ctx.lineTo(surface.fromVideoX(poly[i].x), surface.fromVideoY(poly[i].y))
                                ctx.stroke()
                            }
                            // Reference landmark dots (orange).
                            ctx.fillStyle = "#ffb46b"
                            for (const p of model.points) {
                                const sx = surface.fromVideoX(p.x), sy = surface.fromVideoY(p.y)
                                ctx.beginPath()
                                ctx.arc(sx, sy, 2.6, 0, 2 * Math.PI)
                                ctx.fill()
                            }
                        }
                    }

                    // Corner handles
                    Repeater {
                        model: App.homography.points
                        delegate: Item {
                            required property var modelData
                            readonly property bool sel: view.selectedPointId === modelData.id
                            x: surface.fromVideoX(modelData.ix)
                            y: surface.fromVideoY(modelData.iy)
                            visible: App.videoLoaded

                            Rectangle {
                                anchors.centerIn: parent
                                width: sel ? 18 : 12
                                height: width
                                radius: width / 2
                                color: sel ? Theme.greenBright : Theme.orange
                                border.color: "white"
                                border.width: sel ? 2.5 : 1.5
                            }
                            Text {
                                x: 10; y: -18
                                text: modelData.id
                                color: Theme.orange
                                font { family: Theme.fontMono; pixelSize: 11; weight: Font.Bold }
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: App.videoLoaded && view.selectedPointId !== ""
                        cursorShape: enabled ? Qt.CrossCursor : Qt.ArrowCursor
                        onClicked: (mouse) => {
                            if (!surface.insidePainted(mouse.x, mouse.y))
                                return
                            App.homography.setImagePoint(view.selectedPointId,
                                                         surface.toVideoX(mouse.x),
                                                         surface.toVideoY(mouse.y))
                            view.selectedPointId = ""
                        }
                    }

                    // Graphics-mask regions (phase F5): red overlays over the
                    // masked on-screen areas, each with a delete handle.
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
                            color: "#33d0503b"
                            border.color: "#e0503b"
                            border.width: 1.5
                            Text {
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.margins: 2
                                text: "GFX"
                                color: "#ffb59e"
                                font { family: Theme.fontMono; pixelSize: 9; weight: Font.Bold }
                            }
                            Rectangle {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                width: 16; height: 16; radius: 8
                                color: gfxDel.containsMouse ? Theme.red : "#b0503b"
                                Text { anchors.centerIn: parent; text: "×"; color: "white"; font.pixelSize: 12 }
                                MouseArea {
                                    id: gfxDel
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: App.homography.removeGraphicsRegion(index)
                                }
                            }
                        }
                    }

                    // Rubber-band while drawing a new graphics region.
                    Rectangle {
                        visible: view.graphicsDragging
                        x: Math.min(view.gx0, view.gx1)
                        y: Math.min(view.gy0, view.gy1)
                        width: Math.abs(view.gx1 - view.gx0)
                        height: Math.abs(view.gy1 - view.gy0)
                        color: "#33ffb46b"
                        border.color: Theme.orange
                        border.width: 1.5
                    }

                    // Draw MouseArea (only in graphics mode; sits on top).
                    MouseArea {
                        anchors.fill: parent
                        enabled: view.graphicsMode && App.videoLoaded
                        visible: enabled
                        z: 20
                        cursorShape: Qt.CrossCursor
                        onPressed: (mouse) => {
                            view.gx0 = mouse.x; view.gy0 = mouse.y
                            view.gx1 = mouse.x; view.gy1 = mouse.y
                            view.graphicsDragging = true
                        }
                        onPositionChanged: (mouse) => {
                            if (view.graphicsDragging) { view.gx1 = mouse.x; view.gy1 = mouse.y }
                        }
                        onReleased: {
                            view.graphicsDragging = false
                            const x0 = surface.toVideoX(Math.min(view.gx0, view.gx1))
                            const y0 = surface.toVideoY(Math.min(view.gy0, view.gy1))
                            const x1 = surface.toVideoX(Math.max(view.gx0, view.gx1))
                            const y1 = surface.toVideoY(Math.max(view.gy0, view.gy1))
                            if (App.videoWidth > 0 && App.videoHeight > 0)
                                App.homography.addGraphicsRegion(
                                    x0 / App.videoWidth, y0 / App.videoHeight,
                                    (x1 - x0) / App.videoWidth, (y1 - y0) / App.videoHeight)
                        }
                    }

                    // Verify badge (top-right)
                    Rectangle {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 12
                        readonly property bool lowConf: App.homography.atPropagated
                                                        && App.homography.currentConfidence < 0.35
                        width: verifyRow.implicitWidth + 20; height: 24; radius: 6
                        color: lowConf ? "#d9532016"
                             : App.homography.atPropagated ? "#d9122a3f"
                             : App.homography.verified ? "#d91d3a2b" : "#d93a2f16"
                        Row {
                            id: verifyRow
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle {
                                width: 6; height: 6; radius: 3
                                color: parent.parent.lowConf ? "#ff6b4a"
                                     : App.homography.atPropagated ? "#5aa0ff"
                                     : App.homography.verified ? Theme.greenBright : Theme.yellow
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: App.homography.atKeyframe
                                        ? "KEYFRAME · FRAME " + App.homography.currentFrame
                                    : App.homography.atPropagated
                                        ? (parent.parent.lowConf ? "LOW CONF · " : "PROPAGATED · ")
                                          + Math.round(App.homography.currentConfidence * 100) + "%"
                                    : App.homography.verified
                                        ? "INTERPOLATED · " + App.homography.keyframeCount + " KF"
                                        : "NEEDS RECOMPUTE"
                                color: parent.parent.lowConf ? "#ffb59e"
                                     : App.homography.atPropagated ? "#d7e6ff"
                                     : App.homography.verified ? "#e6f5ec" : "#f5ecd0"
                                font { family: Theme.fontMono; pixelSize: 11; weight: Font.DemiBold }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }

                    // NO-PITCH badge (phase F4): current frame is a non-pitch
                    // shot, so the homography is invalid here.
                    Rectangle {
                        visible: App.hasShots && !App.pitchVisible
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.margins: 12
                        width: noPitchText.implicitWidth + 22; height: 24; radius: 6
                        color: "#d9532016"
                        border.color: "#c0503b"
                        border.width: 1
                        Text {
                            id: noPitchText
                            anchors.centerIn: parent
                            text: "⊘ NO PITCH · H invalid"
                            color: "#ffb59e"
                            font { family: Theme.fontMono; pixelSize: 11; weight: Font.DemiBold }
                        }
                    }

                    // Placement hint (bottom-left)
                    Rectangle {
                        visible: view.selectedPointId !== ""
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.margins: 12
                        width: hintText.implicitWidth + 22; height: 28; radius: 6
                        color: "#e6ffb46b"
                        Text {
                            id: hintText
                            anchors.centerIn: parent
                            text: "Click on the image to place point " + view.selectedPointId
                            color: "#402508"
                            font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                        }
                    }
                }

                // ---- frame stepper ----
                Row {
                    id: stepper
                    width: parent.width
                    height: 34
                    spacing: 14

                    Repeater {
                        model: [{ t: "‹", d: -1 }, { t: "›", d: 1 }]
                        delegate: Rectangle {
                            required property var modelData
                            width: 34; height: 34; radius: 8
                            color: stepMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
                            Text {
                                text: modelData.t
                                color: Theme.textBright
                                font.pixelSize: 16
                                anchors.centerIn: parent
                            }
                            MouseArea {
                                id: stepMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { App.pause(); App.stepFrames(modelData.d) }
                            }
                        }
                    }

                    Text {
                        text: "frame " + App.currentFrame
                        color: Theme.textMid
                        font { family: Theme.fontMono; pixelSize: 12 }
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Item {
                        width: parent.width - 34 * 2 - 14 * 4 - 120 - jumpStrip.width
                        height: 34
                        Rectangle {
                            id: homoTrack
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 6
                            radius: 3
                            color: "#262b33"
                            Rectangle {
                                width: App.totalFrames > 1
                                    ? homoTrack.width * App.currentFrame / (App.totalFrames - 1) : 0
                                height: parent.height
                                radius: 3
                                color: Theme.green
                            }
                            // Shot segments (phase F4): pitch = green tint,
                            // non-pitch (close-up/graphic) = red tint.
                            Repeater {
                                model: App.hasShots ? App.shots() : []
                                delegate: Rectangle {
                                    required property var modelData
                                    x: App.totalFrames > 1
                                        ? homoTrack.width * modelData.start / (App.totalFrames - 1) : 0
                                    width: Math.max(1, App.totalFrames > 1
                                        ? homoTrack.width * (modelData.end - modelData.start) / (App.totalFrames - 1) : 0)
                                    y: 8
                                    height: 4
                                    color: modelData.pitch ? "#3bd07a" : "#d0503b"
                                    opacity: 0.9
                                }
                            }
                            // Propagated (dense flow) range: keyframe span.
                            Rectangle {
                                visible: App.homography.propagated && App.homography.keyframeCount >= 2
                                readonly property var kfs: App.homography.keyframes
                                readonly property int firstF: kfs.length ? kfs[0].frame : 0
                                readonly property int lastF: kfs.length ? kfs[kfs.length - 1].frame : 0
                                x: App.totalFrames > 1
                                    ? homoTrack.width * firstF / (App.totalFrames - 1) : 0
                                width: App.totalFrames > 1
                                    ? homoTrack.width * (lastF - firstF) / (App.totalFrames - 1) : 0
                                height: parent.height
                                radius: 3
                                color: "#5aa0ff"
                            }
                            // Calibration keyframe ticks.
                            Repeater {
                                model: App.homography.keyframes
                                delegate: Rectangle {
                                    required property var modelData
                                    x: (App.totalFrames > 1
                                        ? homoTrack.width * modelData.frame / (App.totalFrames - 1) : 0) - 1.5
                                    y: -5
                                    width: 3; height: 16; radius: 1
                                    color: Theme.orange
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: App.videoLoaded
                            onPressed: (mouse) => { App.pause(); App.seekFrac(mouse.x / width) }
                            onPositionChanged: (mouse) => {
                                if (pressed) App.seekFrac(mouse.x / width)
                            }
                        }
                    }

                    // Quick-jump strip (12 evenly spaced positions)
                    Row {
                        id: jumpStrip
                        width: 220
                        height: 16
                        spacing: 2
                        anchors.verticalCenter: parent.verticalCenter
                        Repeater {
                            model: 12
                            delegate: Rectangle {
                                required property int index
                                width: (220 - 11 * 2) / 12
                                height: 16
                                radius: 2
                                color: index % 4 === 0 ? Theme.orange : Theme.green
                                opacity: jumpMouse.containsMouse ? 1 : 0.75
                                MouseArea {
                                    id: jumpMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { App.pause(); App.seekFrac(index / 11) }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- right panel: correspondences + error + actions ----
        Rectangle {
            id: rightPanel
            width: 320
            height: parent.height
            color: "transparent"

            Rectangle { width: 1; height: parent.height; color: Theme.border }

            Flickable {
                anchors.fill: parent
                anchors.margins: 18
                anchors.topMargin: 20
                contentHeight: rcol.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: rcol
                    width: parent.width
                    spacing: 16

                // ---- reference pitch: pick a landmark for a point ----
                Column {
                    width: parent.width
                    spacing: 8

                    Item {
                        width: parent.width
                        height: 16
                        Text {
                            anchors.left: parent.left
                            text: "REFERENCE PITCH"
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                        }
                        Text {
                            anchors.right: parent.right
                            text: "assign point " + view.pitchEditId
                            color: Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }
                    }

                    // Which point (A/B/C/D) the clicks assign to.
                    Row {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: ["A", "B", "C", "D"]
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool sel: view.pitchEditId === modelData
                                width: (parent.width - 6 * 3) / 4
                                height: 26
                                radius: 6
                                color: sel ? Theme.orange
                                     : (tgtMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi)
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: sel ? "#241505" : Theme.textBright
                                    font { family: Theme.fontMono; pixelSize: 12; weight: Font.Bold }
                                }
                                MouseArea {
                                    id: tgtMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: view.pitchEditId = modelData
                                }
                            }
                        }
                    }

                    // Mini pitch (105 x 68) with clickable landmark dots.
                    Rectangle {
                        id: miniPitch
                        width: parent.width
                        height: width * 68 / 105
                        radius: 8
                        color: "#12321f"
                        border.color: Theme.border
                        border.width: 1

                        readonly property real pad: 10
                        function mapX(px) { return pad + px / 105.0 * (width - 2 * pad) }
                        function mapY(py) { return pad + py / 68.0 * (height - 2 * pad) }

                        Canvas {
                            id: pitchCanvas
                            anchors.fill: parent
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.strokeStyle = "#5aa87a"
                                ctx.lineWidth = 1.2
                                const mx = miniPitch.mapX, my = miniPitch.mapY
                                function line(x1, y1, x2, y2) {
                                    ctx.beginPath(); ctx.moveTo(mx(x1), my(y1))
                                    ctx.lineTo(mx(x2), my(y2)); ctx.stroke()
                                }
                                function rect(x1, y1, x2, y2) {
                                    ctx.strokeRect(mx(x1), my(y1), mx(x2) - mx(x1), my(y2) - my(y1))
                                }
                                // boundary + halfway
                                rect(0, 0, 105, 68)
                                line(52.5, 0, 52.5, 68)
                                // centre circle + spot
                                const r = (9.15 / 105.0) * (miniPitch.width - 2 * miniPitch.pad)
                                ctx.beginPath()
                                ctx.arc(mx(52.5), my(34), r, 0, 2 * Math.PI); ctx.stroke()
                                function spot(x, y) {
                                    ctx.beginPath(); ctx.arc(mx(x), my(y), 1.5, 0, 2 * Math.PI)
                                    ctx.fillStyle = "#5aa87a"; ctx.fill()
                                }
                                spot(52.5, 34); spot(11, 34); spot(94, 34)
                                // penalty + goal areas
                                rect(0, 13.84, 16.5, 54.16)
                                rect(88.5, 13.84, 105, 54.16)
                                rect(0, 24.84, 5.5, 43.16)
                                rect(99.5, 24.84, 105, 43.16)
                                // penalty-area front line extended to the
                                // touchlines (guides for the box-front ×
                                // touchline landmarks).
                                ctx.save()
                                ctx.setLineDash([3, 3])
                                ctx.strokeStyle = "#3d7a58"
                                line(16.5, 0, 16.5, 13.84); line(16.5, 54.16, 16.5, 68)
                                line(88.5, 0, 88.5, 13.84); line(88.5, 54.16, 88.5, 68)
                                ctx.restore()
                            }
                        }

                        // Clickable landmark dots.
                        Repeater {
                            model: App.homography.pitchLandmarks()
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool cur: {
                                    const p = view.pointById(view.pitchEditId)
                                    return p && Math.abs(p.px - modelData.px) < 0.05
                                             && Math.abs(p.py - modelData.py) < 0.05
                                }
                                x: miniPitch.mapX(modelData.px) - width / 2
                                y: miniPitch.mapY(modelData.py) - height / 2
                                width: dotMouse.containsMouse || cur ? 12 : 8
                                height: width
                                radius: width / 2
                                color: cur ? Theme.orange
                                     : (dotMouse.containsMouse ? "#cfe9db" : "#8fbfa6")
                                border.color: "#12321f"
                                border.width: 1
                                Behavior on width { NumberAnimation { duration: 90 } }
                                MouseArea {
                                    id: dotMouse
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: App.homography.setPitchLandmark(view.pitchEditId,
                                                                               modelData.key)
                                }
                                // tooltip-ish label on hover
                                Rectangle {
                                    visible: dotMouse.containsMouse
                                    anchors.bottom: parent.top
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.bottomMargin: 3
                                    width: tipText.implicitWidth + 10
                                    height: 16
                                    radius: 3
                                    color: "#0d1f15"
                                    z: 5
                                    Text {
                                        id: tipText
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: "#cfe9db"
                                        font { family: Theme.fontUi; pixelSize: 9 }
                                    }
                                }
                            }
                        }

                        // A/B/C/D markers at their assigned landmark positions.
                        Repeater {
                            model: App.homography.points
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool active: view.pitchEditId === modelData.id
                                x: miniPitch.mapX(modelData.px) - width / 2
                                y: miniPitch.mapY(modelData.py) - height / 2
                                width: 16; height: 16; radius: 8
                                color: active ? Theme.greenBright : Theme.orange
                                border.color: "white"
                                border.width: active ? 2 : 1
                                z: 3
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.id
                                    color: "#241505"
                                    font { family: Theme.fontMono; pixelSize: 10; weight: Font.Bold }
                                }
                            }
                        }
                    }
                }

                // Point correspondences
                Column {
                    width: parent.width
                    spacing: 0

                    Item {
                        width: parent.width
                        height: 16
                        Text {
                            anchors.left: parent.left
                            text: "POINT CORRESPONDENCES"
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                        }
                        Text {
                            anchors.right: parent.right
                            text: "place · pick landmark"
                            color: Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }
                    }
                    Item { width: 1; height: 10 }

                    Repeater {
                        model: App.homography.points
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width
                            height: 50
                            radius: 6
                            color: view.selectedPointId === modelData.id ? "#1fffb46b" : "transparent"

                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width; height: 1
                                color: "#242830"
                            }

                            // Line 1: badge + image coords (click to place point)
                            Item {
                                id: line1
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                height: 26
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    spacing: 10
                                    Rectangle {
                                        width: 20; height: 20; radius: 5
                                        color: "#33ffb46b"
                                        border.color: Theme.orange
                                        border.width: 1
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text {
                                            text: modelData.id
                                            color: Theme.orange
                                            font { family: Theme.fontMono; pixelSize: 10; weight: Font.Bold }
                                            anchors.centerIn: parent
                                        }
                                    }
                                    Text {
                                        text: "img (" + Math.round(modelData.ix) + ", " + Math.round(modelData.iy) + ")"
                                        color: Theme.textMuted
                                        font { family: Theme.fontMono; pixelSize: 11 }
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                                Text {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: view.selectedPointId === modelData.id ? "placing…" : "place"
                                    color: view.selectedPointId === modelData.id ? Theme.greenBright : Theme.textFaint
                                    font { family: Theme.fontUi; pixelSize: 10; weight: Font.DemiBold }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: view.selectedPointId =
                                        view.selectedPointId === modelData.id ? "" : modelData.id
                                }
                            }

                            // Line 2: pitch-landmark chip (click to reassign)
                            Rectangle {
                                anchors.top: line1.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                height: 20
                                radius: 5
                                color: lmMouse.containsMouse ? Theme.surfaceHi : "transparent"
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 4
                                    anchors.right: parent.right
                                    anchors.rightMargin: 4
                                    spacing: 6
                                    Text {
                                        text: "↳"
                                        color: Theme.textFaint
                                        font.pixelSize: 11
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        width: parent.width - 34
                                        elide: Text.ElideRight
                                        text: modelData.landmark
                                        color: Theme.textMid
                                        font { family: Theme.fontMono; pixelSize: 10 }
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: "▾"
                                        color: Theme.textDim
                                        font.pixelSize: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                                MouseArea {
                                    id: lmMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: landmarkPicker.openFor(modelData.id)
                                }
                            }
                        }
                    }
                }

                // Reprojection error card
                Rectangle {
                    width: parent.width
                    height: 62
                    radius: 8
                    color: App.homography.verified ? "#2e1d3a2b" : "#26382d12"
                    border.color: App.homography.verified ? "#661d8f52" : "#66e0ac37"
                    border.width: 1

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        spacing: 10
                        Text {
                            text: App.homography.verified
                                ? App.homography.reprojError.toFixed(2) + "px" : "—"
                            color: App.homography.verified ? Theme.greenBright : Theme.yellow
                            font { family: Theme.fontMono; pixelSize: 22; weight: Font.Bold }
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "mean reprojection\nerror, this frame"
                            color: Theme.textMid
                            font { family: Theme.fontUi; pixelSize: 11 }
                            lineHeight: 1.3
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                // Calibration keyframes (per-frame homography via interpolation)
                Column {
                    width: parent.width
                    spacing: 6

                    Item {
                        width: parent.width
                        height: 16
                        Text {
                            anchors.left: parent.left
                            text: "KEYFRAMES · " + App.homography.keyframeCount
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                        }
                        Text {
                            anchors.right: parent.right
                            text: App.homography.atKeyframe ? "editing this frame" : "recompute to add"
                            color: Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 4

                        Repeater {
                            model: App.homography.keyframes
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool cur: modelData.frame === App.homography.currentFrame
                                width: parent.width
                                height: 30
                                radius: 6
                                color: cur ? "#1c2b22"
                                     : (kfMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi)
                                border.color: cur ? "#5930d980" : Theme.border2
                                border.width: 1

                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 9
                                    spacing: 8
                                    Rectangle {
                                        width: 6; height: 6; radius: 3
                                        color: Theme.orange
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: "frame " + modelData.frame
                                        color: Theme.textBright
                                        font { family: Theme.fontMono; pixelSize: 11; weight: Font.DemiBold }
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: modelData.reproj.toFixed(1) + "px"
                                        color: Theme.textDim
                                        font { family: Theme.fontMono; pixelSize: 10 }
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                                MouseArea {
                                    id: kfMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { App.pause(); App.seekFrame(modelData.frame) }
                                }
                                Text {
                                    text: "×"
                                    color: kfDelMouse.containsMouse ? Theme.red : Theme.textDim
                                    font.pixelSize: 14
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    MouseArea {
                                        id: kfDelMouse
                                        anchors.fill: parent
                                        anchors.margins: -5
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: App.homography.removeKeyframe(modelData.frame)
                                    }
                                }
                            }
                        }

                        Text {
                            visible: App.homography.keyframeCount === 0
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: "No keyframes yet. Place A–D on the video and "
                                  + "press “Set keyframe” to calibrate this frame."
                            color: Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 11 }
                            lineHeight: 1.3
                        }
                    }
                }

                // Overlay toggle
                Rectangle {
                    width: parent.width
                    height: 42
                    radius: 8
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    Text {
                        text: "Show pitch overlay"
                        color: Theme.text
                        font { family: Theme.fontUi; pixelSize: 13 }
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                    }
                    Rectangle {
                        width: 36; height: 20; radius: 10
                        color: App.homography.overlayEnabled ? Theme.green : "#2c313a"
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        Rectangle {
                            x: App.homography.overlayEnabled ? 18 : 2
                            y: 2
                            width: 16; height: 16; radius: 8
                            color: "white"
                            Behavior on x { NumberAnimation { duration: 150 } }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: App.homography.overlayEnabled = !App.homography.overlayEnabled
                    }
                }

                // Full pitch-model reprojection toggle + per-frame H export.
                Column {
                    width: parent.width
                    spacing: 8

                    Item {
                        width: parent.width
                        height: 16
                        Text {
                            anchors.left: parent.left
                            text: "MODEL REPROJECTION"
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                        }
                        Text {
                            anchors.right: parent.right
                            text: App.homography.verified ? "H ready" : "needs H"
                            color: App.homography.verified ? "#8fd7e6" : Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Projects the whole pitch (lines + reference points) "
                              + "onto the image with the inverse map, so you can see "
                              + "how well H fits the visible field."
                        color: Theme.textFaint
                        font { family: Theme.fontUi; pixelSize: 11 }
                        lineHeight: 1.3
                    }

                    // Overlay toggle.
                    Rectangle {
                        width: parent.width
                        height: 42
                        radius: 8
                        readonly property bool on: App.homography.modelOverlayEnabled
                        color: on ? "#12323a" : Theme.surface
                        border.color: on ? "#2f7d8f" : Theme.border
                        border.width: 1

                        Text {
                            text: "Reproject pitch model"
                            color: Theme.text
                            font { family: Theme.fontUi; pixelSize: 13 }
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                        }
                        Rectangle {
                            width: 36; height: 20; radius: 10
                            color: parent.on ? "#22c0d9" : "#2c313a"
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            Rectangle {
                                x: parent.parent.on ? 18 : 2
                                y: 2
                                width: 16; height: 16; radius: 8
                                color: "white"
                                Behavior on x { NumberAnimation { duration: 150 } }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: App.homography.modelOverlayEnabled = !App.homography.modelOverlayEnabled
                        }
                    }

                    // Export per-frame H to disk.
                    Rectangle {
                        width: parent.width
                        height: 36
                        radius: 8
                        readonly property bool on: App.videoLoaded && App.homography.keyframeCount >= 1
                        color: !on ? Theme.surfaceHi
                             : (exportMouse.containsMouse ? "#2f5c6b" : "#274c58")
                        border.color: "#3d7d8f"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: "Export H · all frames"
                            color: parent.on ? "#dff4fa" : Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 13; weight: Font.Bold }
                        }
                        MouseArea {
                            id: exportMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: parent.on
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: {
                                const path = App.exportHomographies()
                                view.exportResult = path !== ""
                                    ? "Saved " + (App.totalFrames) + " frames → homographies.json"
                                    : "Export failed: " + App.lastError
                            }
                        }
                    }

                    Text {
                        visible: view.exportResult !== ""
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: view.exportResult
                        color: view.exportResult.indexOf("failed") >= 0 ? Theme.red : "#8fd7a6"
                        font { family: Theme.fontMono; pixelSize: 10 }
                    }
                }

                // Actions
                Row {
                    width: parent.width
                    spacing: 8

                    Rectangle {
                        width: (parent.width - 8) / 2
                        height: 36
                        radius: 8
                        color: recomputeMouse.containsMouse ? Theme.greenBright : Theme.green
                        Text {
                            text: "Set keyframe"
                            color: "#10231a"
                            font { family: Theme.fontUi; pixelSize: 13; weight: Font.Bold }
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            id: recomputeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: App.homography.recompute()
                        }
                    }
                    Rectangle {
                        width: (parent.width - 8) / 2
                        height: 36
                        radius: 8
                        color: resetMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
                        border.color: Theme.border2
                        border.width: 1
                        Text {
                            text: "Reset points"
                            color: Theme.text
                            font { family: Theme.fontUi; pixelSize: 13; weight: Font.DemiBold }
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            id: resetMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { view.selectedPointId = ""; App.homography.reset() }
                        }
                    }
                }

                // Auto-calibration by pitch lines (phase F3): snaps the current
                // (manual/interpolated) H to the detected white lines and stores
                // a refined keyframe at this frame.
                Rectangle {
                    width: parent.width
                    height: 36
                    radius: 8
                    readonly property bool on: App.videoLoaded && App.homography.verified
                    color: !on ? Theme.surfaceHi
                         : (autoCalMouse.containsMouse ? "#6b4a12" : "#5c3f10")
                    border.color: "#9c7a2e"
                    border.width: 1
                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        Text {
                            text: "▚"
                            color: parent.parent.on ? "#ffcf7a" : Theme.textFaint
                            font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "Auto-calibrate (lines)"
                            color: parent.parent.on ? "#ffe6bd" : Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 13; weight: Font.Bold }
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    MouseArea {
                        id: autoCalMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: parent.on
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: App.autoCalibrateHomography()
                    }
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Refines this frame's H by fitting the detected pitch "
                          + "lines. Place A–D roughly (or land on a keyframe) first "
                          + "so it has a starting point."
                    color: Theme.textFaint
                    font { family: Theme.fontUi; pixelSize: 11 }
                    lineHeight: 1.3
                }

                // ---- inter-frame propagation (phase F2, optical flow) ----
                Column {
                    width: parent.width
                    spacing: 8

                    Item {
                        width: parent.width
                        height: 16
                        Text {
                            anchors.left: parent.left
                            text: "PROPAGATION · FLOW"
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                        }
                        Text {
                            anchors.right: parent.right
                            text: App.homography.propagated ? "dense · active"
                                : App.homography.keyframeCount >= 2 ? "ready" : "needs 2 KF"
                            color: App.homography.propagated ? "#8fbfe6" : Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Estimates the real camera motion between keyframes "
                              + "(LK optical flow + RANSAC) and fills a homography "
                              + "for every frame in the span."
                        color: Theme.textFaint
                        font { family: Theme.fontUi; pixelSize: 11 }
                        lineHeight: 1.3
                    }

                    // Progress bar (visible while running).
                    Rectangle {
                        visible: App.homography.propagating
                        width: parent.width
                        height: 26
                        radius: 6
                        color: Theme.surfaceHi
                        border.color: Theme.border2
                        border.width: 1
                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 2
                            width: (parent.width - 4) * App.homography.propProgress
                            height: parent.height - 4
                            radius: 5
                            color: "#2f4a6b"
                        }
                        Text {
                            anchors.centerIn: parent
                            text: App.homography.propLabel + "  ·  "
                                  + Math.round(App.homography.propProgress * 100) + "%"
                            color: "#d7e6ff"
                            font { family: Theme.fontMono; pixelSize: 10 }
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: 8

                        Rectangle {
                            width: App.homography.propagated ? (parent.width - 8) / 2 : parent.width
                            height: 36
                            radius: 8
                            readonly property bool on: App.homography.keyframeCount >= 2
                                                       && !App.homography.propagating
                            color: !on ? Theme.surfaceHi
                                 : (propMouse.containsMouse ? "#3d5c82" : "#33507a")
                            border.color: "#4d6f9c"
                            border.width: 1
                            Text {
                                text: App.homography.propagating ? "Propagating…"
                                    : App.homography.propagated ? "Re-run flow" : "Propagate (flow)"
                                color: parent.on ? "#e6f0ff" : Theme.textFaint
                                font { family: Theme.fontUi; pixelSize: 13; weight: Font.Bold }
                                anchors.centerIn: parent
                            }
                            MouseArea {
                                id: propMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: parent.on
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: App.propagateHomography()
                            }
                        }
                        Rectangle {
                            visible: App.homography.propagated
                            width: (parent.width - 8) / 2
                            height: 36
                            radius: 8
                            color: clearPropMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
                            border.color: Theme.border2
                            border.width: 1
                            Text {
                                text: "Clear"
                                color: Theme.text
                                font { family: Theme.fontUi; pixelSize: 13; weight: Font.DemiBold }
                                anchors.centerIn: parent
                            }
                            MouseArea {
                                id: clearPropMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.homography.clearPropagation()
                            }
                        }
                    }
                }

                // ---- shot segmentation (phase F4, feed_tv) ----
                Column {
                    width: parent.width
                    spacing: 8

                    Item {
                        width: parent.width
                        height: 16
                        Text {
                            anchors.left: parent.left
                            text: "SHOTS · CUTS"
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                        }
                        Text {
                            anchors.right: parent.right
                            text: App.hasShots ? App.shotCount + " shots"
                                : App.shotDetecting ? "detecting…" : "none"
                            color: App.hasShots ? "#8fbfa6" : Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Detects camera cuts and flags non-pitch shots "
                              + "(close-ups / graphics). Tags on non-pitch frames "
                              + "get no pitch coordinates."
                        color: Theme.textFaint
                        font { family: Theme.fontUi; pixelSize: 11 }
                        lineHeight: 1.3
                    }

                    // Current-frame shot status.
                    Rectangle {
                        visible: App.hasShots
                        width: parent.width
                        height: 26
                        radius: 6
                        color: App.pitchVisible ? "#16321f" : "#331a16"
                        border.color: App.pitchVisible ? "#2f6b45" : "#7a3128"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: App.pitchVisible ? "◉ pitch visible · H valid here"
                                                   : "⊘ non-pitch shot · H invalid here"
                            color: App.pitchVisible ? "#9fe0b8" : "#ffb59e"
                            font { family: Theme.fontMono; pixelSize: 10 }
                        }
                    }

                    // Progress bar (visible while detecting).
                    Rectangle {
                        visible: App.shotDetecting
                        width: parent.width
                        height: 26
                        radius: 6
                        color: Theme.surfaceHi
                        border.color: Theme.border2
                        border.width: 1
                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 2
                            width: (parent.width - 4) * App.shotProgress
                            height: parent.height - 4
                            radius: 5
                            color: "#2f6b45"
                        }
                        Text {
                            anchors.centerIn: parent
                            text: App.shotLabel + "  ·  " + Math.round(App.shotProgress * 100) + "%"
                            color: "#d7f0e0"
                            font { family: Theme.fontMono; pixelSize: 10 }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 36
                        radius: 8
                        readonly property bool on: App.videoLoaded && !App.shotDetecting
                        color: !on ? Theme.surfaceHi
                             : (shotMouse.containsMouse ? "#2f6b45" : "#295c3c")
                        border.color: "#3d7d54"
                        border.width: 1
                        Text {
                            text: App.shotDetecting ? "Detecting shots…"
                                : App.hasShots ? "Re-detect shots" : "Detect shots"
                            color: parent.on ? "#e6fff0" : Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 13; weight: Font.Bold }
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            id: shotMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: parent.on
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: App.detectShots()
                        }
                    }
                }

                // ---- graphics mask regions (phase F5, feed_tv) ----
                Column {
                    width: parent.width
                    spacing: 8

                    Item {
                        width: parent.width
                        height: 16
                        Text {
                            anchors.left: parent.left
                            text: "GRAPHICS MASK"
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                        }
                        Text {
                            anchors.right: parent.right
                            text: App.homography.graphicsCount + " region(s)"
                            color: App.homography.graphicsCount > 0 ? "#ffb59e" : Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Static on-screen overlays (scoreboard, logos) are "
                              + "fixed in screen space and corrupt the camera-motion "
                              + "estimate. Mark them to exclude from the flow."
                        color: Theme.textFaint
                        font { family: Theme.fontUi; pixelSize: 11 }
                        lineHeight: 1.3
                    }

                    Row {
                        width: parent.width
                        spacing: 8

                        Rectangle {
                            width: App.homography.graphicsCount > 0 ? (parent.width - 8) / 2 : parent.width
                            height: 34
                            radius: 8
                            readonly property bool on: App.videoLoaded
                            color: !on ? Theme.surfaceHi
                                 : view.graphicsMode ? Theme.orange
                                 : (gfxDrawMouse.containsMouse ? "#6b4a12" : "#5c3f10")
                            border.color: "#9c7a2e"
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: view.graphicsMode ? "Done drawing" : "Draw region"
                                color: view.graphicsMode ? "#241505"
                                     : parent.on ? "#ffe6bd" : Theme.textFaint
                                font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                            }
                            MouseArea {
                                id: gfxDrawMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: parent.on
                                cursorShape: Qt.PointingHandCursor
                                onClicked: view.graphicsMode = !view.graphicsMode
                            }
                        }
                        Rectangle {
                            visible: App.homography.graphicsCount > 0
                            width: (parent.width - 8) / 2
                            height: 34
                            radius: 8
                            color: gfxClearMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
                            border.color: Theme.border2
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: "Clear all"
                                color: Theme.text
                                font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                            }
                            MouseArea {
                                id: gfxClearMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { view.graphicsMode = false; App.homography.clearGraphics() }
                            }
                        }
                    }

                    Text {
                        visible: view.graphicsMode
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Drag a rectangle over the graphic on the video."
                        color: Theme.orange
                        font { family: Theme.fontUi; pixelSize: 11; weight: Font.DemiBold }
                    }
                }

                // ---- homography solver backend selector ----
                Column {
                    width: parent.width
                    spacing: 6

                    Item {
                        width: parent.width
                        height: 16
                        Text {
                            anchors.left: parent.left
                            text: "SOLVER"
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
                        }
                        Text {
                            anchors.right: parent.right
                            text: "RANSAC engine"
                            color: Theme.textFaint
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: [{ id: "opencv", label: "OpenCV" }, { id: "custom", label: "Custom" }]
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool sel: App.solverBackend === modelData.id
                                width: (parent.width - 6) / 2
                                height: 30
                                radius: 6
                                color: sel ? Theme.green
                                     : (slvMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi)
                                border.color: sel ? Theme.green : Theme.border2
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: sel ? "#10231a" : Theme.text
                                    font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                                }
                                MouseArea {
                                    id: slvMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: App.solverBackend = modelData.id
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "OpenCV uses cv::findHomography; Custom uses the "
                              + "in-house normalized-DLT + RANSAC (fully controllable)."
                        color: Theme.textFaint
                        font { family: Theme.fontUi; pixelSize: 11 }
                        lineHeight: 1.3
                    }
                }
                }
            }
        }
    }

    // ---- pitch-landmark picker (per reference point) ----
    Popup {
        id: landmarkPicker
        property string pointId: ""
        function openFor(id) { pointId = id; open() }

        width: 320
        height: Math.min(460, view.height - 40)
        x: Math.round((view.width - width) / 2)
        y: Math.round((view.height - height) / 2)
        modal: true
        dim: true
        padding: 0

        background: Rectangle {
            color: "#1b1f26"
            border.color: Theme.borderHi
            border.width: 1
            radius: 10
        }

        contentItem: Item {

            Item {
                id: pickerHeader
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 40
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: "PITCH LANDMARK · POINT " + landmarkPicker.pointId
                    color: Theme.textBright
                    font { family: Theme.fontUi; pixelSize: 12; weight: Font.Bold; letterSpacing: 0.5 }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width; height: 1; color: Theme.border
                }
            }

            ListView {
                anchors.top: pickerHeader.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                clip: true
                model: App.homography.pitchLandmarks()
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool cur: view.pointById(landmarkPicker.pointId)
                        && Math.abs(view.pointById(landmarkPicker.pointId).px - modelData.px) < 0.05
                        && Math.abs(view.pointById(landmarkPicker.pointId).py - modelData.py) < 0.05
                    width: ListView.view.width
                    height: 34
                    color: cur ? "#1c2b22" : (lmRowMouse.containsMouse ? Theme.surfaceHi : "transparent")
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        spacing: 8
                        Text {
                            text: cur ? "✓" : " "
                            color: Theme.green
                            font { family: Theme.fontMono; pixelSize: 11 }
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            width: parent.width - 120
                            elide: Text.ElideRight
                            text: modelData.label
                            color: cur ? Theme.greenBright : Theme.text
                            font { family: Theme.fontUi; pixelSize: 12 }
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "(" + modelData.px + ", " + modelData.py + ")"
                            color: Theme.textDim
                            font { family: Theme.fontMono; pixelSize: 10 }
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    MouseArea {
                        id: lmRowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            App.homography.setPitchLandmark(landmarkPicker.pointId, modelData.key)
                            landmarkPicker.close()
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }
}
