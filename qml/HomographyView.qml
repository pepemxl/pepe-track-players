import QtQuick
import QtQuick.Controls

// Section 2 — pitch calibration. Select a corner (A/B/C/D) in the right
// panel, click the video to place it, then Recompute H (cv::findHomography).
Item {
    id: view

    property string selectedPointId: ""

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

                    // Verify badge (top-right)
                    Rectangle {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 12
                        width: verifyRow.implicitWidth + 20; height: 24; radius: 6
                        color: App.homography.verified ? "#d91d3a2b" : "#d93a2f16"
                        Row {
                            id: verifyRow
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle {
                                width: 6; height: 6; radius: 3
                                color: App.homography.verified ? Theme.greenBright : Theme.yellow
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: App.homography.atKeyframe
                                        ? "KEYFRAME · FRAME " + App.homography.currentFrame
                                    : App.homography.verified
                                        ? "INTERPOLATED · " + App.homography.keyframeCount + " KF"
                                        : "NEEDS RECOMPUTE"
                                color: App.homography.verified ? "#e6f5ec" : "#f5ecd0"
                                font { family: Theme.fontMono; pixelSize: 11; weight: Font.DemiBold }
                                anchors.verticalCenter: parent.verticalCenter
                            }
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

            Column {
                anchors.fill: parent
                anchors.margins: 18
                anchors.topMargin: 20
                spacing: 16

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
                            text: "click to select"
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
            }
        }
    }
}
