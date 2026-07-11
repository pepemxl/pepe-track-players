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

    // Frame-marker types used by the video ops panel and scrubber ticks.
    // Every "*_start"/"*_end" pair (except commercials) delimits a play
    // period: tracking only runs inside play periods.
    readonly property var markerTypes: [
        { type: "match_start",       label: "Match start",    tint: "#30d980" },
        { type: "match_end",         label: "Match end",      tint: "#e35449" },
        { type: "first_half_start",  label: "1T inicio",      tint: "#38c98a" },
        { type: "first_half_end",    label: "1T fin",         tint: "#2a8f64" },
        { type: "second_half_start", label: "2T inicio",      tint: "#38aec9" },
        { type: "second_half_end",   label: "2T fin",         tint: "#2a7e8f" },
        { type: "extra1_start",      label: "ET1 inicio",     tint: "#9a7ae0" },
        { type: "extra1_end",        label: "ET1 fin",        tint: "#6f57a8" },
        { type: "extra2_start",      label: "ET2 inicio",     tint: "#c97ae0" },
        { type: "extra2_end",        label: "ET2 fin",        tint: "#8f57a8" },
        { type: "penalties_start",   label: "Penales inicio", tint: "#e07aab" },
        { type: "penalties_end",     label: "Penales fin",    tint: "#a85779" },
        { type: "lineup_a",          label: "Lineup A",       tint: "#4757d8" },
        { type: "lineup_b",          label: "Lineup B",       tint: "#dd7a41" },
        { type: "bench_a",           label: "Bench A",        tint: "#6b77c9" },
        { type: "bench_b",           label: "Bench B",        tint: "#c9976b" },
        { type: "commercial_start",  label: "Comm. start",    tint: "#e0ac37" },
        { type: "commercial_end",    label: "Comm. end",      tint: "#e0ac37" },
    ]

    function markerInfo(type) {
        for (const m of markerTypes)
            if (m.type === type) return m
        return { type: type, label: type, tint: "#8b93a1" }
    }
}
