# LABQ8: hoja de ruta

Cada fase termina con build limpia, pruebas reproducibles y un informe de revisión.

| Fase | Alcance | Estado |
|---|---|---|
| **E0** | Base: CMake/JUCE, 1 banda Bell, parámetros + estado, mono/estéreo, editor genérico, pruebas DSP | Entregada para revisión |
| **E1** | De 1 a 8 bandas estáticas, tipos de filtro, ganancia de salida, bypass global con rampa | Propuesta abajo |
| E2 | Validación de formatos (auval, pluginval, Logic) en entorno aislado, medición de CPU, empaquetado | — |
| E3 | Dynamic Engine: detector y envolvente por banda, ganancia dinámica sobre la Bell | — |
| E4 | Procesamiento M/S y colocación L/R/M/S por banda | — |
| E5 | Analysis Engine: FFT pre/post fuera del hilo de audio (FIFO lock-free → hilo de análisis) | — |
| E6 | UI definitiva (rack digital 80/90, curva compuesta, analizador) | — |
| E7 | Intention Engine: perfiles (p. ej. DRUMS / DARK ROOM, GUITAR / SCREAMING SOLO, BASS / AFTERPUNK COLOUR) como sugerencias, nunca escritura directa desde el hilo de audio | — |
| E8 | Variante de mastering (fase lineal opcional, mayor resolución, mayor latencia aceptable) | — |

## Propuesta concreta para E1: de una a ocho bandas estáticas

### DSP

1. Generalizar `BellBand` a `EqBand`, con `BandSettings { type, freq, gain, q, slope, bypassed }`.
   - Tipos iniciales: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch.
   - Cortes de 12 dB/oct en un biquad. Pendientes mayores como cascada de
     2–4 secciones de tamaño fijo (`std::array`, sin asignación).
   - Se mantienen el suavizado log2 y la rejilla absoluta de 16 muestras.
2. **Cambio de tipo sin click:** no se pueden interpolar coeficientes entre
   tipos distintos. Propuesta: crossfade de 20 ms entre la instancia vieja y
   la nueva (dos estados por banda durante el fundido). Pruebas con la misma
   métrica de 4.ª diferencia.
3. `EqCore` con `std::array<EqBand, 8>` en serie. Las bandas en bypass o a 0
   dB quedan idle y no cuestan CPU.
4. `out_gain` con rampa y `master_bypass` con rampa, expuesto como
   `getBypassParameter()` para que el bypass del host no produzca click.
5. Una función pura `magnitudeAt(f)` por banda y otra compuesta, que servirá
   después a la curva de la UI y a las pruebas.

### Parámetros y estado

6. Generar los IDs `b{n}_*` para n = 1..8 con la convención de
   PARAMETER_SCHEMA.md. Los cuatro `b1_*` de E0 no cambian.
7. Fijar las frecuencias por defecto y si b1/b8 empiezan como cortes. Hay
   que decidirlo antes de distribuir.
8. Subir `labq8StateVersion` a 2 y migrar: un estado v1 carga b1 tal cual y
   reinicia b2–b8 y los globales a sus valores por defecto antes de
   `replaceState` (riesgo 3).

### Pruebas nuevas

9. Respuesta de cada tipo frente a su prototipo analógico (1e-4 dB) y medición con senoides (0,01 dB).
10. Respuesta compuesta de 8 bandas = producto de las respuestas individuales.
11. Ocho bandas a 0 dB / en bypass = identidad bit a bit.
12. Cambio de tipo sin click (≥ 20 dB frente al control sin crossfade).
13. Migración de estado v1 → v2 y round-trip v2.
14. Benchmark de CPU (sin umbral de fallo en E1, solo informe): 8 bandas en
    rampa continua a 96 kHz estéreo.

### Fuera de alcance en E1

Dinámica, M/S, análisis FFT, perfiles, GUI definitiva, instalación de plugins.
