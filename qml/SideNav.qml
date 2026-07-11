import QtQuick

// Left sidebar with the section switches
// (Video / Sync / Chunks / Homog. / Metadata / Tracking).
Rectangle {
    id: nav
    width: 88
    color: Theme.bgDark

    property int activeTab: 0
    signal tabSelected(int tab)

    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }

    Column {
        anchors.top: parent.top
        anchors.topMargin: 18
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 6

        NavItem {
            id: videoNav
            label: "Video"
            active: nav.activeTab === 0
            onClicked: nav.tabSelected(0)
            iconItem: Canvas {
                anchors.fill: parent
                property color c: videoNav.iconColor
                onCChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = c
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    ctx.roundedRect(1, 3, 13, 14, 3, 3)
                    ctx.stroke()
                    ctx.fillStyle = c
                    ctx.beginPath()
                    ctx.moveTo(15, 5); ctx.lineTo(21, 10); ctx.lineTo(15, 15)
                    ctx.closePath(); ctx.fill()
                }
            }
        }

        NavItem {
            id: syncNav
            label: "Sync"
            active: nav.activeTab === 4
            onClicked: nav.tabSelected(4)
            iconItem: Canvas {
                anchors.fill: parent
                property color c: syncNav.iconColor
                onCChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = c
                    ctx.lineWidth = 1.6
                    // two overlapping film frames
                    ctx.beginPath()
                    ctx.roundedRect(1, 3, 12, 11, 2, 2)
                    ctx.stroke()
                    ctx.beginPath()
                    ctx.roundedRect(8, 7, 12, 11, 2, 2)
                    ctx.stroke()
                }
            }
        }

        NavItem {
            id: chunksNav
            label: "Chunks"
            active: nav.activeTab === 5
            onClicked: nav.tabSelected(5)
            iconItem: Canvas {
                anchors.fill: parent
                property color c: chunksNav.iconColor
                onCChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = c
                    ctx.lineWidth = 1.6
                    // three stacked segments
                    for (const y of [3, 8.5, 14]) {
                        ctx.beginPath()
                        ctx.roundedRect(2, y, 16, 3.5, 1.5, 1.5)
                        ctx.stroke()
                    }
                }
            }
        }

        NavItem {
            id: homoNav
            label: "Homog."
            active: nav.activeTab === 1
            onClicked: nav.tabSelected(1)
            iconItem: Canvas {
                anchors.fill: parent
                property color c: homoNav.iconColor
                onCChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = c
                    ctx.lineWidth = 1.6
                    ctx.beginPath()
                    ctx.moveTo(2, 7); ctx.lineTo(18, 4); ctx.lineTo(18, 16)
                    ctx.lineTo(2, 15); ctx.closePath()
                    ctx.moveTo(2, 7); ctx.lineTo(18, 16)
                    ctx.stroke()
                }
            }
        }

        NavItem {
            id: metaNav
            label: "Metadata"
            active: nav.activeTab === 2
            onClicked: nav.tabSelected(2)
            iconItem: Canvas {
                anchors.fill: parent
                property color c: metaNav.iconColor
                onCChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = c
                    ctx.lineWidth = 1.6
                    ctx.beginPath()
                    ctx.roundedRect(2, 1, 16, 17, 2, 2)
                    ctx.moveTo(5.5, 6);   ctx.lineTo(14.5, 6)
                    ctx.moveTo(5.5, 9.5); ctx.lineTo(14.5, 9.5)
                    ctx.moveTo(5.5, 13);  ctx.lineTo(11, 13)
                    ctx.stroke()
                }
            }
        }

        NavItem {
            id: trackNav
            label: "Tracking"
            active: nav.activeTab === 3
            onClicked: nav.tabSelected(3)
            iconItem: Canvas {
                anchors.fill: parent
                property color c: trackNav.iconColor
                onCChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = c
                    ctx.lineWidth = 1.6
                    ctx.setLineDash([2, 2])
                    ctx.beginPath()
                    ctx.moveTo(4, 12); ctx.lineTo(11, 5); ctx.lineTo(18, 13)
                    ctx.stroke()
                    ctx.setLineDash([])
                    for (const p of [[4, 12], [11, 5], [18, 13]]) {
                        ctx.beginPath()
                        ctx.arc(p[0], p[1], 2.5, 0, 2 * Math.PI)
                        ctx.stroke()
                    }
                }
            }
        }
    }
}
