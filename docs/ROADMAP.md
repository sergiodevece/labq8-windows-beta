# LABQ8: hoja de ruta

Cada fase termina con build limpia, pruebas reproducibles y un informe de revisión.

| Fase | Alcance | Estado |
|---|---|---|
| **E0** | Base: CMake/JUCE, 1 banda Bell, parámetros + estado, mono/estéreo, editor genérico, pruebas DSP | Cerrada (`dfcf25b`) |
| **E1a** | 8 bandas Bell estáticas en cascada, estado v2 con migración E0, pruebas de cascada, asignaciones y CPU | Cerrada (`9095401`) |
| **E1b** | Seis topologías por banda (Bell, Low/High Shelf, Low/High Cut 12 dB/oct, Notch), crossfade lineal de 20 ms entre tipos, bypass por topología, estado v3 | Cerrada (`1e8db0b`) |
| **E1b.1** | Control de resonancias: Q efectivo máximo 1 en shelves y 2 en cortes (Q guardado intacto), barridos de respuesta, estabilidad y headroom | Cerrada (`5e889ec`) |
| **E2a** | Prototipo DSP de una banda Bell dinámica (detector, ley de reducción, balística), sin parámetros públicos nuevos; ruta estática intacta | Cerrada (`111acab`) |
| **E2b** | Motor dinámico de ocho bandas en `EqCore`: secciones independientes, detección sobre la señal original común, estéreo enlazado por máximo; solo DSP y pruebas, sin parámetros públicos | Cerrada (`5623dc5`) |
| **E2c.1** | Parámetros públicos de dinámica (40 nuevos, version hint 4, índices 40–79, rangos del DSP validado) y estado v4 con migración | Cerrada (`3041019`) |
| **UI-1** | Interfaz del diseño aprobado APPROVED_01: fondo derivado con escalas corregidas, panel compartido de 8 bandas, bypass desde el indicador, Dynamic EQ solo en Bell, pantalla ±24 dB con la respuesta real; controles sin DSP inactivos | Cerrada (`13a810f`); SMOKE-1 en Logic satisfactorio (auval arm64/x86_64) |
| **UI-1.1** | Ergonomía tras SMOKE-1: zoom en seis tamaños 40–140 % (60 % inicial, preferencia global, menú del icono de configuración, sin tirador de esquina, tamaños que no caben deshabilitados), ocho botones ON/OFF de bypass, selección y edición de bandas desde los nodos con tiradores de Q; sin cambios de DSP, parámetros ni estado | Cerrada; SMOKE-2 y SMOKE-3 en Logic aprobados (UI_IMPLEMENTATION §9.1, §9.5), zoom en seis tamaños desde §9.4 |
| **UI-2a** | Medidores IN/OUT reales: pico de muestra antes y después del EQ, transporte lock-free, balística en la interfaz, 26 celdas con la escala del diseño, verde / ámbar / rojo, mono en L y R, bypass del host medido; sin cambios de DSP, parámetros ni estado | Cerrada; SMOKE-4 en Logic aprobado (UI_IMPLEMENTATION §10.1) |
| UI-2 (resto) | Curva dinámica, presentación provisional de INTENTION (según decisiones de UI-1) | — |
| E2c.2 | Mejora del detector: Q del detector limitada en graves (propuesta medida en DYNAMICS_E2C1.md) | — |
| E2c (resto) | Detección estéreo no enlazada / M/S | — |
| E3 | Validación de formatos (auval, pluginval, Logic) en entorno aislado, empaquetado y firma | — |
| E4 | M/S y colocación L/R/M/S por banda (dirección de producto: `docs/specs/LABQ8_INTENTION_PHASE_MS_v01.md`, §1) | — |
| E5 | Analysis Engine: FFT fuera del hilo de audio (FIFO lock-free) | — |
| E6 | UI definitiva: completar lo que UI-1 deja pendiente (analizador, M/S, INTENTION en pantalla) | — |
| E7 | Intention Engine: perfiles como sugerencias (dirección de producto: `docs/specs/LABQ8_INTENTION_PHASE_MS_v01.md`, §2) | — |
| E8 | Variante de mastering | — |

## Especificación conceptual: INTENTION, PHASE BEHAVIOR y M/S

`docs/specs/LABQ8_INTENTION_PHASE_MS_v01.md` (v0.1, 30-10-2026) recoge la
dirección de producto aprobada. **No autoriza ninguna implementación**: cada
etapa requiere su propia fase y autorización, después de UI-1.1. Fase de
PHASE BEHAVIOR por asignar.

- **Decisiones conceptuales aprobadas:** sustituir L/R por MID/SIDE como
  selector del banco editado en modo M/S (un banco de ocho bandas por canal
  M/S); los 12 presets de INTENTION por nombre y objetivo (no por valores);
  PHASE BEHAVIOR con dos escenarios complementarios, análisis interno de una
  pista y comparación con una señal externa por sidechain; flujo
  ANALYZE → PROPOSE → APPLY, con aplicación solo tras aprobación del usuario,
  A/B y deshacer.
- **Pendiente de diseño técnico:** arquitectura M/S, migración de estado y
  parámetros, automatización por banco y entradas mono; valores reales de los
  presets, estado Modified, presets de usuario y alcance en M/S; NATIVE
  (selector), PHASE MAP y MONO WATCH (medición y visualización, sin procesado);
  sidechain externo (buses, límites del host, latencia, fiabilidad de la
  medida) para LOW END LOCK y MULTI-MIC ALIGN.
- **Experimental (investigación y validación musical):** calibración de
  presets y umbrales dinámicos con material real; LOW END LOCK y MULTI-MIC
  ALIGN como I+D; PHASE SCULPT (modelado de fase por regiones, p. ej.
  all-pass), sujeto a viabilidad, utilidad musical, estabilidad y CPU. Una
  variante de fase lineal no está aprobada.

## Pendiente tras UI-1 y UI-1.1 (no iniciado, sin fase asignada)

- Registrar en Logic la automatización (botones y nodos) y la persistencia
  de sesión de UI-1.1 (UI_IMPLEMENTATION §9.2); cubiertas por pruebas fuera
  de Logic.

- Mostrar el Q efectivo en la UI definitiva (propuesta en ARCHITECTURE.md).
- Resolver en la fase de interfaz las escalas de APPROVED_01 frente a los
  rangos públicos (PARAMETER_SCHEMA.md).
- Pendientes seleccionables para los cortes (`b{n}_slope`), `out_gain` y
  `master_bypass` con rampa.
- Precalentar la ruta nueva en los crossfades (reduciría el transitorio de
  arranque con estado cero).

Fuera de alcance hasta su fase: dinámica, M/S, FFT, perfiles, GUI
definitiva e instalación de plugins.
