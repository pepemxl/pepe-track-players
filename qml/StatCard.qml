import QtQuick

// One stat tile on the Tracking tab.
Rectangle {
    radius: 10
    color: Theme.surface
    border.color: Theme.border
    border.width: 1
    height: 86

    property string label: ""
    property string value: ""
    property color valueColor: Theme.textBright

    Column {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 18
        spacing: 8
        Text {
            text: label
            color: Theme.textDim
            font { family: Theme.fontUi; pixelSize: 11 }
        }
        Text {
            text: value
            color: valueColor
            font { family: Theme.fontMono; pixelSize: 26; weight: Font.Bold }
        }
    }
}
