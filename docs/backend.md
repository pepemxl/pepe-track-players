# Backend C++ — clases y funciones

Todas las clases viven en `src/`. QML habla únicamente con `AppController`,
expuesto como context property `App`; el resto se alcanza a través de sus
propiedades (`App.homography`, `App.match`, etc.).

---

## AppController (`AppController.h/.cpp`)

Fachada principal. Posee el motor de video, los modelos y los managers.

### Propiedades (QML)

| Propiedad | Tipo | Descripción |
|---|---|---|
| `videoLoaded` | bool | Hay un video abierto y con información válida |
| `videoName` / `videoWidth` / `videoHeight` | — | Metadatos del archivo abierto |
| `totalFrames` / `durationSec` / `fps` | — | Duración del video |
| `playing` | bool | Estado de reproducción |
| `currentFrame` / `positionSec` | — | Posición actual (se actualiza por frame) |
| `frameSerial` | int | Contador que invalida el caché del `Image` QML (`image://videoframe/<serial>`) |
| `playbackFps` | double (RW) | Tasa fija de reproducción en frames/s; 0 = velocidad nativa del video. Opciones de la UI: 1/5/10/20/30/60/120 |
| `dirty` | bool | Hay cambios sin guardar (chip del top bar) |
| `lastError` | QString | Último error reportado por los workers |
| `metadata, homeRoster, awayRoster, tags, homography, tracking, tracksModel, match` | QObject* | Sub-objetos (ver abajo) |

### Funciones invocables

- `openVideo(QUrl)` — detiene los workers, arranca `VideoEngine` con la nueva
  fuente (pausado mostrando el frame 0), registra el match y recarga el
  proyecto (`<video>_project/project.json`) si existe.
- `togglePlay()` / `pause()` — control de reproducción; al llegar al final,
  `togglePlay()` reinicia desde el frame 0.
- `seekFrac(double 0..1)` / `seekFrame(int)` / `stepFrames(int delta)` —
  búsqueda absoluta por fracción, por frame, o paso relativo (±N frames).
- `seekRelative(double seconds)` — salto relativo en segundos (flechas
  ←/→ del teclado: ±5 s en la vista Video).
- `timecode(double sec) → QString` — formatea `HH:MM:SS.FF` usando el fps real.
- `addTag(vx, vy, team, rosterRow)` — crea un `TagEvent` en el frame actual
  con coordenadas de video en píxeles; si la homografía está verificada
  añade coordenadas de cancha en metros (`imageToPitch`).
- `saveProject() → bool` — escribe `project.json`, `tags.csv` y `tracks.csv`
  en `<carpeta del video>/<nombre>_project/`; limpia `dirty`.
- **Undo/redo de tagging** — `undo()` / `redo()` (invocables) +
  `canUndo`/`canRedo`. Stack de comandos (máx. 100) que cubre: agregar tag,
  borrar tag, asignar track y desasignar track. Por eso las mutaciones se
  rutean por `AppController`: `removeTag(row)`, `assignTrack(key, number,
  name, team)` y `clearTrackAssignment(key)` capturan el estado previo
  antes de aplicar. Los tags llevan un `id` estable (`TagEvent::id`) para
  deshacer/rehacer sin depender de índices. El historial se limpia al
  abrir otro video. UI: `Ctrl+Z` / `Ctrl+Y`/`Ctrl+Shift+Z` (solo tab
  Video) y botones ↶/↷ en el rail de tags.

---

## VideoEngine (`VideoEngine.h/.cpp`)

`QThread` de reproducción (patrón videopp): `run()` es dueño del
`cv::VideoCapture`; la GUI lo controla con atomics + `QWaitCondition` y nunca
se bloquea.

- `setSource(path)` — fija el archivo (llamar con el worker detenido).
- `stopProcessing()` — pide alto y espera el join del hilo.
- `setPaused(bool)` — pausa/reanuda; despierta la wait condition.
- `seekToFrame(int)` — seek absoluto (se aplica en el loop del worker).
- `stepFrames(int delta)` — paso relativo al frame mostrado; funciona en
  pausa (el chequeo de pausa es *después* de emitir, así el nuevo frame se
  muestra de inmediato).
- `setSpeed(double)` — multiplicador 0.1–8.0 sobre el fps nativo (ajusta el
  sleep entre frames).
- `setPlaybackFps(double)` — tasa fija de reproducción en **frames
  mostrados por segundo** (0 = nativa). Cuando es > 0 tiene prioridad sobre
  `setSpeed`: el delay entre frames pasa a ser `1000/fps` sin importar el
  fps del archivo.
- Señales: `frameReady(QImage, frameIndex, posSec)`, `videoInfo(w, h,
  totalFrames, fps)`, `endReached()`, `error(QString)`, `finished()`.
- Al llegar al fin de archivo el hilo **no** muere: queda en pausa esperando
  seek/step/stop, para que el scrub siga funcionando.

## FrameProvider (`FrameProvider.h`)

`QQuickImageProvider` thread-safe (mutex). `setImage(QImage)` guarda el
último frame decodificado; QML lo lee con
`Image { source: "image://videoframe/" + App.frameSerial; cache: false }`.

---

## Modelos de datos

### RosterModel (`RosterModel.h/.cpp`) — ×2 (home/away)

`QAbstractListModel` editable con roles `number`, `name`, `position`.

- `addPlayer()` / `removePlayer(row)` — alta/baja de filas.
- `set(row, field, value)` — edición en línea desde los `TextField` QML.
- `get(row) → QVariantMap` — usado por el dropdown de tagging.
- `toJson()` / `fromJson(QJsonArray)` — persistencia en `project.json`.
- Señal `edited()` → marca `dirty` en `AppController`.

### MatchMetadata (`MatchMetadata.h/.cpp`)

`QObject` con `Q_PROPERTY` de texto: `homeTeam`, `awayTeam`, `league`,
`season`, `competition`, `date`, `venue`, `referee` (macro `META_PROP`).
`toJson()` / `fromJson()`. Señal única `changed()`.

### TagsModel (`TagsModel.h/.cpp`)

`QAbstractListModel` de eventos de tagging (roles: `frame`, `timecode`,
`playerNumber`, `playerName`, `team`, `x`, `y`, `pitchX`, `pitchY`,
`hasPitch`). `addTag(TagEvent)` inserta al inicio (log más reciente primero);
`removeTag(row)`; `toJson()`/`fromJson()`.

### TracksModel (`TracksModel.h/.cpp`)

`QAbstractListModel` de solo lectura para la tabla de tracks. `setRows(
QVariantList)` reemplaza todo el contenido con los snapshots que emite
`TrackingManager` (roles: `trackId`, `name`, `framesTracked`, `avgConf`,
`status`, `statusKind`).

---

## HomographyManager (`HomographyManager.h/.cpp`)

Calibración imagen→cancha con las 4 correspondencias A/B/C/D (cancha de
105×68 m).

- `setImageSize(w, h)` — recoloca los puntos default dentro del frame al
  cargar video (solo si el usuario no los ha movido).
- `setImagePoint(id, x, y)` — mueve un punto (coordenadas de video en px);
  invalida `verified`.
- `recompute()` — resuelve la homografía con **DLT de 4 puntos** vía
  `cv::solve` (el build de OpenCV no trae calib3d, y con 4 correspondencias
  el sistema 8×8 es exacto). Calcula el error medio de reproyección en px
  (`H⁻¹`·pitch vs. puntos de imagen) y enciende `verified`.
- `reset()` — vuelve a los puntos default.
- `imageToPitch(x, y) → QPointF` — transforma px de video a metros
  (`cv::perspectiveTransform`); usado por los tags.
- Propiedades: `points` (QVariantList de `{id, ix, iy, px, py}`),
  `verified`, `reprojError`, `overlayEnabled`.
- `toJson()` / `fromJson()`.

---

## YoloDetector (`YoloDetector.h/.cpp`)

Detector de personas YOLOv8n (COCO, clase 0) sobre `cv::dnn`, compartido por
`TrackingManager` y `VideoOpsWorker`. Letterbox a 640 + NMS (mismo decode que
el PoseDetector de videopp).

- `resolveModelPath()` (estática) — busca `models/yolov8n.onnx` junto al
  ejecutable y en el root del proyecto.
- `load(&error) → bool` — `readNetFromONNX`, backend/target CPU.
- `detect(cv::Mat) → vector<Detection{box, conf}>` — umbral 0.35, NMS 0.45.

## TrackingManager (`TrackingManager.h/.cpp`)

`QThread` de inferencia interactiva (Tab Tracking): recorre el video
completo, detecta cada 5 frames y asocia por IoU (greedy, umbral 0.2).

- `setSource(path)` — resetea stats/chips/tracks.
- `toggleRun()` (invocable) — Start/Stop; `stopInference()` hace join.
- Propiedades para QML: `running`, `completed`, `progress`,
  `framesProcessed`, `playersTracked`, `avgConfidence`, `lostFrames`,
  `frameChips` (90 buckets: 0 sin procesar, 1 bien, 2 baja confianza,
  3 perdido), `modelName`.
- El worker emite `snapshotReady(...)` (queued) cada ~300 ms; el hilo GUI
  aplica el snapshot y reenvía `tracksUpdated(QVariantList)` al
  `TracksModel`.
- `loadFromChunkCsvs(chunksDir, durationSec, excludedSec)` — puebla el tab
  desde los CSVs persistidos del Track chunks (stats, chips de 90 buckets
  y tabla de tracks con ids `NNN-Txx` por chunk, top 200 por frames).
  `AppController` lo llama al abrir un video con status `tracked` y al
  terminar un Track chunks; no hace nada si hay una corrida en vivo.
  Además indexa cada detección (x,y,w,h,conf) por slot de 0.1 s.
- `detectionsAt(sec)` (invocable) + `hasDetections` — cajas detectadas en
  la posición de reproducción, para el overlay del reproductor (la vista
  Video las dibuja con un toggle "DETECTIONS ON/OFF"). Cada caja incluye
  su `key` de track (`NNN-Txx`) y, si está asignada, el jugador.
- `assignTrack(key, number, name, team)` / `clearAssignment(key)`
  (invocables) — asigna un jugador del roster a un chunk-track: sus cajas
  pasan a mostrar `P<dorsal>` con el color del equipo, la tabla del tab
  Tracking muestra `dorsal · nombre`, y todo persiste en
  `<matchDir>/track_assignments.json` (se recarga con los CSVs). Cada
  cambio reescribe la metadata del chunk afectado (ver abajo).
- `inferIdentities(allChunks, currentSec)` / `clearInferred()` /
  `inferredCount` — **inferencia de identidades**: propaga las
  asignaciones manuales a otros tracks por continuidad espacial, con dos
  mecanismos encadenables (pasadas hasta converger):
  - *Fronteras de chunk*: IoU ≥ 0.2 entre el último box del track
    identificado y el primero de los tracks del chunk vecino (±3 s).
  - *Fusión de tracks rotos dentro del chunk*: cuando un track
    identificado muere y otro nace cerca poco después (gap ≤ 2 s,
    distancia de centros ≤ 1.2× la altura del box, alturas comparables),
    hereda la identidad — en ambas direcciones (muerte→nacimiento y
    nacimiento→muerte previa).
  - *Restricción física*: todos los tracks con una misma identidad deben
    ser disjuntos en el tiempo (tolerancia de 1.5 s para duplicados
    momentáneos); esto evita que las cadenas "deriven" a otros jugadores.
  Con `allChunks=false` solo conserva las inferencias del chunk de
  `currentSec`. Las inferidas se distinguen de las manuales (`≈` en tabla
  y overlay) y una asignación manual siempre las supersede. Escribe
  `<matchDir>/video_chunks_metadata/video_metadata_part_<NNN>.json` con
  todas las asignaciones del chunk (`source: manual | inferred`).

---

## MatchManager (`MatchManager.h/.cpp`)

Registro por video + marcadores de frame + orquestación de operaciones
offline. Expuesto como `App.match`.

- `setVideo(path, fps, totalFrames)` — busca/crea la entrada en
  `LOCAL_DATA/matches/games.json` (id incremental), crea
  `matches/match_<id 4 ceros>/` (migrando carpetas viejas sin padding) y
  carga `markers.json`.
- `addMarker(type, frame)` / `removeMarker(index)` — marcadores ordenados
  por frame, guardado inmediato a `markers.json`. Tipos: `match_start`,
  `match_end`, `lineup_a`, `lineup_b`, `bench_a`, `bench_b`,
  `commercial_start`, `commercial_end`.
- `preprocess()` / `createChunks()` / `trackChunks()` / `extractLineups()`
  / `cancelOp()` — lanzan una operación en `VideoOpsWorker` o
  `LineupExtractor` (una a la vez).
- `excludedRangesSec()` / `excludedFrameRanges()` — rangos que **ambos**
  caminos de tracking saltan: antes de `match_start`, después de
  `match_end`, y cada rango comercial (un `commercial_start` sin cerrar
  corre hasta el final). `AppController` los empuja al `TrackingManager`
  en cada cambio de marcadores; `startOp` los pasa al `VideoOpsWorker`.
- `matchStartFrame` / `matchEndFrame` / `hasLineupMarkers` /
  `lineupsExtracted` — propiedades derivadas de los marcadores para la UI
  (Range chip del tab Tracking, botón de OCR).
- `createProject()` (invocable) — crea un proyecto vacío (entrada en
  games.json con `videos: []` y su carpeta) y lo hace el proyecto actual
  con `videoId = 0`; las operaciones, marcadores y crop quedan bloqueados
  hasta que se agrega el primer video vía `prepareAddVideo()` (menú
  "ADD VIDEO AS…").
- `listProjects()` / `prepareAddVideo(role, segment)` /
  `prepareOpenVideo(matchId, videoId)` — soporte del menú Project: lista
  de proyectos con sus videos; alta del próximo video abierto en el
  proyecto actual con rol y segmento (**siempre crea entrada nueva**, así
  el mismo archivo puede repetirse como otra vista); y apertura de una
  entrada exacta por (match, video id) — necesario porque con rutas
  duplicadas el lookup por ruta es ambiguo (resuelve a la primera; lo
  usan los modos standalone y CLI).
- Al terminar cada op actualiza `status` en games.json:
  `registered → preprocessed → chunked → tracked`.
- Propiedades: `registered`, `matchId`, `status`, `matchDir`, `chunkCount`,
  `markers`, `opRunning`, `opLabel`, `opProgress`, `lastError`.
- La raíz de datos se resuelve en `dataRoot()`: env `PEPETRACK_DATA_DIR`,
  o `LOCAL_DATA/` en el root del proyecto (layout dev `build/bin` → `../..`),
  o junto al exe.

## VideoOpsWorker (`VideoOpsWorker.h/.cpp`)

`QThread` de operaciones pesadas; una op por `run()` según `configure(op,
sourcePath, matchDir, commercialsSec)`.

- `runPreprocess()` — re-muestrea por tiempo el video fuente a **20 fps**
  (`preprocessed_20fps.mp4`, mp4v). Un frame de salida por cada 1/20 s de
  tiempo fuente (duplica o descarta frames según el fps origen).
- `runChunks()` — usa el preprocesado si existe (si no, el original) y
  escribe chunks de **1 minuto a 10 fps**:
  `video_chunks/video_part_<NNN>.mp4` (600 frames por chunk, numeración
  3 dígitos desde 001; borra chunks y CSVs de corridas anteriores).
  Además escribe `video_chunks/chunks.json` con el rango de cada chunk
  (`number`, `file`, `frames`, `start_sec`, `end_sec`).
- `runTrack()` — por cada chunk corre `YoloDetector` frame a frame con un
  tracker IoU local (los tracks no cruzan chunks; un track sin match por
  más de 1.5 s muere y no puede re-capturar detecciones — el jugador que
  reaparece es un track nuevo) y escribe
  `video_part_<NNN>.csv` con
  `frame,time_sec,track_id,x,y,w,h,conf,chunk_start_sec,chunk_end_sec`
  (`time_sec` absoluto; `chunk_*` = rango real del chunk, con el fin
  parcial exacto en el último). `isExcluded(sec)` omite por completo los
  frames fuera de la ventana del partido y dentro de comerciales.
- Señales: `progressChanged(frac, label)` y `opFinished(op, ok, error,
  result)` — entregadas queued al hilo GUI.
- `requestStop()` / `stopAndWait()` — cancelación cooperativa (el
  preprocess cancelado borra el archivo parcial).

## LineupExtractor (`LineupExtractor.h/.cpp`)

`QThread` de extracción de alineaciones por OCR: para cada marcador
`lineup_a/b` y `bench_a/b`, salta a ese frame, lo guarda como BMP en
`<matchDir>/lineups/` (BMP: único codec del build de OpenCV) y lo pasa al
**OCR de Windows** (`scripts/ocr.ps1`, `Windows.Media.Ocr` vía QProcess).

- Parser: filas numeradas (`"10 MESSI"`, con tolerancia al badge de capitán
  OCR-eado como letra suelta: `"B 10 LIONEL MESSI"`) y filas de solo nombre
  (≥ 2 palabras) cuando el OCR pierde el dorsal → número 0, editable.
  Blocklist de señalética (FIFA/CUP/WORLD/ENTRENADOR/COACH).
- Dedupe por dorsal y por nombre, conservando la primera aparición.
- `finishedExtraction(ok, error, teamA, teamB)` → `MatchManager` escribe
  `lineups.json` y emite `lineupsReady`; `AppController` reemplaza los
  rosters (equipo A → home, B → away) y marca dirty.

## Modo headless (CLI)

- `pepe_track_players.exe --extract-lineups <video>` — corre el OCR de
  alineaciones de un video ya marcado e imprime jugadores y nombres de
  equipo detectados.
- `pepe_track_players.exe --dump-exclusions <video>` — imprime la ventana
  del partido y los rangos excluidos del tracking.
- `pepe_track_players.exe --create-chunks <video>` — regenera los chunks
  (y `chunks.json`). Ojo: borra los CSVs existentes de ese partido.
- `pepe_track_players.exe --track-chunks <video>` — regenera los CSVs de
  tracking por chunk (respeta las exclusiones de marcadores).
- `pepe_track_players.exe --infer-ids <video>` — corre la inferencia de
  identidades sobre todos los chunks y escribe la metadata por chunk.
