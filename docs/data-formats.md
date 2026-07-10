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
Un registro por video abierto. `status` avanza:
`registered → preprocessed → chunked → tracked`.
```json
{
  "matches": [
    {
      "id": 1,
      "dir": "match_0001",
      "video": "D:/ruta/partido.mp4",
      "status": "tracked",
      "fps": 29.92,
      "total_frames": 1801,
      "preprocessed": "match_0001/preprocessed_20fps.mp4",
      "chunks": 2,
      "updated": "2026-07-10T07:29:17"
    }
  ]
}
```

### match_<id con 4 ceros>/  (p. ej. `match_0001/`)

| Archivo | Contenido |
|---|---|
| `markers.json` | Marcadores de frame: `{ "markers": [ { "type": "match_start", "frame": 0 }, ... ], "fps": 29.92 }`. Tipos: `match_start`, `match_end`, `lineup_a`, `lineup_b`, `bench_a`, `bench_b`, `commercial_start`, `commercial_end`. |
| `preprocessed_20fps.mp4` | Video fuente re-muestreado a 20 fps (mp4v). |
| `video_chunks/video_part_<NNN>.mp4` | Chunks de 1 minuto a 10 fps (600 frames), numerados `001, 002, …` desde el preprocesado si existe. |
| `video_chunks/video_part_<NNN>.csv` | Tracking del chunk correspondiente. |

### video_part_<NNN>.csv
```
frame,time_sec,track_id,x,y,w,h,conf
0,0.00,1,674,83,141,392,0.91
```
- `frame`: índice local al chunk (0–599, a 10 fps).
- `time_sec`: tiempo **absoluto** del video (`(NNN-1)·60 + frame/10`).
- `track_id`: id del tracker IoU, local al chunk (los tracks no cruzan chunks).
- `x,y,w,h`: bounding box en píxeles del video.
- `conf`: confianza de la detección YOLOv8 (0–1).
- Los frames cuyo `time_sec` cae dentro de un rango
  `commercial_start`–`commercial_end` **no aparecen** (se descartan del
  tracking). Un `commercial_start` sin cierre descarta hasta el final.
