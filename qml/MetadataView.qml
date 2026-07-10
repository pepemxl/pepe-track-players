import QtQuick
import QtQuick.Controls

// Section 3 — match details form + editable rosters for both teams.
Flickable {
    id: view
    contentHeight: content.height + 56
    clip: true

    Column {
        id: content
        x: 32
        y: 28
        width: parent.width - 64
        spacing: 24

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
}
