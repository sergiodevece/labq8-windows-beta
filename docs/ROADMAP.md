# LABQ8: hoja de ruta

Cada fase termina con build limpia, pruebas reproducibles y un informe de revisión.

| Fase | Alcance | Estado |
|---|---|---|
| **E0** | Base: CMake/JUCE, 1 banda Bell, parámetros + estado, mono/estéreo, editor genérico, pruebas DSP | Cerrada (`dfcf25b`) |
| **E1a** | 8 bandas Bell estáticas en cascada, estado v2 con migración E0, pruebas de cascada, asignaciones y CPU | Cerrada (`9095401`) |
| **E1b** | Seis topologías por banda (Bell, Low/High Shelf, Low/High Cut 12 dB/oct, Notch), crossfade lineal de 20 ms entre tipos, bypass por topología, estado v3 | Cerrada (`1e8db0b`) |
| **E1b.1** | Control de resonancias: Q efectivo máximo 1 en shelves y 2 en cortes (Q guardado intacto), barridos de respuesta, estabilidad y headroom | Cerrada (`5e889ec`) |
| **E2a** | Prototipo DSP de una banda Bell dinámica (detector, ley de reducción, balística), sin parámetros públicos nuevos; ruta estática intacta | Entregada para revisión |
| E2b / E2c | Dinámica en las ocho bandas y detección estéreo enlazada (propuesta en ARCHITECTURE.md) | — |
| E3 | Validación de formatos (auval, pluginval, Logic) en entorno aislado, empaquetado y firma | — |
| E4 | M/S y colocación L/R/M/S por banda | — |
| E5 | Analysis Engine: FFT fuera del hilo de audio (FIFO lock-free) | — |
| E6 | UI definitiva (rack digital 80/90) | — |
| E7 | Intention Engine: perfiles como sugerencias | — |
| E8 | Variante de mastering | — |

## Pendiente tras E2a (no iniciado, sin fase asignada)

- Mostrar el Q efectivo en la UI definitiva (propuesta en ARCHITECTURE.md).
- Pendientes seleccionables para los cortes (`b{n}_slope`), `out_gain` y
  `master_bypass` con rampa.
- Precalentar la ruta nueva en los crossfades (reduciría el transitorio de
  arranque con estado cero).

Fuera de alcance hasta su fase: dinámica, M/S, FFT, perfiles, GUI
definitiva e instalación de plugins.
