import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1100
    minimumHeight: 700
    title: "PepeTrack — Football Tracker"
    color: Theme.bg

    property int activeTab: 0   // 0 video, 1 homography, 2 metadata, 3 tracking

    FileDialog {
        id: openDialog
        title: "Open match video"
        nameFilters: ["Video files (*.mp4 *.mov *.mkv *.avi *.m4v)", "All files (*)"]
        onAccepted: App.openVideo(selectedFile)
    }

    Shortcut { sequence: "Ctrl+O"; onActivated: openDialog.open() }
    Shortcut { sequence: "Ctrl+S"; onActivated: App.saveProject() }
    Shortcut {
        sequence: "Space"
        enabled: root.activeTab === 0
        onActivated: App.togglePlay()
    }
    Shortcut {
        sequence: "Left"
        enabled: root.activeTab === 0
        onActivated: App.seekRelative(-5)
    }
    Shortcut {
        sequence: "Right"
        enabled: root.activeTab === 0
        onActivated: App.seekRelative(5)
    }

    Column {
        anchors.fill: parent

        TopBar {
            id: topBar
            width: parent.width
            onOpenRequested: openDialog.open()
            onSaveRequested: App.saveProject()
        }

        Row {
            width: parent.width
            height: parent.height - topBar.height

            SideNav {
                id: sideNav
                height: parent.height
                activeTab: root.activeTab
                onTabSelected: (tab) => root.activeTab = tab
            }

            StackLayout_ {
                width: parent.width - sideNav.width
                height: parent.height
            }
        }
    }

    // Lightweight stack: only the active view is visible; all keep state.
    component StackLayout_: Item {
        VideoView      { anchors.fill: parent; visible: root.activeTab === 0 }
        HomographyView { anchors.fill: parent; visible: root.activeTab === 1 }
        MetadataView   { anchors.fill: parent; visible: root.activeTab === 2 }
        TrackingView   { anchors.fill: parent; visible: root.activeTab === 3 }
    }
}
