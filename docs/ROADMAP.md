# LABQ8: hoja de ruta

Cada fase termina con build limpia, pruebas reproducibles y un informe de revisión.

| Fase | Alcance | Estado |
|---|---|---|
| **E0** | Base: CMake/JUCE, 1 banda Bell, parámetros + estado, mono/estéreo, editor genérico, pruebas DSP | Cerrada (`dfcf25b`) |
| **E1a** | 8 bandas Bell estáticas en cascada, estado v2 con migración E0, pruebas de cascada, asignaciones y CPU | Entregada para revisión |
| E1b | Tipos de filtro (Shelf, Cut, Notch) con crossfade al cambiar de tipo, `out_gain`, `master_bypass` con rampa | Propuesta abajo; no iniciada |
| E2 | Validación de formatos (auval, pluginval, Logic) en entorno aislado, empaquetado y firma | — |
| E3 | Dynamic Engine: detector y envolvente por banda | — |
| E4 | M/S y colocación L/R/M/S por banda | — |
| E5 | Analysis Engine: FFT fuera del hilo de audio (FIFO lock-free) | — |
| E6 | UI definitiva (rack digital 80/90) | — |
| E7 | Intention Engine: perfiles como sugerencias | — |
| E8 | Variante de mastering | — |

## E1b (propuesta, no iniciada)

1. Generalizar `BellBand` a `EqBand` con `type` y `slope`. Pendientes mayores
   de 12 dB/oct como cascada de secciones de tamaño fijo.
2. Cambio de tipo sin click mediante crossfade de 20 ms entre dos instancias
   por banda, probado con la métrica de la 4.ª diferencia.
3. Parámetros nuevos `b{n}_type`, `b{n}_slope`, `out_gain` y `master_bypass`
   (hint 3), con estado v3 y migración v2 → v3 (tipo Bell para todo lo
   existente, de modo que las sesiones E0/E1a suenen igual).
4. Prueba de cada tipo frente a su prototipo analógico y medición con senoides.
5. Decidir antes de distribuir el orden de frecuencias por defecto (ver
   PARAMETER_SCHEMA.md).

Fuera de alcance también en E1b: dinámica, M/S, FFT, perfiles, GUI
definitiva e instalación de plugins.
