# PepeTrack — Football Tracker

Aplicación de análisis deportivo (Qt 6 QML + C++ + OpenCV) para trackear
jugadores de fútbol sobre video. UI con layout tipo IDE: reproductor central + 4 secciones.

## Secciones

| Sección | Qué hace |
|---|---|
| **Video** | Reproductor OpenCV (play/pausa, scrub, timecode). Click sobre el video abre el dropdown de tagging con el roster; cada tag guarda frame, coordenadas de video y — si hay homografía — coordenadas de cancha en metros. Strip de calidad de tracking por frame. |
| **Homography** | Puntos A/B/C/D seleccionables (panel derecho) y colocables con click sobre el frame. Recompute H resuelve la homografía imagen→cancha (105×68 m, DLT de 4 puntos), reporta error de reproyección y estado VERIFIED. Overlay de quad + diagonales + línea media. Stepper de frames ‹ › y saltos rápidos. |
| **Metadata** | Formulario del partido (liga, temporada, fecha, sede, árbitro) y tablas editables de roster de ambos equipos (agregar / editar en línea / borrar). |
| **Tracking** | Inferencia offline sobre todo el video con YOLOv8n ONNX (`cv::dnn`, clase person) + asociación IoU. Start/Stop, progreso, stat cards, status por frame (90 buckets) y tabla de tracks (frames, confianza, estado). |

El chip del top bar indica `PROJECT SAVED` / `UNSAVED CHANGES`; click (o
`Ctrl+S`) guarda `project.json`, `tags.csv` y `tracks.csv` en
`<carpeta del video>/<nombre>_project/`. El proyecto se recarga al reabrir
el mismo video.

## Arquitectura (patrones de `videopp`)

- **`VideoEngine`** (`QThread`): posee el `cv::VideoCapture` en `run()`;
  controles vía atomics + `QWaitCondition` (pausa, seek absoluto, step por
  frames, velocidad). Nunca bloquea la GUI. Igual patrón que
  `VideoProcessor` de videopp.
- **`FrameProvider`** (`QQuickImageProvider`): el frame decodificado cruza
  al QML como `image://videoframe/<serial>`.
- **`TrackingManager`** (`QThread`): abre su propio capture, corre YOLOv8n
  (letterbox 640 + NMS, mismo decode que el `PoseDetector` de videopp) cada
  5 frames y publica snapshots (stats/chips/tracks) como señales queued.
- **`HomographyManager`**: DLT 4 puntos con `cv::solve` (el build de OpenCV
  no trae calib3d); `imageToPitch()` para los tags.
- **Modelos QML**: `RosterModel` ×2, `TagsModel`, `TracksModel`
  (`QAbstractListModel`) y `MatchMetadata` (`Q_PROPERTY`).
- **`AppController`**: fachada expuesta a QML como context property `App`.
- **QML**: módulo estático `PepeTrack` en `qml/` (Theme singleton + 4 vistas).

## Build (Windows, MinGW)

Requisitos: Qt 6.11 (mingw_64) en `C:\Qt`, y el árbol vcpkg para OpenCV 4.12
con ffmpeg+dnn.

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
