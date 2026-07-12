import QtQuick

// Compact frame transport: step / play-pause / step, timecode, scrubber and a
// "frame N / total" counter. Drives the shared video engine through App.
Row {
    id: transport
    height: 34
    spacing: 12
    enabled: App.videoLoaded
    opacity: App.videoLoaded ? 1 : 0.4

    Rectangle {
        width: 34; height: 34; radius: 8
        color: prevMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
        anchors.verticalCenter: parent.verticalCenter
        Text { anchors.centerIn: parent; text: "◀"; color: Theme.textBright; font.pixelSize: 12 }
        MouseArea { id: prevMouse; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: App.stepFrames(-1) }
    }

    Rectangle {
        width: 34; height: 34; radius: 8
        color: playMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
        anchors.verticalCenter: parent.verticalCenter
        Row {
            visible: App.playing
            anchors.centerIn: parent; spacing: 3
            Rectangle { width: 3; height: 12; color: Theme.textBright }
            Rectangle { width: 3; height: 12; color: Theme.textBright }
        }
        Text { visible: !App.playing; anchors.centerIn: parent
               text: "▶"; color: Theme.textBright; font.pixelSize: 13 }
        MouseArea { id: playMouse; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: App.togglePlay() }
    }

    Rectangle {
        width: 34; height: 34; radius: 8
        color: nextMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
        anchors.verticalCenter: parent.verticalCenter
        Text { anchors.centerIn: parent; text: "▶▏"; color: Theme.textBright; font.pixelSize: 11 }
        MouseArea { id: nextMouse; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: App.stepFrames(1) }
    }

    Text {
        id: tcLeft
        text: App.timecode(App.positionSec)
        color: Theme.textMid
        font { family: Theme.fontMono; pixelSize: 12 }
        anchors.verticalCenter: parent.verticalCenter
    }

    Item {
        width: transport.width - 34 * 3 - 12 * 5 - tcLeft.width - frameText.width
        height: 34
        anchors.verticalCenter: parent.verticalCenter
        Rectangle {
            id: ftrack
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width; height: 6; radius: 3
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
