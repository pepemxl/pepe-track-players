import QtQuick
import QtQuick.Controls

// Editable roster table for one team: # / NAME / POSITION / delete,
// plus the "+ Add player" footer row.
Column {
    id: table

    property var roster          // RosterModel
    property color teamColor: Theme.homeColor
    property string teamName: ""
    signal teamNameEdited(string name)

    spacing: 10

    Row {
        spacing: 10
        Rectangle {
            width: 10; height: 10; radius: 3
            color: table.teamColor
            anchors.verticalCenter: parent.verticalCenter
        }
        EditField {
            text: table.teamName
            boxed: false
            font { family: Theme.fontUi; pixelSize: 13; weight: Font.Bold }
            width: Math.max(120, implicitWidth)
            anchors.verticalCenter: parent.verticalCenter
            onEditingFinished: table.teamNameEdited(text)
        }
        Text {
            text: table.roster.count + " players"
            color: Theme.textDim
            font { family: Theme.fontMono; pixelSize: 11 }
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Rectangle {
        width: parent.width
        height: header.height + list.contentHeight + addRow.height
        radius: 10
        color: "transparent"
        border.color: Theme.border
        border.width: 1
        clip: true

        Column {
            anchors.fill: parent

            Row {
                id: header
                width: parent.width
                height: 32
                Rectangle {
                    width: parent.width; height: parent.height
                    color: Theme.surface2
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        Repeater {
                            model: [
                                { w: 56,                     t: "#" },
                                { w: header.width - 210 - 14, t: "NAME" },
                                { w: 100,                    t: "POSITION" },
                            ]
                            delegate: Text {
                                required property var modelData
                                width: modelData.w
                                text: modelData.t
                                color: Theme.textDim
                                font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.4 }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }

            ListView {
                id: list
                width: parent.width
                height: contentHeight
                interactive: false
                model: table.roster

                delegate: Rectangle {
                    required property int index
                    required property int number
                    required property string name
                    required property string position

                    // A zero number means the id inference could not read the
                    // shirt number — flag the row in amber so it's easy to fix.
                    readonly property bool unidentified: number === 0

                    width: list.width
                    height: 40
                    color: unidentified ? "#26e0ac37" : "transparent"

                    Rectangle { width: parent.width; height: 1; color: "#242830" }

                    // Left accent bar on unidentified rows.
                    Rectangle {
                        visible: parent.unidentified
                        width: 3; height: parent.height
                        anchors.left: parent.left
                        color: Theme.yellow
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 14

                        EditField {
                            width: 56
                            boxed: false
                            text: number
                            textColor: unidentified ? Theme.yellow : table.teamColor
                            font { family: Theme.fontMono; pixelSize: 13; weight: Font.Bold }
                            anchors.verticalCenter: parent.verticalCenter
                            onEditingFinished: table.roster.set(index, "number", parseInt(text) || 0)
                        }
                        EditField {
                            width: parent.width - 210 - 14
                            boxed: false
                            text: name
                            anchors.verticalCenter: parent.verticalCenter
                            onEditingFinished: table.roster.set(index, "name", text)
                        }
                        EditField {
                            width: 100
                            boxed: false
                            text: position
                            textColor: Theme.textMuted
                            font { family: Theme.fontUi; pixelSize: 12 }
                            anchors.verticalCenter: parent.verticalCenter
                            onEditingFinished: table.roster.set(index, "position", text)
                        }
                        Text {
                            width: 40
                            text: "×"
                            color: delMouse.containsMouse ? Theme.red : Theme.textDim
                            font.pixelSize: 15
                            horizontalAlignment: Text.AlignHCenter
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                id: delMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: table.roster.removePlayer(index)
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: addRow
                width: parent.width
                height: 38
                color: addMouse.containsMouse ? Theme.surface : "transparent"
                Rectangle { width: parent.width; height: 1; color: "#242830" }
                Text {
                    text: "+ Add player"
                    color: Theme.greenBright
                    font { family: Theme.fontUi; pixelSize: 13; weight: Font.DemiBold }
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                }
                MouseArea {
                    id: addMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: table.roster.addPlayer()
                }
            }
        }
    }
}
