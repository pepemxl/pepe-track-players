import QtQuick
import QtQuick.Controls

// Section 3 — match details form + editable rosters for both teams.
Flickable {
    id: view
    contentHeight: content.height + 56
    clip: true

    // Emitted after the project is deleted so the shell can leave this tab.
    signal requestVideoTab()

    Column {
        id: content
        x: 32
        y: 28
        width: parent.width - 64
        spacing: 24

        // ---- project (rename / delete) ----
        Column {
            width: parent.width
            spacing: 12
            visible: App.videoLoaded && App.match.registered

            Text {
                text: "PROJECT"
                color: Theme.textDim
                font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
            }

            Row {
                width: parent.width
                spacing: 14

                Column {
                    width: parent.width - 170 - 14
                    spacing: 6
                    Text {
                        text: "Project name"
                        color: Theme.textDim
                        font { family: Theme.fontUi; pixelSize: 11 }
                    }
                    EditField {
                        id: nameField
                        width: Math.min(parent.width, 520)
                        text: App.match.matchName
                        placeholderText: "Project #" + App.match.matchId
                        onEditingFinished: App.match.renameProject(text)
                    }
                }

                // Delete, bottom-aligned with the name field.
                Item {
                    width: 170
                    height: 26 + nameField.height
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: nameField.height
                        radius: 8
                        color: delMouse.containsMouse ? "#3a1a17" : "transparent"
                        border.color: Theme.red
                        border.width: 1
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            Text {
                                text: "🗑"
                                color: Theme.red
                                font.pixelSize: 13
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "Delete project"
                                color: Theme.red
                                font { family: Theme.fontUi; pixelSize: 13; weight: Font.DemiBold }
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        MouseArea {
                            id: delMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: deleteDialog.open()
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.border }
        }

        // ---- match details ----
        Column {
            width: parent.width
            spacing: 12

            Text {
                text: "MATCH DETAILS"
                color: Theme.textDim
                font { family: Theme.fontUi; pixelSize: 11; weight: Font.Bold; letterSpacing: 0.5 }
            }

            Grid {
                columns: 4
                columnSpacing: 14
                rowSpacing: 14
                width: parent.width

                Repeater {
                    model: [
                        { label: "League",      key: "league" },
                        { label: "Season",      key: "season" },
                        { label: "Competition", key: "competition" },
                        { label: "Date",        key: "date" },
                        { label: "Venue",       key: "venue" },
                        { label: "Referee",     key: "referee" },
                    ]
                    delegate: Column {
                        required property var modelData
                        width: (content.width - 3 * 14) / 4
                        spacing: 6
                        Text {
                            text: modelData.label
                            color: Theme.textDim
                            font { family: Theme.fontUi; pixelSize: 11 }
                        }
                        EditField {
                            width: parent.width
                            text: App.metadata[modelData.key]
                            onEditingFinished: App.metadata[modelData.key] = text
                        }
                    }
                }
            }
        }

        // ---- rosters ----
        Row {
            width: parent.width
            spacing: 24

            RosterTable {
                width: (parent.width - 24) / 2
                roster: App.homeRoster
                teamColor: Theme.homeColor
                teamName: App.metadata.homeTeam
                onTeamNameEdited: (name) => App.metadata.homeTeam = name
            }

            RosterTable {
                width: (parent.width - 24) / 2
                roster: App.awayRoster
                teamColor: Theme.awayColor
                teamName: App.metadata.awayTeam
                onTeamNameEdited: (name) => App.metadata.awayTeam = name
            }
        }
    }

    // ---- delete confirmation ----
    Dialog {
        id: deleteDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 460
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface
            radius: 12
            border.color: Theme.border
            border.width: 1
        }

        contentItem: Column {
            spacing: 0

            Text {
                width: parent.width
                padding: 22
                bottomPadding: 10
                text: "Delete project?"
                color: Theme.text
                font { family: Theme.fontUi; pixelSize: 16; weight: Font.Bold }
            }
            Text {
                width: parent.width
                leftPadding: 22; rightPadding: 22; bottomPadding: 20
                wrapMode: Text.WordWrap
                text: "This permanently deletes “" + App.match.matchName
                    + (App.match.matchName.length ? "”" : "Project #" + App.match.matchId)
                    + " and all of its data — video chunks, feature masks, "
                    + "tracking, homography, tags and metadata. This cannot be undone."
                color: Theme.textMuted
                font { family: Theme.fontUi; pixelSize: 13 }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.border }

            Row {
                width: parent.width
                layoutDirection: Qt.RightToLeft
                padding: 16
                spacing: 10

                Rectangle {
                    width: delConfirmText.implicitWidth + 32; height: 38; radius: 8
                    color: Theme.red
                    Text {
                        id: delConfirmText
                        anchors.centerIn: parent
                        text: "Delete project"
                        color: "white"
                        font { family: Theme.fontUi; pixelSize: 13; weight: Font.Bold }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            deleteDialog.close()
                            App.deleteProject()
                            view.requestVideoTab()
                        }
                    }
                }
                Rectangle {
                    width: cancelText.implicitWidth + 32; height: 38; radius: 8
                    color: Theme.surfaceHi
                    border.color: Theme.border2; border.width: 1
                    Text {
                        id: cancelText
                        anchors.centerIn: parent
                        text: "Cancel"
                        color: Theme.text
                        font { family: Theme.fontUi; pixelSize: 13; weight: Font.DemiBold }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: deleteDialog.close()
                    }
                }
            }
        }
    }
}
