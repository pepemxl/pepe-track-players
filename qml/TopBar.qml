import QtQuick
import QtQuick.Controls

// Top bar: logo, match label, open button, save status chip, avatar.
Rectangle {
    id: bar
    height: 56
    color: Theme.bgDark

    signal openRequested()
    signal saveRequested()

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 20
        spacing: 16

        Row {
            spacing: 9
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                width: 22; height: 22; radius: 6
                color: Theme.green
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    width: 8; height: 8; radius: 2
                    color: Theme.bgDark
                    anchors.centerIn: parent
                }
            }
            Text {
                text: "PepeTrack"
                color: Theme.text
                font { family: Theme.fontUi; pixelSize: 15; weight: Font.ExtraBold; letterSpacing: 0.2 }
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Rectangle { width: 1; height: 22; color: Theme.border; anchors.verticalCenter: parent.verticalCenter }

        Text {
            text: App.metadata.homeTeam + " vs " + App.metadata.awayTeam + " · " + App.metadata.competition
            color: Theme.textMuted
            font { family: Theme.fontMono; pixelSize: 13 }
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            visible: App.videoLoaded
            text: App.videoName
            color: Theme.textDim
            font { family: Theme.fontMono; pixelSize: 12 }
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 20
        spacing: 12

        // Open video
        Rectangle {
            width: openLabel.implicitWidth + 24; height: 30; radius: 8
            color: openMouse.containsMouse ? Theme.borderHi : Theme.surfaceHi
            anchors.verticalCenter: parent.verticalCenter
            Text {
                id: openLabel
                text: "Open video"
                color: Theme.textBright
                font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                anchors.centerIn: parent
            }
            MouseArea {
                id: openMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.openRequested()
            }
        }

        // Save status chip (click to save)
        Rectangle {
            width: saveRow.implicitWidth + 20; height: 28; radius: 999
            color: App.dirty ? "#26e0ac37" : "#2630d980"
            border.color: App.dirty ? "#80e0ac37" : "#8030d980"
            border.width: 1
            anchors.verticalCenter: parent.verticalCenter
            Row {
                id: saveRow
                anchors.centerIn: parent
                spacing: 8
                Rectangle {
                    width: 7; height: 7; radius: 3.5
                    color: App.dirty ? Theme.yellow : Theme.green
                    anchors.verticalCenter: parent.verticalCenter
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { from: 1; to: 0.4; duration: 900; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 0.4; to: 1; duration: 900; easing.type: Easing.InOutSine }
                    }
                }
                Text {
                    text: App.dirty ? "UNSAVED CHANGES" : "PROJECT SAVED"
                    color: App.dirty ? Theme.yellow : Theme.greenBright
                    font { family: Theme.fontMono; pixelSize: 11.5; weight: Font.DemiBold }
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.saveRequested()
            }
        }

        // Avatar
        Rectangle {
            width: 30; height: 30; radius: 8
            color: Theme.surfaceHi
            anchors.verticalCenter: parent.verticalCenter
            Text {
                text: "JA"
                color: Theme.textMuted
                font { family: Theme.fontUi; pixelSize: 12; weight: Font.Bold }
                anchors.centerIn: parent
            }
        }
    }
}
