pragma Singleton
import QtQuick

// Palette and typography lifted from the Football Tracker design
// (oklch values approximated as sRGB).
QtObject {
    // Backgrounds
    readonly property color bg:        "#14171c"   // main window
    readonly property color bgDark:    "#0f1116"   // top bar / sidebar
    readonly property color surface:   "#1b1f26"   // cards, list rows
    readonly property color surface2:  "#20242c"   // table headers
    readonly property color surfaceHi: "#252a33"   // buttons, chips
    readonly property color overlayBg: "#b3121419" // translucent badges on video

    // Borders
    readonly property color border:    "#2c313a"
    readonly property color border2:   "#333945"
    readonly property color borderHi:  "#3c434f"

    // Text
    readonly property color text:       "#eceef2"
    readonly property color textBright: "#dfe2e8"
    readonly property color textMid:    "#9aa2af"
    readonly property color textMuted:  "#8b93a1"
    readonly property color textDim:    "#6f7684"
    readonly property color textFaint:  "#616876"

    // Accents
    readonly property color green:       "#30d980"
    readonly property color greenBright: "#6fe6a8"
    readonly property color greenDim:    "#0d8f52"
    readonly property color red:         "#e35449"
    readonly property color yellow:      "#e0ac37"
    readonly property color orange:      "#ffb46b"
    readonly property color homeColor:   "#4757d8"
    readonly property color awayColor:   "#dd7a41"

    readonly property string fontUi:   "Segoe UI"
    readonly property string fontMono: "Consolas"

    function teamColor(team) {
        return team === 0 ? homeColor : awayColor
    }
}
