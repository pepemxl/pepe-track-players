import QtQuick
import QtQuick.Controls

// Four small integer boxes for a football formation, e.g. 4-1-3-2 (a 3-line
// formation like 4-3-3 just leaves the last box empty). `value` is the
// "-"-joined string; edits emit edited(newValue) with empty boxes dropped.
Row {
    id: root

    property string value: ""
    signal edited(string v)

    spacing: 5

    function loadValue() {
        const p = root.value.split("-")
        f0.text = (p[0] || "").trim()
        f1.text = (p[1] || "").trim()
        f2.text = (p[2] || "").trim()
        f3.text = (p[3] || "").trim()
    }
    // Current boxes as a "-"-joined string (empty boxes dropped so "4--3" can't
    // happen — formations have no internal gaps).
    function composed() {
        const fs = [f0, f1, f2, f3]
        let out = []
        for (let i = 0; i < 4; ++i) {
            const t = fs[i].text.trim()
            if (t.length > 0)
                out.push(t)
        }
        return out.join("-")
    }

    Component.onCompleted: loadValue()
    // Reload only on an external change (project load), not our own commit —
    // that keeps a half-typed row from reshuffling as boxes are filled.
    onValueChanged: if (root.value !== composed()) loadValue()

    component Cell: TextField {
        width: 42; height: 34
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.text
        font { family: Theme.fontUi; pixelSize: 14; weight: Font.DemiBold }
        inputMethodHints: Qt.ImhDigitsOnly
        maximumLength: 2
        validator: IntValidator { bottom: 0; top: 11 }
        selectByMouse: true
        placeholderText: "–"
        padding: 0
        onEditingFinished: root.edited(root.composed())
        background: Rectangle {
            radius: 7
            color: Theme.surface
            border.color: parent.activeFocus ? Theme.greenDim : Theme.border2
            border.width: 1
        }
    }

    Cell { id: f0 }
    Cell { id: f1 }
    Cell { id: f2 }
    Cell { id: f3 }
}
