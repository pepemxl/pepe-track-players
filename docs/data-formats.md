# Formatos de datos

## Proyecto por video — `<carpeta del video>/<nombre>_project/`

Se escribe con **Ctrl+S** (o click en el chip de guardado) y se recarga
automáticamente al reabrir el mismo video.

### project.json
```json
{
  "video": "D:/ruta/partido.mp4",
  "metadata":   { "homeTeam": "...", "awayTeam": "...", "league": "...",
                  "season": "...", "competition": "...", "date": "...",
                  "venue": "...", "referee": "..." },
  "homeRoster": [ { "number": 5, "name": "R. Solano", "position": "CDM" } ],
  "awayRoster": [ ... ],
  "tags":       [ { "frame": 0, "timecode": "00:00:00.00", "playerNumber": 5,
                    "playerName": "R. Solano", "team": 0, "x": 430.5, "y": 258.3,
                    "pitchX": 55.9, "pitchY": 43.2, "hasPitch": true } ],
  "homography": { "points": [ { "id": "A", "ix": 170, "iy": 72, "px": 0, "py": 0 }, ... ],
                  "verified": true }
}
```

### tags.csv
`frame,timecode,team,player_number,player_name,x,y,pitch_x,pitch_y` —
`x,y` en píxeles de video; `pitch_x,pitch_y` en metros (vacíos si no había
homografía verificada al taggear).

### tracks.csv
`track_id,frames_tracked,avg_conf,status` — snapshot de la tabla del tab
Tracking al momento de guardar.

---

## Registro de partidos — `LOCAL_DATA/matches/`

Raíz configurable con la variable de entorno `PEPETRACK_DATA_DIR`
(default: `LOCAL_DATA/` en el root del proyecto).

### games.json
Un registro por partido (proyecto), con uno o más videos por partido.
Cada video tiene rol (`tv_feed` | `tactical` | `panoramic` | `other`),
**segmento** (`full` | `first_half` | `second_half` | `extra1` | `extra2`
| `penalties` | `partial_first_half` | `partial_second_half` — indica que
parte del partido cubre el video, y por tanto si contiene su inicio/fin),
status propio (`registered → preprocessed → chunked → tracked`) y crop
opcional (la vista seleccionada dentro de un video multi-vista).
```json
{
  "matches": [
    {
      "id": 2,
      "dir": "match_0002",
      "updated": "2026-07-10T17:56:51",
      "videos": [
        {
          "id": 1,
          "role": "tv_feed",
          "segment": "full",
          "path": "D:/ruta/partido.mp4",
          "status": "tracked",
          "fps": 60,
          "total_frames": 42662,
          "preprocessed": "match_0002/preprocessed_20fps_01.mp4",
          "chunks": 12,
          "crop": { "x": 100, "y": 50, "w": 960, "h": 540 }
        }
      ]
    }
  ]
}
```
Las entradas con el formato viejo (campo `video` a nivel de match) se
normalizan automáticamente al abrirlas.

### match_<id con 4 ceros>/  (p. ej. `match_0002/`)

Todos los artefactos que dependen del video llevan el sufijo
`_<video id con 2 ceros>` (`_01`, `_02`, ...). Los archivos previos al
sufijo se migran automaticamente al video 1.

| Archivo | Contenido |
|---|---|
| `markers_<NN>.json` | Marcadores de frame: `{ "markers": [ { "type": "match_start", "frame": 0 }, ... ], "fps": 29.92 }`. Tipos: `match_start`/`match_end`, `first_half_start`/`first_half_end`, `second_half_start`/`second_half_end`, `extra1_start`/`extra1_end`, `extra2_start`/`extra2_end`, `penalties_start`/`penalties_end`, `lineup_a`, `lineup_b`, `bench_a`, `bench_b`, `commercial_start`, `commercial_end`. Cada par `*_start`/`*_end` (salvo comerciales) delimita una ventana de juego: el tracking solo corre dentro de la union de ventanas (el medio tiempo entre `first_half_end` y `second_half_start` queda excluido). |
| `preprocessed_20fps_<NN>.mp4` | Video fuente re-muestreado a 20 fps (mp4v), **recortado a la vista (crop)** si el video la tiene definida. |
| `lineups_<NN>.json` | Resultado del OCR de alineaciones: `{ "teamA": [{number,name}...], "teamB": [...], "teamNameA": "ARGENTINA", "teamNameB": "" }`. Al abrir el video en la GUI se aplica a los rosters/nombres de Metadata **si es más reciente** que el `project.json` guardado (las ediciones manuales ganan). |
| `lineups_<NN>/<tipo>_f<frame>.bmp` | Frames capturados para el OCR (con crop aplicado si existe). |
| `track_assignments_<NN>.json` | Jugadores asignados a chunk-tracks (click dentro de una caja en el reproductor): `{ "assignments": { "002-T31": { "number": 10, "name": "L. MESSI", "team": 0 } } }`. |
| `video_chunks_metadata_<NN>/video_metadata_part_<NNN>.json` | Asignaciones por chunk (manuales + inferidas): `{ "chunk": 3, "file": "video_part_003.mp4", "generated": "...", "assignments": { "003-T717": { "number": 10, "name": "LIONEL MESSI", "team": 0, "source": "manual" }, "003-T821": { ..., "source": "inferred" } } }`. Se regenera al asignar/desasignar y al correr la inferencia de identidades. |
| `video_chunks_<NN>/chunks.json` | Índice de chunks: por chunk `number`, `file`, `frames`, `start_sec`, `end_sec` (tiempo absoluto del video; el último chunk suele ser parcial), más `fps` y `chunk_seconds` globales. |
| `video_chunks_<NN>/video_part_<NNN>.mp4` | Chunks de 1 minuto a 10 fps (600 frames), numerados `001, 002, …` desde el preprocesado si existe. |
| `video_chunks_<NN>/video_part_<NNN>.csv` | Tracking del chunk correspondiente (coordenadas en el espacio de la vista recortada). |

### video_part_<NNN>.csv
```
frame,time_sec,track_id,x,y,w,h,conf,chunk_start_sec,chunk_end_sec
0,0.00,1,674,83,141,392,0.91,0.00,60.00
```
- `frame`: índice local al chunk (0–599, a 10 fps).
- `time_sec`: tiempo **absoluto** del video (`(NNN-1)·60 + frame/10`).
- `track_id`: id del tracker IoU, local al chunk (los tracks no cruzan chunks).
- `x,y,w,h`: bounding box en píxeles del video.
- `conf`: confianza de la detección YOLOv8 (0–1).
- `chunk_start_sec,chunk_end_sec`: inicio y fin del chunk en tiempo absoluto
  del video (el fin es real: el último chunk parcial no reporta 60 s).
- Los frames cuyo `time_sec` cae en un rango excluido (antes de
  `match_start`, después de `match_end`, o dentro de
  `commercial_start`–`commercial_end`) **no aparecen**. Un
  `commercial_start` sin cierre descarta hasta el final.
