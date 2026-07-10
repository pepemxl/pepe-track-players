import QtQuick
import QtQuick.Controls

// "Assign player" popup shown when the user clicks the video to tag.
// Lists both rosters; emits playerPicked(team, rosterRow).
Popup {
    id: dropdown
    width: 230
    height: Math.min(300, headerItem.height + list.contentHeight + 24)
    padding: 8

    property string title: "TAG PLAYER"
    signal playerPicked(int team, int rosterRow)

    background: Rectangle {
        color: "#1b1f26"
        border.color: Theme.borderHi
        border.width: 1
        radius: 10
    }

    contentItem: Column {
        spacing: 4

        Text {
            id: headerItem
            text: dropdown.title
            color: Theme.textFaint
            font { family: Theme.fontUi; pixelSize: 10.5; weight: Font.Bold; letterSpacing: 0.5 }
            padding: 4
        }

        ListView {
            id: list
            width: parent.width
            height: dropdown.height - headerItem.height - 24
            clip: true
            spacing: 2

            // Home roster first, then away: flatten via a combined count.
            model: App.homeRoster.count + App.awayRoster.count

            delegate: Rectangle {
                required property int index
                readonly property bool isHome: index < App.homeRoster.count
                readonly property int rosterRow: isHome ? index : index - App.homeRoster.count
                readonly property var player: isHome ? App.homeRoster.get(rosterRow)
                                                     : App.awayRoster.get(rosterRow)
                width: list.width
                height: 34
                radius: 6
                color: rowMouse.containsMouse ? Theme.surfaceHi : "transparent"

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    spacing: 8
                    Rectangle {
                        width: 22; height: 22; radius: 5
                        color: isHome ? Theme.homeColor : Theme.awayColor
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            text: player.number
                            color: "white"
                            font { family: Theme.fontMono; pixelSize: 10.5; weight: Font.Bold }
                            anchors.centerIn: parent
                        }
                    }
                    Text {
                        text: player.name
                        color: Theme.text
                        font { family: Theme.fontUi; pixelSize: 12.5 }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        dropdown.playerPicked(isHome ? 0 : 1, rosterRow)
                        dropdown.close()
                    }
                }
            }
        }
    }
}
