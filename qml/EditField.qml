import QtQuick
import QtQuick.Controls

// Flat editable text cell used across Metadata forms and roster tables.
TextField {
    id: field

    property color textColor: Theme.text
    property bool boxed: true

    color: textColor
    font { family: Theme.fontUi; pixelSize: 13 }
    selectByMouse: true
    padding: boxed ? 10 : 2
    leftPadding: boxed ? 12 : 2

    background: Rectangle {
        radius: 8
        color: field.boxed ? Theme.surface : "transparent"
        border.color: field.activeFocus ? Theme.greenDim
                     : field.boxed ? Theme.border : "transparent"
        border.width: field.activeFocus || field.boxed ? 1 : 0
    }
}
