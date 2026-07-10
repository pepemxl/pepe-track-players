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
- `timecode(double sec) → QString` — formatea `HH:MM:SS.FF` usando el fps real.
- `addTag(vx, vy, team, rosterRow)` — crea un `TagEvent` en el frame actual
  con coordenadas de video en píxeles; si la homografía está verificada
  añade coordenadas de cancha en metros (`imageToPitch`).
- `saveProject() → bool` — escribe `project.json`, `tags.csv` y `tracks.csv`
  en `<carpeta del video>/<nombre>_project/`; limpia `dirty`.

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
- `preprocess()` / `createChunks()` / `trackChunks()` / `cancelOp()` —
  lanzan una operación en `VideoOpsWorker` (una a la vez).
- `commercialRangesSec()` (privada) — empareja `commercial_start`/`_end` en
  orden de frame (un start sin cerrar corre hasta el final del video);
  estos rangos se excluyen del tracking de chunks.
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
  3 dígitos desde 001; borra chunks de corridas anteriores).
- `runTrack()` — por cada chunk corre `YoloDetector` frame a frame con un
  tracker IoU local (los tracks no cruzan chunks) y escribe
  `video_part_<NNN>.csv` con `frame,time_sec,track_id,x,y,w,h,conf`
  (`time_sec` absoluto del video). `inCommercial(sec)` omite por completo
  los frames dentro de rangos comerciales.
- Señales: `progressChanged(frac, label)` y `opFinished(op, ok, error,
  result)` — entregadas queued al hilo GUI.
- `requestStop()` / `stopAndWait()` — cancelación cooperativa (el
  preprocess cancelado borra el archivo parcial).
