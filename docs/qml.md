# Capa QML — componentes

Módulo estático `PepeTrack` (URI) definido en `qml/CMakeLists.txt`. Todos los
componentes leen el backend a través del context property `App`.

## Estructura

```
Main.qml            ventana principal: TopBar + SideNav + stack de 4 vistas
Theme.qml           singleton: paleta, fuentes, tipos de marcador
TopBar.qml          logo, etiqueta del partido, Open video, chip de guardado, avatar
SideNav.qml         navegación lateral (iconos Canvas): Video / Homog. / Metadata / Tracking
NavItem.qml         un ítem del sidebar (icono por slot + label + estado activo)
VideoSurface.qml    lienzo de video compartido (Image + provider) con mapeo de coordenadas
VideoView.qml       vista Video: ops panel + player + tagging + rail de tags
VideoOpsPanel.qml   panel colapsable: marcadores de frame + operaciones offline
PlayerDropdown.qml  popup "assign player" con ambos rosters
HomographyView.qml  vista Homography: quad A/B/C/D + panel de correspondencias
MetadataView.qml    vista Metadata: formulario del partido + 2 RosterTable
RosterTable.qml     tabla editable de un equipo (#/NAME/POSITION/×, + Add player)
TrackingView.qml    vista Tracking: control bar, stat cards, strip por frame, tabla
StatCard.qml        tile de estadística (label + valor mono grande)
EditField.qml       TextField plano reutilizable (boxed o inline)
```

## Detalles por componente

### Main.qml
`ApplicationWindow` 1440×900. Mantiene `activeTab`; las 4 vistas viven en un
stack propio (`visible:` por pestaña) para conservar estado. `FileDialog`
para abrir video. Atajos: `Ctrl+O` abrir, `Ctrl+S` guardar, `Space`
play/pausa (solo en la vista Video).

### Theme.qml (singleton)
Paleta del diseño (oklch aproximado a sRGB), fuentes (`Segoe UI` /
`Consolas`), `teamColor(team)`, y el catálogo `markerTypes`
(`{type, label, tint}`) + `markerInfo(type)` usado por el panel de ops y los
ticks del scrubber.

### VideoSurface.qml
Rectángulo con el `Image` del provider (`image://videoframe/<serial>`,
`PreserveAspectFit`) y placeholder de rayas cuando no hay video. Expone la
geometría del área realmente pintada (letterbox):
`paintedX/Y/Width/Height`, `videoScale`, y helpers
`toVideoX/Y(mouse)` ↔ `fromVideoX/Y(px)` + `insidePainted(mx,my)` para que
los overlays (tags, puntos de homografía) mapeen píxeles de video a pantalla.

### VideoView.qml
Fila de tres bloques: `VideoOpsPanel` (colapsable), columna central (video +
transporte + strip de calidad) y rail derecho de tags.
- Click en el video (crosshair) → pausa y abre `PlayerDropdown` en esa
  posición; al elegir jugador llama `App.addTag(vx, vy, team, row)`.
- Marcadores de tag (`P<num>`) se dibujan sobre el video para tags a ±1 s
  del frame actual (los roles `x`/`y` del modelo se leen como `model.x`
  para no chocar con la geometría del delegate).
- Cajas de detección del Track chunks superpuestas al video durante la
  reproducción (`App.tracking.detectionsAt(App.positionSec)`): borde verde
  (conf ≥ 0.5) o amarillo, con etiqueta `T<id>`; chip
  "▣ DETECTIONS ON/OFF" bajo el badge de frame para alternarlas.
- Click **dentro de una caja** → el dropdown pasa a modo
  "ASSIGN PLAYER — TRACK <key>": elegir jugador asigna el track
  (`assignTrack`) en vez de crear un tag posicional; la caja toma el color
  del equipo y muestra `P<dorsal>`. Click fuera de las cajas → tagging
  posicional normal (círculo).
- **Click derecho sobre una caja**: si está asignada, la desasigna
  (`clearAssignment`, vuelve a `T<id>`); si no, muestra un toast con su
  track (`track 002-T31 · conf 0.85`). El toast aparece abajo al centro
  del video y se desvanece solo.
- Transporte: play/pausa, timecode, scrubber arrastrable
  (`App.seekFrac`), ticks de colores de los frame markers, duración total,
  y el selector de velocidad: chip `native ▾` que abre un popup con
  native / 1 / 5 / 10 / 20 / 30 / 60 / 120 frames/s (escribe
  `App.playbackFps`; el chip se pinta verde cuando hay tasa fija activa).
- Strip "FRAME TRACKING QUALITY": 90 chips desde `App.tracking.frameChips`.
- Rail derecho: lista de tags (click → seek al frame, × borra), resaltando
  los cercanos al frame actual; en su encabezado, botones ↶/↷ de
  undo/redo del tagging (también `Ctrl+Z` / `Ctrl+Y` en el tab Video).

### VideoOpsPanel.qml
Panel izquierdo colapsable (252 px ↔ 32 px, animado).
- Chip del registro: `MATCH #<id>` + status (+ conteo de chunks).
- "FRAME MARKERS · at frame N": grid 2×4 de botones (uno por
  `Theme.markerTypes`) que llaman `App.match.addMarker(type,
  App.currentFrame)`; lista de marcadores (click → seek, × borra).
- "OPERATIONS": `OpButton`s (componente inline con estado ✓ done):
  `preprocess()`, `createChunks()`, `trackChunks()` (habilitado con
  `chunkCount > 0`), `extractLineups()` (con marcadores de lineup), e
  **Infer IDs · this frame / all chunks** (`App.tracking.inferIdentities`)
  con contador de inferidas y "clear". Barra de progreso + label + Cancel
  mientras `opRunning`; errores en rojo desde `lastError`.

### PlayerDropdown.qml
`Popup` con ambos rosters aplanados (home primero); cada fila muestra el
chip numerado con el color del equipo. Emite `playerPicked(team, rosterRow)`.

### HomographyView.qml
- `Canvas` dibuja el quad A-B-C-D (relleno translúcido + borde), y con el
  overlay activo las diagonales y la línea media. Se repinta al cambiar
  puntos, overlay, tamaño o geometría pintada del video.
- Handles de los puntos como `Repeater` (el punto seleccionado crece y
  cambia a verde). Flujo: seleccionar punto en el panel derecho → click en
  el frame (`setImagePoint`) → hint naranja mientras hay selección.
- Stepper `‹ ›` (±1 frame, pausa), scrubber propio y strip de salto rápido
  (12 posiciones).
- Panel derecho: filas de correspondencias `img (x, y) / pitch (px, py)`,
  card del error de reproyección, toggle "Show pitch overlay", botones
  **Recompute H** y **Reset points**.

### MetadataView.qml
`Flickable` con el grid "MATCH DETAILS" (6 `EditField` ligados por nombre de
propiedad a `App.metadata[key]`) y dos `RosterTable` lado a lado (nombre de
equipo editable ligado a `metadata.homeTeam/awayTeam`).

### RosterTable.qml
Encabezado + `ListView` no interactivo de filas editables (`EditField`
inline por columna → `roster.set(index, field, value)`), botón × por fila y
footer "+ Add player".

### TrackingView.qml
- Control bar: chips de modelo (`tracking.modelName`) y rango, label de
  progreso, barra de 180 px y botón Start/Stop/Rerun.
- 4 `StatCard`: frames procesados, players tracked, avg confidence, frames
  con track perdido.
- Strip "PER-FRAME TRACKING STATUS" (90 celdas) + leyenda de 4 estados.
- Tabla "TRACKS": filas desde `App.tracksModel` con pill de status
  coloreada; click selecciona/deselecciona.
