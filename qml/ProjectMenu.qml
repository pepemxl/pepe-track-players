import QtQuick
import QtQuick.Controls

// Projects popup: open a registered project, switch between the current
// project's videos, or add a new video to it with a camera role.
Popup {
    id: menu
    width: 340
    padding: 10

    // Snapshot of games.json, refreshed every time the menu opens.
    property var projects: []
    onAboutToShow: projects = App.match.listProjects()

    readonly property var roles: [
        { id: "tv_feed",   label: "Feed de TV" },
        { id: "tactical",  label: "Cámara táctica" },
        { id: "panoramic", label: "Cámara panorámica" },
        { id: "other",     label: "Otra" },
    ]

    readonly property var segments: [
        { id: "full",                label: "Video completo" },
        { id: "first_half",         label: "Primer tiempo" },
        { id: "second_half",        label: "Segundo tiempo" },
        { id: "extra1",             label: "Primer tiempo extra" },
        { id: "extra2",             label: "Segundo tiempo extra" },
        { id: "penalties",          label: "Penales" },
        { id: "partial_first_half",  label: "Parcial primer tiempo" },
        { id: "partial_second_half", label: "Parcial segundo tiempo" },
    ]

    // Two-step add flow: pick the camera role, then the match segment.
    // The file dialog lives in TopBar (outside this popup): opening it
    // while the popup closes is unreliable.
    signal addVideoRequested(string role, string segment)
    property string pendingRole: ""
    onClosed: pendingRole = ""

    function roleLabel(id) {
        for (const r of roles)
            if (r.id === id) return r.label
        return id
    }

    function segmentLabel(id) {
        for (const s of segments)
            if (s.id === id) return s.label
        return id
    }

    background: Rectangle {
        color: "#1b1f26"
        border.color: Theme.borderHi
        border.width: 1
        radius: 10
    }

    contentItem: Column {
        spacing: 6

        // ---- registered projects ----
        Text {
            text: "PROJECTS"
            color: Theme.textFaint
            font { family: Theme.fontUi; pixelSize: 10; weight: Font.Bold; letterSpacing: 0.5 }
            padding: 2
        }

        // New empty project: the first video is added afterwards through
        // the "ADD VIDEO AS…" role entries below.
        Rectangle {
            width: parent.width
            height: 30
            radius: 6
            color: newProjMouse.containsMouse ? Theme.surfaceHi : "transparent"
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 8
                text: "＋ New project"
                color: Theme.greenBright
                font { family: Theme.fontUi; pixelSize: 12; weight: Font.Bold }
            }
            MouseArea {
                id: newProjMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    const id = App.match.createProject()
                    menu.projects = App.match.listProjects()
                }
            }
        }

        Repeater {
            model: menu.projects
            delegate: Rectangle {
                required property var modelData
                readonly property var firstVideo: modelData.videos.length > 0
                    ? modelData.videos[0] : null
                width: parent.width
                height: 34
                radius: 6
                color: projMouse.containsMouse ? Theme.surfaceHi : "transparent"

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    spacing: 8
                    Text {
                        text: "#" + modelData.id
                        color: modelData.id === App.match.matchId
                            ? Theme.greenBright : Theme.textDim
                        font { family: Theme.fontMono; pixelSize: 11; weight: Font.Bold }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        width: 230
                        text: modelData.name && modelData.name.length
                            ? modelData.name
                            : (firstVideo ? firstVideo.path.split("/").pop() : "(empty)")
                        elide: Text.ElideMiddle
                        color: Theme.text
                        font { family: Theme.fontUi; pixelSize: 12 }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.videos.length + "v"
                        color: Theme.textDim
                        font { family: Theme.fontMono; pixelSize: 10 }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: projMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: firstVideo !== null
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        menu.close()
                        App.openProjectVideo(modelData.id, firstVideo.id, firstVideo.path)
                    }
                }
            }
        }

        Text {
            visible: menu.projects.length === 0
            text: "No projects yet — open a video to create one"
            color: Theme.textFaint
            font { family: Theme.fontUi; pixelSize: 11 }
            padding: 2
        }

        Rectangle { width: parent.width; height: 1; color: Theme.border }

        // ---- videos of the current project ----
        Text {
            visible: App.match.registered
            text: "VIDEOS IN PROJECT #" + App.match.matchId
            color: Theme.textFaint
            font { family: Theme.fontUi; pixelSize: 10; weight: Font.Bold; letterSpacing: 0.5 }
            padding: 2
        }

        Text {
            visible: App.match.registered && App.match.videos.length === 0
            text: "No videos yet — add the first one below"
            color: Theme.textFaint
            font { family: Theme.fontUi; pixelSize: 11 }
            padding: 2
        }

        Repeater {
            model: App.match.registered ? App.match.videos : []
            delegate: Rectangle {
                required property var modelData
                width: parent.width
                height: 34
                radius: 6
                color: vidMouse.containsMouse ? Theme.surfaceHi
                     : modelData.current ? "#141d3a2b" : "transparent"

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    spacing: 8
                    Rectangle {
                        width: roleText.implicitWidth + 10; height: 18; radius: 4
                        color: Theme.surface2
                        border.color: Theme.border2
                        border.width: 1
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            id: roleText
                            anchors.centerIn: parent
                            text: menu.roleLabel(modelData.role)
                                  + (modelData.segment && modelData.segment !== "full"
                                     ? " · " + menu.segmentLabel(modelData.segment) : "")
                                  + (modelData.view ? " · ✂" + modelData.view : "")
                            color: Theme.textMid
                            font { family: Theme.fontUi; pixelSize: 9; weight: Font.DemiBold }
                        }
                    }
                    Text {
                        width: 160
                        text: modelData.path.split("/").pop()
                        elide: Text.ElideMiddle
                        color: modelData.current ? Theme.greenBright : Theme.text
                        font { family: Theme.fontUi; pixelSize: 12 }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.status
                        color: Theme.textDim
                        font { family: Theme.fontMono; pixelSize: 9 }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: vidMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !modelData.current
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        menu.close()
                        App.openProjectVideo(App.match.matchId, modelData.id, modelData.path)
                    }
                }
            }
        }

        Rectangle {
            visible: App.match.registered
            width: parent.width; height: 1; color: Theme.border
        }

        // ---- add video: camera role, then match segment ----
        Text {
            visible: App.match.registered
            text: menu.pendingRole === ""
                ? "ADD VIDEO AS…"
                : "SEGMENT — " + menu.roleLabel(menu.pendingRole)
            color: Theme.textFaint
            font { family: Theme.fontUi; pixelSize: 10; weight: Font.Bold; letterSpacing: 0.5 }
            padding: 2
        }

        Repeater {
            model: !App.match.registered ? []
                 : menu.pendingRole === "" ? menu.roles : menu.segments
            delegate: Rectangle {
                required property var modelData
                width: parent.width
                height: 26
                radius: 6
                color: addMouse.containsMouse ? Theme.surfaceHi : "transparent"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    text: (menu.pendingRole === "" ? "+ " : "· ") + modelData.label
                    color: menu.pendingRole === "" ? Theme.greenBright : Theme.text
                    font { family: Theme.fontUi; pixelSize: 12; weight: Font.DemiBold }
                }
                MouseArea {
                    id: addMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (menu.pendingRole === "") {
                            menu.pendingRole = modelData.id
                        } else {
                            menu.addVideoRequested(menu.pendingRole, modelData.id)
                            menu.close()
                        }
                    }
                }
            }
        }

        Text {
            visible: App.match.registered && menu.pendingRole !== ""
            text: "‹ back"
            color: Theme.textDim
            font { family: Theme.fontUi; pixelSize: 11; weight: Font.DemiBold }
            padding: 4
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: menu.pendingRole = ""
            }
        }
    }
}
