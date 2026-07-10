import QtQuick

// Shared video canvas: shows the latest frame from the OpenCV worker via
// the image provider, with the striped placeholder from the design when
// nothing is loaded. Exposes the painted geometry so overlays can map
// video pixel coordinates onto the screen.
Rectangle {
    id: surface
    radius: 10
    color: "#181b21"
    border.color: Theme.border
    border.width: 1
    clip: true

    // Geometry of the actually painted (letterboxed) frame.
    readonly property real paintedX: (width - paintedWidth) / 2
    readonly property real paintedY: (height - paintedHeight) / 2
    readonly property real paintedWidth: frame.status === Image.Ready && App.videoLoaded
        ? frame.paintedWidth : width
    readonly property real paintedHeight: frame.status === Image.Ready && App.videoLoaded
        ? frame.paintedHeight : height
    readonly property real videoScale: App.videoWidth > 0 ? paintedWidth / App.videoWidth : 1

    function toVideoX(mouseX) { return (mouseX - paintedX) / videoScale }
    function toVideoY(mouseY) { return (mouseY - paintedY) / videoScale }
    function fromVideoX(vx) { return paintedX + vx * videoScale }
    function fromVideoY(vy) { return paintedY + vy * videoScale }
    function insidePainted(mx, my) {
        return mx >= paintedX && mx <= paintedX + paintedWidth
            && my >= paintedY && my <= paintedY + paintedHeight
    }

    // Diagonal-stripe placeholder (design's "VIDEO PLACEHOLDER" background).
    Canvas {
        anchors.fill: parent
        visible: !App.videoLoaded
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = "#20242c"
            ctx.lineWidth = 10
            for (let x = -height; x < width; x += 20) {
                ctx.beginPath()
                ctx.moveTo(x, height)
                ctx.lineTo(x + height, 0)
                ctx.stroke()
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    Image {
        id: frame
        anchors.fill: parent
        visible: App.videoLoaded
        fillMode: Image.PreserveAspectFit
        cache: false
        asynchronous: false
        source: App.videoLoaded ? "image://videoframe/" + App.frameSerial : ""
    }

    Text {
        visible: !App.videoLoaded
        anchors.centerIn: parent
        text: "Open a video to start  (Ctrl+O)"
        color: Theme.textDim
        font { family: Theme.fontMono; pixelSize: 13 }
    }

    // Top-left info badge
    Rectangle {
        x: 12; y: 12
        width: infoText.implicitWidth + 18; height: 24; radius: 6
        color: Theme.overlayBg
        Text {
            id: infoText
            anchors.centerIn: parent
            text: App.videoLoaded
                ? App.videoWidth + "×" + App.videoHeight + " · " + App.fps.toFixed(0) + "fps"
                : "— · —"
            color: Theme.textBright
            font { family: Theme.fontMono; pixelSize: 11 }
        }
    }
}
