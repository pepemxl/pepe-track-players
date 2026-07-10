# PepeTrack — Football Tracker

Aplicación de análisis deportivo (Qt 6 QML + C++ + OpenCV) para trackear
jugadores de fútbol sobre video. UI con layout tipo IDE: reproductor central + 4 secciones.

## Secciones

| Sección | Qué hace |
|---|---|
| **Video** | Reproductor OpenCV (play/pausa con `Space`, scrub, ←/→ = ±5 s, timecode, selector de velocidad: nativa o 1/5/10/20/30/60/120 frames/s). Click sobre el video abre el dropdown de tagging con el roster; cada tag guarda frame, coordenadas de video y — si hay homografía — coordenadas de cancha en metros. Strip de calidad de tracking por frame. Panel izquierdo colapsable de operaciones (ver abajo). |
| **Homography** | Puntos A/B/C/D seleccionables (panel derecho) y colocables con click sobre el frame. Recompute H resuelve la homografía imagen→cancha (105×68 m, DLT de 4 puntos), reporta error de reproyección y estado VERIFIED. Overlay de quad + diagonales + línea media. Stepper de frames ‹ › y saltos rápidos. |
| **Metadata** | Formulario del partido (liga, temporada, fecha, sede, árbitro) y tablas editables de roster de ambos equipos (agregar / editar en línea / borrar). |
| **Tracking** | Inferencia offline sobre todo el video con YOLOv8n ONNX (`cv::dnn`, clase person) + asociación IoU. Start/Stop, progreso, stat cards, status por frame (90 buckets) y tabla de tracks (frames, confianza, estado). |

El chip del top bar indica `PROJECT SAVED` / `UNSAVED CHANGES`; click (o
`Ctrl+S`) guarda `project.json`, `tags.csv` y `tracks.csv` en
`<carpeta del video>/<nombre>_project/`. El proyecto se recarga al reabrir
el mismo video.

## Panel de operaciones de video (vista Video, lado izquierdo)

Cada video abierto se registra con un id en `LOCAL_DATA/matches/games.json`
(status y referencias) y obtiene su carpeta
`LOCAL_DATA/matches/match_<id con 4 ceros>/` (p. ej. `match_0001/`).

- **Frame markers**: marca el frame actual como inicio/fin del partido,
  alineación A/B, banca A/B, o inicio/fin de comercial. Se guardan en
  `match_<id>/markers.json`, se listan en el panel (click → seek, × borra)
  y se dibujan como ticks de color en el scrubber. **Ambos trackings**
  (tab Tracking y Track chunks) saltan los frames antes de `match_start`,
  después de `match_end` y dentro de comerciales; el Range chip del tab
  Tracking refleja la ventana del partido.
- **Extract lineups (OCR)**: con marcadores de Lineup/Bench, extrae de esos
  frames los dorsales y nombres de ambos equipos usando el OCR de Windows
  y llena las tablas de roster (guardando `match_<id>/lineups.json` y los
  frames en `lineups/`). También disponible headless:
  `pepe_track_players.exe --extract-lineups <video>`.
- **Preprocess → 20 fps**: re-encodea el video fuente a 20 fps en
  `match_<id>/preprocessed_20fps.mp4` (status → `preprocessed`).
- **Create chunks · 1 min @ 10 fps**: parte el video (el preprocesado si
  existe) en chunks de 1 minuto a 10 fps en
  `match_<id>/video_chunks/video_part_<NNN>.mp4` (status → `chunked`).
- **Track chunks → CSV**: corre YOLOv8 + asociación IoU sobre cada chunk y
  escribe `video_chunks/video_part_<NNN>.csv` con columnas
  `frame,time_sec,track_id,x,y,w,h,conf` (tiempo absoluto del video;
  frames dentro de comerciales se omiten; status → `tracked`).

Las operaciones corren en un hilo worker con barra de progreso y botón
Cancel; el status y el conteo de chunks se actualizan en `games.json`.
La raíz de datos se puede mover con la variable de entorno
`PEPETRACK_DATA_DIR` (default: `LOCAL_DATA/` junto al proyecto).

## Arquitectura (patrones de `videopp`)

- **`VideoEngine`** (`QThread`): posee el `cv::VideoCapture` en `run()`;
  controles vía atomics + `QWaitCondition` (pausa, seek absoluto, step por
  frames, velocidad). Nunca bloquea la GUI. Igual patrón que
  `VideoProcessor` de videopp.
- **`FrameProvider`** (`QQuickImageProvider`): el frame decodificado cruza
  al QML como `image://videoframe/<serial>`.
- **`YoloDetector`**: detector de personas YOLOv8n ONNX sobre `cv::dnn`
  (letterbox 640 + NMS, mismo decode que el `PoseDetector` de videopp),
  compartido por el tracking interactivo y el de chunks.
- **`TrackingManager`** (`QThread`): abre su propio capture, detecta cada
  5 frames y publica snapshots (stats/chips/tracks) como señales queued.
- **`MatchManager`** + **`VideoOpsWorker`** (`QThread`): registro en
  `games.json`, marcadores de frame, y las operaciones offline
  (preprocess 20 fps / chunks 10 fps / tracking por chunk a CSV).
- **`HomographyManager`**: DLT 4 puntos con `cv::solve` (el build de OpenCV
  no trae calib3d); `imageToPitch()` para los tags.
- **Modelos QML**: `RosterModel` ×2, `TagsModel`, `TracksModel`
  (`QAbstractListModel`) y `MatchMetadata` (`Q_PROPERTY`).
- **`AppController`**: fachada expuesta a QML como context property `App`.
- **QML**: módulo estático `PepeTrack` en `qml/` (Theme singleton + 4 vistas).

## Documentación

Referencia detallada de funciones y formatos en `docs/`:

- [`docs/backend.md`](docs/backend.md) — clases C++ y sus funciones
  (AppController, VideoEngine, modelos, HomographyManager, YoloDetector,
  TrackingManager, MatchManager, VideoOpsWorker).
- [`docs/qml.md`](docs/qml.md) — componentes QML: responsabilidades,
  propiedades y cómo se conectan con `App`.
- [`docs/data-formats.md`](docs/data-formats.md) — `project.json`,
  `tags.csv`, `tracks.csv`, `games.json`, `markers.json` y los CSV de
  chunks (`video_part_<NNN>.csv`).

## Build (Windows, MinGW)

Requisitos: Qt 6.11 (mingw_64) en `C:\Qt` y vcpkg en `C:\vcpkg`
(`git clone https://github.com/microsoft/vcpkg C:\vcpkg` +
`bootstrap-vcpkg.bat`). OpenCV 4.12 (ffmpeg+dnn) se instala solo vía el
manifest `vcpkg.json` en la primera configuración (~15 min; queda en
`vcpkg_installed/` local al proyecto). Los DLLs de OpenCV/FFmpeg se copian
junto al exe (deployment applocal de vcpkg), así que `build\bin` queda
autocontenido.

```bat
build.bat   :: configura (Ninja + vcpkg toolchain) y compila a build\bin
run.bat     :: lanza con los DLLs de OpenCV/MinGW en PATH
run.bat "C:\ruta\al\partido.mp4"  :: abre un video al arrancar
```

O con el Makefile (`mingw32-make` viene en `C:\Qt\Tools\mingw1310_64\bin`):

```bat
mingw32-make            :: configura (si hace falta) y compila
mingw32-make run        :: lanza la app
mingw32-make run VIDEO="C:\ruta\al\partido.mp4"
mingw32-make clean      :: borra el build
mingw32-make rebuild    :: clean + build
```

Las rutas del toolchain se pueden sobreescribir por variable, p. ej.
`mingw32-make QT_DIR=C:/Qt/6.12.0/mingw_64 BUILD_TYPE=Debug`.

El modelo `models/yolov8n.onnx` se copia junto al ejecutable en el
post-build.
