# LABQ8: arquitectura (E1b, E1b.1 y motor dinámico E2a–E2c.1)

## Capas

| # | Capa | Responsabilidad | Estado E1b | Ubicación |
|---|---|---|---|---|
| 1 | Plugin Host Adapter | JUCE `AudioProcessor`, buses, parámetros → ajustes DSP, estado y migración | Implementado | `src/plugin/`, `src/parameters/` |
| 2 | EQ Core | Filtros, suavizado, crossfades de tipo, bypass, cascada | 8 bandas × 6 topologías | `src/dsp/` |
| 3 | Dynamic Engine | Detectores, envolventes, control dinámico por banda | Ocho secciones en `EqCore` (E2b), con parámetros públicos desde E2c.1 ([DYNAMICS_E2A.md](DYNAMICS_E2A.md), [DYNAMICS_E2B.md](DYNAMICS_E2B.md)) | `src/dsp/Dynamic*`, `EqCore` |
| 4 | Analysis Engine | FFT, medición | No iniciado | — |
| 5 | Intention Engine | Perfiles musicales, sugerencias | No iniciado | — |
| 6 | UI Layer | Interfaz definitiva | Editor genérico de JUCE | (`GenericAudioProcessorEditor`) |

Las capas 3–6 no tienen código ni clases vacías.

### Reglas de dependencia

- **EQ Core no depende de JUCE.** `labq8_dsp` (C++20 puro) contiene
  `BandLimits`, `FilterTypes`, `BiquadDesign`, `BellBand`, `EqBand` y `EqCore`.
- **El Host Adapter no contiene DSP.** Lee los `std::atomic<float>` de los
  40 parámetros, construye un `BandSettings` por banda y llama a
  `EqCore::setBandTargets` / `process`.
- **La UI solo habla con el `AudioProcessorValueTreeState`.**
- Los rangos se definen una vez (`dsp/BandLimits.h`). El DSP vuelve a
  limitarlos porque no se fía de quien lo llama.

```
Host/DAW ──► LabQ8AudioProcessor ──► APVTS (40 parámetros, estado v3)
                │  pushParametersToCore()          ▲      ▲
                ▼                                  │      └─ migrateState() al restaurar
          dsp::EqCore                     GenericAudioProcessorEditor
            ├─ EqBand 1 ─► EqBand 2 ─► … ─► EqBand 8      (en serie, in-place)
            │
            EqBand: ruta activa  ─┐
                    ruta saliente ┴─► mezcla lineal durante 20 ms al cambiar de tipo
                    (cada ruta: biquad con rampas, coeficientes y estado por canal,
                     o ruta "seca" sin filtro)
```

## Arquitectura multi-filtro (E1b)

### Componentes

| Archivo | Papel |
|---|---|
| `dsp/FilterTypes.h` | `enum class FilterType` con índices congelados (0 Bell … 5 Notch), `usesGain()`, `filterTypeFromIndex()` (fuera de rango → Bell) |
| `dsp/BiquadDesign.*` | `designBell` (sin cambios desde E0) y los nuevos `designLowShelf`, `designHighShelf`, `designLowCut`, `designHighCut`, `designNotch`, más el despachador `designFilter` |
| `dsp/BellBand.*` | **Congelado desde E0.** Ya no está en el camino de audio; se conserva como implementación de referencia (oráculo) y las pruebas exigen que `EqBand` en modo Bell sea idéntico a él bit a bit |
| `dsp/EqBand.*` | Nueva banda: dos rutas, crossfade, política de cambios, bypass por topología |
| `dsp/EqCore.*` | 8 `EqBand` en serie. Conserva la interfaz E1a (`setBandTargets(int, BellSettings)`) además de la nueva (`BandSettings` con tipo) |

**Reutilización:** `designBell`, los límites, `clamp*`, `BiquadCoefficients`,
`magnitudeAt`/`isStable`, el esquema de suavizado (rampas de 20 ms y rejilla
absoluta de 16 muestras), la protección numérica, la cascada de `EqCore` y
la migración genérica de estado se reutilizan sin cambios. `EqBand` repite la
aritmética de `BellBand` (rampa, tick, segmento TDF-II) en lugar de modificar
`BellBand`. Así el código validado en E0 queda congelado y sirve de oráculo
independiente; la duplicación (~80 líneas) está cubierta por la prueba de
identidad bit a bit.

### Fórmulas (RBJ "Audio EQ Cookbook", R. Bristow-Johnson)

Comunes: `w0 = 2π·f0/fs`, `α = sin(w0)/(2Q)`, `A = 10^(dB/40)`. Todo se
normaliza por `a0`. Las entradas pasan por los mismos límites que en E0 (f0
entre 20 Hz y min(20 kHz, 0,49·fs); Q entre 0,1 y 18; ganancia ±24 dB; NaN/Inf →
valor por defecto).

| Tipo | b0 | b1 | b2 | a0 | a1 | a2 |
|---|---|---|---|---|---|---|
| Bell | 1+αA | −2cos w0 | 1−αA | 1+α/A | −2cos w0 | 1−α/A |
| Low Shelf | A[(A+1)−(A−1)cos+2√Aα] | 2A[(A−1)−(A+1)cos] | A[(A+1)−(A−1)cos−2√Aα] | (A+1)+(A−1)cos+2√Aα | −2[(A−1)+(A+1)cos] | (A+1)+(A−1)cos−2√Aα |
| High Shelf | A[(A+1)+(A−1)cos+2√Aα] | −2A[(A−1)+(A+1)cos] | A[(A+1)+(A−1)cos−2√Aα] | (A+1)−(A−1)cos+2√Aα | 2[(A−1)−(A+1)cos] | (A+1)−(A−1)cos−2√Aα |
| Low Cut (HPF) | (1+cos)/2 | −(1+cos) | (1+cos)/2 | 1+α | −2cos w0 | 1−α |
| High Cut (LPF) | (1−cos)/2 | 1−cos | (1−cos)/2 | 1+α | −2cos w0 | 1−α |
| Notch | 1 | −2cos w0 | 1 | 1+α | −2cos w0 | 1−α |

- `1 − cos w0` se calcula como `2·sin²(w0/2)` para no perder precisión con f0
  baja a fs alta (20 Hz a 96 kHz). `1 + cos w0 = 2 − (1 − cos w0)`.
- Cada diseño es exactamente la transformada bilineal, con prewarping en
  f0, de su prototipo analógico. Las pruebas comparan con estos prototipos
  evaluados en la frecuencia deformada
  `Ω = tan(πf/fs)/tan(πf0/fs)`, sin compartir código con el diseño:
  - Low Shelf: `A·(s² + s√A/Q + A) / (A s² + s√A/Q + 1)`
  - High Shelf: `A·(A s² + s√A/Q + 1) / (s² + s√A/Q + A)`
  - Low Cut: `s² / (s² + s/Q + 1)` · High Cut: `1 / (s² + s/Q + 1)` · Notch: `(s² + 1) / (s² + s/Q + 1)`
- **Shelves a 0 dB:** con A = 1, cada término b coincide con su a bit a bit
  (A−1 = 0, A+1 = 2, y escalar por 2 es exacto, también con FMA). La banda es
  una identidad exacta y puede pasar a idle, igual que la Bell.

### Significado de frecuencia, ganancia y Q por topología

| Tipo | Frecuencia | Ganancia | Q (guardado: 0,1–18 para todos; efectivo: ver "Q guardado y Q efectivo") |
|---|---|---|---|
| Bell | centro; \|H(f0)\| = ganancia | sí | ancho de banda RBJ (bordes a ganancia/2 en dB) |
| Low Shelf | punto medio: \|H(f0)\| = ganancia/2 dB para cualquier Q; DC = ganancia; Nyquist = 0 dB | sí | pendiente o resonancia de la transición. Q = 0,7071 equivale a la pendiente S = 1 de RBJ (la más pronunciada sin sobreoscilación). Por encima hay sobreoscilación: ver la tabla de abajo |
| High Shelf | ídem, simétrico (DC = 0 dB, Nyquist = ganancia) | sí | ídem |
| Low Cut | \|H(f0)\| = Q | **no** (ignorada) | resonancia del par de polos. 0,7071 = Butterworth (−3,01 dB en f0, plano); 0,5 = crítico (−6,02 dB); Q > 0,7071 da un pico de Q/√(1−1/(4Q²)) |
| High Cut | ídem | **no** | ídem |
| Notch | cero exacto en f0 (\|H\| < 1e-9) | **no** | f0 / ancho de banda entre los puntos de −3,01 dB |

Pendientes medidas en las asíntotas: Low Cut 12,04 dB/oct, High Cut 12,09
dB/oct (ideal 12,04; el exceso en el High Cut se debe al cero en Nyquist de la
bilineal).

**Estabilidad de los rangos:** todas las fórmulas RBJ con Q > 0 y f0 < fs/2
dan polos dentro del círculo unidad. Está comprobado en la rejilla completa
de tipos, f0, Q, ganancia y fs, incluido el límite de 0,49·fs a 32 kHz, con
respuesta al impulso que decae y entradas no válidas.

### Q guardado y Q efectivo (E1b.1)

**El Q que guarda el parámetro y el Q que usa el filtro pueden ser distintos.**
`b{n}_q` conserva su rango de 0,1 a 18 para todos los tipos (IDs, rango,
orden y hints sin cambios), y el estado guarda siempre el valor del usuario.
Cada banda diseña con `effectiveQ(tipo, Q guardado)`
([FilterTypes.h](../src/dsp/FilterTypes.h)):

| Tipo | Q efectivo máximo | Peor caso en ese límite (cualquier f0, fs y ganancia) |
|---|---|---|
| Bell | 18 (sin límite nuevo) | — |
| Low / High Shelf | **1** | sobreoscilación fuera de las mesetas ≤ **1,12 dB** a ±24 dB (0,44 dB a ±6 dB) |
| Low / High Cut | **2** | pico de resonancia **6,30 dB** (Q²/√(Q²−¼)) |
| Notch | 18 (sin límite nuevo) | — |

- **Regla:** Q efectivo = mín(Q guardado, límite·(1 + 10⁻⁶)). Por debajo del
  límite se usa el Q guardado bit a bit, así que Bell y Notch no cambian nada y
  los shelves y cortes por debajo de su límite suenan exactamente como en E1b.
- **La tolerancia de 1 ppm** existe porque el host guarda Q en float sobre un
  rango logarítmico: un "2,00" queda guardado como 2,0000002384. Sin ella, las
  sesiones E1b con un corte justo en Q 2 cambiarían en una fracción de ppm.
  La función sigue siendo continua y monótona.
- **El límite actúa sobre el objetivo de la rampa**, no sobre el valor
  suavizado. Por eso:
  - mover el Q guardado por encima del límite no cambia nada (salida
    idéntica bit a bit, comprobado con automatización aleatoria);
  - cruzar el límite (18 → 0,3) da exactamente la rampa de 20 ms de límite →
    0,3 que ya validaba E1b (comprobado bit a bit en las dos direcciones);
  - en un cambio de tipo, cada ruta usa el Q efectivo de su propio tipo: Bell
    Q 18 → Low Shelf diseña con Q 1, y volver a Bell recupera Q 18.
- **Lo que se ve desde fuera:** el host y la UI genérica muestran el Q
  guardado. `EqBand::getCurrentQ()` devuelve el efectivo. Sin UI propia, la
  única pista audible de la limitación es que subir Q por encima del límite
  deja de cambiar el sonido.

**Barridos** (`labq8_sweep`, [ResonanceSweep.cpp](../tests/bench/ResonanceSweep.cpp)):

- **Cobertura:** las fórmulas RBJ son la bilineal del prototipo analógico con
  prewarp en f0, así que la respuesta solo depende de f0/fs. El barrido
  recorre f0/fs de 20/768 000 a 0,49, cada punto diseñado con un par (f0, fs)
  admitido real. Eso cubre todas las frecuencias y frecuencias de muestreo.
- **Precisión:** la sobreoscilación digital coincide con la analógica (Q 1:
  2,7e-5 dB), luego **depende solo de ganancia y Q**.

| Q | Shelf ±6 dB | Shelf ±12 dB | Shelf ±24 dB | Cut: pico |
|---|---|---|---|---|
| 0,7071 | 0 | 0 | 0 | 0 dB |
| 1 | 0,44 | 0,78 | **1,12** | 1,25 |
| 1,5 | 1,64 | 2,76 | 3,70 | 4,03 |
| 2 | 2,87 | 4,58 | 5,88 | **6,30** |
| 5 | 8,60 | 11,66 | 13,48 | 14,02 |
| 18 (E1b) | 19,12 | 22,61 | 24,55 | 25,11 |

**Estabilidad:** son 48 000 diseños por tipo (Q de 0,1 a 18, ganancia −24/0/+24
y 400 puntos de f0/fs). Ninguno es inestable ni tiene coeficientes no finitos;
el radio de polo máximo es 0,9999989.

**Headroom:** la tabla da la norma L1 de la respuesta al impulso, es decir, la
ganancia máxima posible de pico a pico para cualquier entrada |x| ≤ 1. Es el
peor caso de una señal adversaria; una señal musical queda muy por debajo.

| Diseño | Q 0,7071 | Límite | Q 18 (E1b) |
|---|---|---|---|
| Low Shelf +24 dB | 30,3 dB | 31,6 dB | 51,0 dB |
| High Shelf +24 dB | 30,8 | 31,9 | 51,0 |
| Low Shelf −24 dB | 6,8 | 7,9 | 27,0 |
| High Shelf −24 dB | 6,3 | 7,6 | 27,0 |
| Low Cut | 7,7 | 11,7 | 27,6 |
| High Cut | 7,4 | 11,6 | 27,6 |
| Bell +24 dB, Q 18 (referencia) | — | 26,0 | — |

- **Shelves:** el límite añade como mucho 1,3 dB al peor caso de la Q sin
  sobreoscilación, y le quita 19–20 dB al de E1b.
- **Cortes:** el límite cuesta unos 4 dB sobre Butterworth y ahorra 16 dB.
- **Pruebas:** comprueban los dos valores con Q guardado 18, en extremos de f0 y
  fs, y con la entrada de ±1 que alcanza la cota (salida finita e igual a L1).

**Propuesta para la UI futura (no implementada):**

- **Mando de Q:** sigue mostrando y editando el Q guardado, para que el usuario
  no pierda su valor al cambiar de tipo.
- **Cuando Q guardado > Q efectivo:**
  - la lectura muestra los dos valores, "Q 4,00 → 1,00", con el efectivo en
    el color de acento y el guardado atenuado;
  - el anillo del mando marca el tramo por encima del límite con un rayado o
    un color apagado, de modo que se ve dónde deja de actuar;
  - un tooltip lo explica: "Low Shelf: Q efectivo máx. 1 (evita
    sobreoscilación). El valor guardado se recupera al cambiar a Bell".
- **Curva de respuesta:** se dibuja siempre con el Q efectivo (la del DSP), así
  que la pantalla y el sonido coinciden.
- **API:** la UI obtendría el valor con `dsp::effectiveQ(type, q)` (función
  pura, sin acceso al hilo de audio).

### Crossfades entre tipos

- **Rutas independientes.** Cada `EqBand` tiene dos rutas (`std::array`,
  tamaño fijo). Cada ruta es un biquad con sus rampas, coeficientes y estado
  por canal, o la ruta **seca** (sin filtro). Normalmente solo una está activa.
- **Al cambiar de tipo,** la ruta activa pasa a ser la saliente. La entrante
  arranca con estado cero y con los parámetros ya en su valor objetivo (sin
  rampa). Las dos siguen procesando, y las dos siguen los cambios de
  frecuencia, ganancia y Q, que son parámetros de la banda.
- **Ley:** salida = `old + w·(new − old)` calculado en double, con `w = k/N`
  por muestra (k = 1…N) y `N = round(0,02·fs)` (882 / 960 / 1764 / 1920
  muestras a 44,1 / 48 / 88,2 / 96 kHz). Es una combinación convexa: la ley
  nunca supera la mayor de las dos rutas (probado muestra a muestra). Una ley
  de igual potencia daría +3 dB con señales correladas; la mutación con esa
  ley hace fallar la prueba.
- **Final sin discontinuidad:** en `w = 1` la salida es exactamente la ruta
  nueva, y después del fade es idéntica bit a bit a esa ruta procesando sola.
- **Determinismo:** `N` se cuenta en muestras, así que no depende del tamaño de
  bloque (probado con 1…4096 y tamaños aleatorios). El tick de rampas sigue en
  la rejilla absoluta de 16 muestras de la banda.
- **Sin asignaciones, mutex ni esperas:** todo el estado es de tamaño fijo y
  se comprueba con el contador de `operator new` en `EqCore` y en
  `processBlock`, con cambios de tipo continuos.

**Política para peticiones durante un crossfade** (acotada y determinista; la
última petición gana):

| Petición durante A → B | Acción |
|---|---|
| B (el tipo entrante) | Nada (se descarta cualquier pendiente) |
| A (el tipo saliente) | **Reversión en sitio:** se intercambian los papeles y `w → 1 − w`. La salida es idéntica en el instante del cambio y la ruta A original nunca se reinicia |
| Cualquier otro tipo C | Se guarda como **único pendiente** (sobrescribe al anterior) y se inicia cuando termina A → B |

El último tipo pedido se alcanza siempre en ≤ 2·N muestras (40 ms). Un tipo
pendiente que se sobrescribe no llega a activarse nunca (probado).

**Medidas de la transición** (48 kHz, f0 = 1 kHz, Q = 2, +9 dB; 30 pares
ordenados × tonos de 200 Hz y 3 kHz, cambiando en un pico del tono):

- 38 de 60 cambios abruptos (intercambiar coeficientes conservando el estado)
  producen un click fuerte, equivalente a un escalón de −40 dBFS o más. En
  todos ellos el crossfade reduce la discontinuidad (métrica: máximo de la
  4.ª diferencia) **26 dB o más** (peor caso: LowShelf → LowCut a 200 Hz).
  Criterio: ≥ 20 dB.
- En los 60 casos, el residuo del crossfade sobre el nivel estacionario de
  los propios filtros es ≤ 1,5e-3, equivalente a un escalón de −65,8 dBFS.
  Criterio: ≤ un escalón de −60 dBFS. El residuo procede de que la ruta
  nueva arranca con estado cero: un breve transitorio a su resonancia,
  escalado por la `w` pequeña del principio.
- **Cancelación de fase (medida, no evitable con la ley):** durante el fade, la
  salida en una frecuencia f es `((1−w)H_A(f) + w H_B(f))·entrada`. Si las dos
  respuestas están en contrafase, la suma cae por debajo de ambas. En 90
  casos de par y tono, la envolvente medida coincide con esa teoría (error
  inferior al 2 % de la ruta mayor). La caída más profunda medida es
  **−25,9 dB por debajo de la ruta más débil**, en HighCut ↔ LowCut en f0, donde
  la teoría predice una cancelación total en `w = 0,5`: la medida promedia
  sobre ventanas de un periodo. Dura una fracción de los 20 ms.

### Semántica de bypass y transparencia

| Tipo | Ganancia 0 dB | Bypass |
|---|---|---|
| Bell, Low Shelf, High Shelf | Neutro: identidad exacta, la banda pasa a idle (seco bit a bit, sin CPU) | Rampa de ganancia a 0 dB en 20 ms y después idle. Es el comportamiento de E0/E1a sin cambios |
| Low Cut, High Cut, Notch | **Siguen filtrando**, porque la ganancia no les afecta | Crossfade lineal de 20 ms hacia la ruta seca; después idle y seco bit a bit. Al quitar el bypass, crossfade de vuelta |

- La ganancia se conserva siempre en el estado. Una banda en Cut o Notch
  guarda su ganancia, y al volver a Bell o a un shelf la recupera (probado).
- Cambiar de tipo con la banda en bypass la deja transparente. Por ejemplo,
  Bell en bypass → Low Cut en bypass es un fade entre dos identidades, que es
  una identidad exacta.
- No hay limitadores, saturación ni normalización. No hay bypass global ni
  Output Gain (fuera del alcance de E1b).

### Ganancia neutra exacta en el adaptador (desde E1a)

Por la rejilla de 0,01 dB de JUCE en float con FMA en arm64, "0.00 dB" llega
como −5,364e-7 dB. El adaptador trata |g| < 0,005 dB como 0 exacto, para que
Bell y shelves a 0 dB queden en idle. No afecta a Cut ni a Notch, que ignoran
la ganancia. Detalle y cuantificación frente a E0 en
[COMPATIBILITY_E0_E1A.md](COMPATIBILITY_E0_E1A.md).

### Bit-exactitud entre arquitecturas

Las garantías bit a bit valen **dentro de una misma arquitectura**. Entre
arm64 y x86_64 hay diferencias del orden de un ulp, porque clang contrae
`a*b + c` en FMA en arm64. Todas las pruebas se ejecutan en las dos
arquitecturas.

## Motor dinámico (E2a, E2b, E2c.1)

**E2c.1** publica la dinámica: 40 parámetros (`b{n}_dyn_on`, `_threshold`,
`_range`, `_attack`, `_release`), version hint 4, índices 40–79 y estado v4.
Detalles en [DYNAMICS_E2C1.md](DYNAMICS_E2C1.md) y
[PARAMETER_SCHEMA.md](PARAMETER_SCHEMA.md).



**E2b** integra ocho `DynamicSection` en `EqCore`: una por banda, cada una con
su propio detector y sus ajustes, y activa solo en Bells sin bypass. Detalles
en [DYNAMICS_E2B.md](DYNAMICS_E2B.md).

- **Señal original común:** mientras hay dinámica activa, `EqCore` procesa
  en segmentos de ≤ 16 muestras alineados con la rejilla de ticks. Primero
  ticks, después los detectores sobre el segmento todavía intacto y después
  la cascada en el sitio.
- **Sin dinámica activa:** la ruta es la de E2a.
- **Cambio en `EqBand`:** la modulación pasa a afectar solo a la ruta Bell.

### Prototipo E2a

Diseño completo, medidas y plan para E2b en [DYNAMICS_E2A.md](DYNAMICS_E2A.md).

- **Qué hay:** `DynamicBellBand` = `EqBand` (Bell) + `DynamicDetector` (dos paso
  banda RBJ en f0/Q y RMS con ventana de 0,5/f0, en dBFS de seno) + ley con
  rodilla de 6 dB, pendiente 1 y tope en Range + balística de ataque y release.
- **Sin parámetros públicos:** el plugin no usa el motor todavía.
- **Cambio en `EqBand`:** `setGainModulationDb()` desplaza la ganancia de las
  topologías con ganancia. Cuando la modulación cambia, los coeficientes se
  interpolan linealmente muestra a muestra dentro del tick.
- **Ruta estática intacta:** con modulación 0 (siempre en el plugin) no se
  interpola nada y `EqBand` es el de E1b.1 bit a bit. Lo verifica el arnés de
  compatibilidad estricto frente a `5e889ec`.
- **Refactor sin cambios de comportamiento:** `LinearRamp` sale de `EqBand` a
  su propio archivo para compartirla. Es la misma aritmética.

## Estado y migración

| Versión | Fase | Contenido |
|---|---|---|
| 1 | E0 | `b1_*` (4 parámetros) |
| 2 | E1a | `b1_*` … `b8_*` (32) |
| 3 | E1b | + `b1_type` … `b8_type` (40) |

`params::migrateState()` añade cualquier parámetro ausente con su **valor
por defecto explícito** y escribe la versión 3. De v1 a v3 y de v2 a v3 solo
se añaden parámetros, y el tipo por defecto es Bell, que es exactamente lo que
sonaba en E0 y E1a. Sin migración, JUCE 9.0.3 vuelve a guardar los ausentes
con o sin `value` según la historia de la instancia, y el estado deja de ser
determinista (comprobado por mutación en E1a). Los `PARAM` desconocidos se
conservan sin crear parámetros. Un estado corrupto o con otra raíz se ignora.
Un valor de tipo fuera de rango en un estado editado (9, −1, 2,6) lo ajusta
el `AudioParameterChoice` de JUCE a un índice válido (probado).

## Rendimiento

`labq8_bench` (Release, sin JUCE): 8 bandas en estéreo, ruido con semillas
fijas, 10 s por pasada, mediana de 7 tras 1 de calentamiento, un solo hilo.
Apple M5, macOS 26.5. Los binarios de E1a y E1b se ejecutaron intercalados
(E1a, E1b, E1a, E1b) en la misma sesión. Rango de las dos ejecuciones, en %
de un núcleo:

| Escenario | 44,1 kHz | 48 kHz | 96 kHz | Frente a E1a |
|---|---|---|---|---|
| idle (8 bandas a 0 dB) | 0,014–0,022 | 0,015–0,017 | 0,030–0,034 | +20 % aprox. (0,005 puntos más) |
| static (8 Bell activas) | 0,12–0,16 | 0,13–0,18 | 0,26–0,37 | 0,95–1,01× |
| automating (8 Bell en rampa) | 0,18–0,22 | 0,19–0,25 | 0,39–0,45 | 0,90–1,12× |
| mixed (8 bandas, los 6 tipos) | 0,12–0,16 | 0,13–0,19 | 0,27–0,36 | (nuevo) ≈ static |
| bypass (8 bandas mixtas en bypass) | 0,012–0,016 | 0,013–0,016 | 0,024–0,030 | (nuevo) ≈ idle |
| typechange (cada banda cambia de tipo cada 250 ms) | 0,14–0,17 | 0,15–0,21 | 0,31–0,41 | (nuevo) +5–15 % sobre static |
| **xfade-all (8 crossfades permanentes)** | 0,29–0,36 | 0,35–0,37 | **0,70–0,75** | (nuevo) ≈ 2,1× static |

- **Coste de un crossfade:** durante el fade la banda procesa dos biquads más
  la mezcla, por muestra, sin la ruta por bloques. Cuesta ≈ 2,1× una banda
  estática, ≈ 75 ns por frame estéreo con las 8 bandas en fade a 96 kHz,
  frente a ≈ 34 ns en estático. Con cambios de tipo realistas (typechange) el
  sobrecoste medio es del 5–15 %.
- **Idle +20 %:** comprobar `fading` y la ruta activa añade ≈ 0,4 ns por banda
  y bloque. En valor absoluto es despreciable (≤ 0,034 % de un núcleo).
- **Bell sin regresión:** static y automating quedan dentro de la variación
  entre ejecuciones (±10 %).
- **x86_64 bajo Rosetta (solo indicativo):** peor caso de 0,92 % de un núcleo a
  96 kHz.
- **Criterios de las pruebas `perf:`** (solo en Release): < 5 % de un núcleo,
  tanto para E1a (automating) como para E1b (xfade-all, 96 kHz, bloques de
  64). Con el peor caso medido en 0,72 % hay unas 7 veces de margen: detectan
  regresiones graves, no variaciones finas. No he fijado un objetivo más
  estricto porque la variación de la máquina compartida (±20 %) lo haría poco fiable.

**E1b.1:** el Q efectivo es una comparación por llamada a `setTargets`, no por
muestra. En una ejecución intercalada (E1b, E1b.1, E1b, E1b.1) a 96 kHz, en % de
un núcleo:

| Escenario | E1b | E1b.1 |
|---|---|---|
| mixed | 0,31–0,40 | 0,31–0,42 |
| bypass | 0,026–0,035 | 0,027–0,038 |
| typechange | 0,34–0,43 | 0,35–0,44 |
| xfade-all | 0,78–0,85 | 0,78–0,80 |

Hay paridad dentro de la variación de la máquina. El mismo binario E1b mide
ahora 0,78–0,85 en xfade-all, frente a 0,70–0,75 en la sesión de E1b: la
variación entre sesiones es mayor que cualquier diferencia entre versiones.

## Compatibilidad macOS (sin cambios desde E1a)

Objetivo 11.0 en arm64 y x86_64 (`minos 11.0` verificado con `vtool` en el
AU y el VST3 de E1b). JUCE 9.0.3 permitiría bajar Intel hasta 10.11; se
mantiene 11.0 por las razones documentadas en E1a (un único objetivo para el
binario universal, no se puede probar en macOS 10.x y el público con Logic
actual no lo necesita).

## Pruebas

107 pruebas en `labq8_tests` (CTest), en Debug, en Release arm64 y en Release
x86_64:

| Prefijo / archivo | Nº | Qué cubre |
|---|---|---|
| `dsp:` (E0) | 11 | `BellBand` congelada; archivo sin cambios |
| `processor:` (E0) | 7 | Una línea cambiada: nº de parámetros de 32 a 40 |
| `eq:` (E1a) | 10 | Sin cambios; ahora sobre `EqBand`, incluida la igualdad bit a bit con 8 `BellBand` encadenadas |
| `processor:` (E1a) | 5 | Una línea cambiada: nº de parámetros de 32 a 40 |
| `state:` (E1a) | 4 | Una línea cambiada: el estado reguardado tiene 40 `PARAM` |
| `rt:`, `perf:` (E1a) | 3 | Sin cambios |
| `filter:` (E1b) | 5 | A, B, C, D: los seis tipos frente al prototipo analógico; significado de f0, ganancia y Q; límites y Nyquist; senoides medidas; tabla de sobreoscilación |
| `band:`, `crossfade:`, `type changes:`, `multi:` (E1b) | 15 | E–M: Bell = BellBand; transparencia de Bell/shelves; Cut y Notch activos a 0 dB; bypass por tipo; estructura y convexidad del crossfade; duración; política; clicks frente a cambio abrupto; cancelación de fase; cambios rápidos; bloques; NaN; estéreo; cascada mixta; automatización por tipo |
| `e1b processor:`, `state:`, `rt:`, `perf:` (E1b) | 8 | Layout y hints; procesador = núcleo con tipos; automatización de tipo independiente del bloque; ganancia conservada; estados E1a reales; sonido idéntico a E1a; round-trip v3 y tipos inválidos; sin asignaciones con cambios de tipo; CPU del peor caso |
| `e2c ...:` (E2c.1, [ProcessorDynamicTests.cpp](../tests/ProcessorDynamicTests.cpp)) | 9 | 80 parámetros: orden, IDs, hints y valores por defecto; round-trip v4 (y deriva de guardar/cargar); migración de todos los estados anteriores con dinámica apagada y sonido estático; ocho bandas automatizadas por separado = núcleo; independencia del bloque del host; enable durante la reproducción; solo Bell; transparencia; sin asignaciones ni NaN; CPU del procesador |
| `dyn8:` (E2b, [EqCoreDynamicTests.cpp](../tests/EqCoreDynamicTests.cpp)) | 11 | Ocho Bells dinámicas, detectores independientes sobre la entrada común, banda dinámica entre estáticas, automatización y bloques, attack/release por banda, estéreo enlazado y panorámica, cambios de tipo, enable/bypass, extremos, varias bandas modulando, CPU |
| `dyn ...:` (E2a, [DynamicTests.cpp](../tests/DynamicTests.cpp)) | 12 | Detector (precisión en f0, rechazo, ruido), ley y rango, ataque/release, transitorios, reposo y dinámica apagada = banda estática bit a bit, señales > 0 dBFS y NaN/Inf, automatización rápida, bloques, clicks de modulación y estabilidad variable en el tiempo, enlace estéreo, CPU |
| `q limit:`, `state:` (E1b.1, [EffectiveQTests.cpp](../tests/EffectiveQTests.cpp)) | 7 | Función de Q efectivo por tipo; sobreoscilación y resonancia con Q guardado 18 en extremos de f0 y fs; headroom (L1 y entrada de peor caso); automatización por encima del límite inaudible y cruce del límite = rampa normal (bit a bit); Q efectivo por ruta en cambios de tipo; procesador y estado conservan el Q guardado; estados E1b reales |

En E1b.1 cambian dos pruebas E1b: "measured sine response" y "automation
ramps every topology like Bell" usaban Q 2 en los shelves y ahora esperan su Q
efectivo (`effectiveQ`). No cambia nada más de esas pruebas.

**Mutaciones verificadas en E1b** (se restauró el archivo después y se
comprobó con `cmp`):

| Mutación | Prueba que falla |
|---|---|
| Fade de 1 muestra (cambio abrupto) | clicks: 87/99 comprobaciones |
| Ley de igual potencia | estructura/convexidad: 120/240 |
| Cut y Notch tratados como neutros a 0 dB | "Cut and Notch stay active": 36/54 |
| Sin reversión (siempre pendiente) | política: 5/13 |

**Mutaciones verificadas en E1b.1** (Debug, archivos restaurados y comprobados
con `cmp`):

| Mutación | Pruebas que fallan |
|---|---|
| Sin límite (Q guardado directo) | 8 pruebas (6 de E1b.1 y las 2 de E1b ajustadas) |
| Límite aplicado al Q suavizado y no al objetivo de la rampa | 5, entre ellas "crossing it ramps like any Q change": 24/40 |
| Sin la tolerancia de 1 ppm | función de Q efectivo y estados E1b reales |
| Límite de cortes 10 (la propuesta de E1b) | 4: función, resonancia, headroom, estados E1b |

Siguen vigentes las mutaciones de E0/E1a (suavizado, migración, 0 dB).

### Tolerancias nuevas

| Tolerancia | Uso | Justificación |
|---|---|---|
| 1e-4 dB, o 1e-8 lineal por debajo de −60 dB | Diseño frente al prototipo analógico | Cálculo en double; en bandas de rechazo y en el centro del Notch el dB amplificaría redondeos inocuos |
| 1e-6 dB / 1e-9 | Puntos característicos (DC, Nyquist, f0, bordes del Notch a 1e-3 dB) | Identidades exactas de la bilineal |
| 0,01 dB | Senoides con E/S float32 | Como en E0 |
| ≤ −80 dB | Profundidad medida del Notch en f0 con E/S float32 | El suelo lo marcan el redondeo float y el transitorio residual |
| 4 ulp del mayor | Crossfade frente a la mezcla de dos rutas de referencia | Redondeo de la conversión a float de las tres señales |
| ≥ 20 dB y ≤ escalón de −60 dBFS | Clicks | Ver la sección de crossfades |
| 2 % de la ruta mayor | Envolvente durante el fade frente a la teoría | La mezcla cambia dentro de la ventana de un periodo |

## Riesgos técnicos y decisiones abiertas

1. **Transitorio de arranque de la ruta nueva:** empieza con estado cero. Queda
   ≤ −60 dBFS sobre el nivel propio en las pruebas, pero con Q alto en graves
   el transitorio dura más que el fade. Mejora posible: precalentar la ruta
   nueva con un historial corto de entrada, de tamaño fijo.
2. **Cancelación de fase en fades entre topologías en contrafase** (Cut ↔ Cut,
   Shelf ↔ Cut): caídas transitorias de hasta −26 dB en algunas frecuencias.
   Es inherente a cualquier crossfade entre filtros distintos.
3. **Q alto en shelves y cortes:** resuelto en E1b.1 con el Q efectivo (shelves
   1, cortes 2). Las sesiones E1b con shelves por encima de Q 1 o cortes por
   encima de Q 2 suenan distinto (ver [COMPATIBILITY_E1B1.md](COMPATIBILITY_E1B1.md)).
   El límite no se ve hasta que exista la UI propia.
4. **Sin `auval` ni validación en DAW** (la instalación sigue prohibida).
5. **Bypass del host sin rampa** (`processBlockBypassed` de JUCE). El bypass
   global y el Output Gain quedan fuera de E1b.
6. **Bit-exactitud solo dentro de cada arquitectura** (FMA).
7. **`getTailLengthSeconds() = 0`.** Con Q alto, la cola IIR real dura segundos.
8. **Variabilidad de las medidas de CPU** (±20 %) en máquina compartida.
9. **El orden de bandas no sigue la frecuencia** (b1 = 1 kHz); decisión ya
   tomada en E1a de no renumerar.
