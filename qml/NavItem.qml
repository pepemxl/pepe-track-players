import QtQuick

// One sidebar entry: icon (delegated to the parent via default property) + label.
Rectangle {
    id: item
    width: 64
    height: 58
    radius: 10
    color: active ? "#4d1d3a2b" : "transparent"

    property string label: ""
    property bool active: false
    property alias iconItem: iconSlot.children
    readonly property color iconColor: active ? Theme.greenBright : Theme.textDim

    signal clicked()

    Column {
        anchors.centerIn: parent
        spacing: 5
        Item {
            id: iconSlot
            width: 22; height: 20
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: item.label
            color: item.iconColor
            font { family: Theme.fontUi; pixelSize: 10; weight: Font.DemiBold }
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: item.clicked()
    }
}
