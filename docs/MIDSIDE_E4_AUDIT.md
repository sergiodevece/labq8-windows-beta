# LABQ8 E4 — Auditoría y propuesta de arquitectura STEREO / MID / SIDE

**Estado:** propuesta para aprobación. **No implementada.** Sin cambios de DSP,
parámetros, estado ni interfaz.
**Base:** `152d46c9f55c213c2306c58f5ad6bf97ce075534` (UI-2c, SMOKE-6 aprobado).
**Alcance:** solo M/S con bancos independientes. Fuera de alcance: INTENTION,
PHASE BEHAVIOR, POLARITY, colocación por banda (`b{n}_place`), solo/escucha
MID/SIDE y cualquier otra función.

---

## 0. Punto de partida (lo que hay hoy)

| Pieza | Situación en 152d46c |
|---|---|
| `dsp::EqCore` | 8 `EqBand` + 8 `DynamicSection`, en sitio, hasta 2 canales con **los mismos** coeficientes en L y R. Detectores con enlace `maxLinked` sobre L/R. Ruta estática (banda a banda) o dinámica (segmentos de 16 muestras sobre una rejilla absoluta). Sin memoria dinámica, sin latencia |
| `LabQ8AudioProcessor` | Un único `EqCore eq`; lee los 80 parámetros una vez por bloque del host; procesa en sub-bloques de 256 muestras (A4) para la telemetría |
| Parámetros | 80 públicos: índices 0–31 (E0/E1a), 32–39 tipos (E1b), 40–79 dinámica (E2c.1). Ningún parámetro de modo. Reservados sin usar en PARAMETER_SCHEMA: `proc_mode` (Stereo, Mid/Side) y `b{n}_place` |
| Estado | v4, 80 `PARAM`, migración genérica: todo parámetro ausente recibe su valor por defecto **explícito** |
| Interfaz | Bloque "STEREO MODE" (STEREO / M/S / L/R) inactivo (`InactiveArea` {458, 80, 310, 58}); el editor usa `params::bandIDs` / `params::dynamicIDs` en 11 puntos |
| Medidores / FFT / DYN | IN/OUT pico de muestra L/R; `SpectrumTap` PRE/POST L/R (media de potencia de canales); `DynamicsTelemetry` de 8 bandas |
| Versión del plugin | `0.0.1` (CMake) |

Consecuencia clave: **`EqCore` ya es reutilizable tal cual** para un banco
MID y otro SIDE (instancias de 1 canal). No hace falta tocar `EqBand`,
`DynamicSection` ni `DynamicDetector`.

---

## 1. Esquema técnico de bancos y recorrido de señal

### 1.1 Bancos

Propuesta recomendada: **tres bancos independientes**, cada uno un `EqCore`
completo (8 bandas, 6 topologías, dinámica):

| Banco | Instancia | Canales | Parámetros |
|---|---|---|---|
| STEREO | `stereoCore` (el `eq` actual, sin cambios) | 1–2 (L/R) | los 80 existentes, mismo ID y significado |
| MID | `midCore` | 1 (M) | 80 nuevos `mid_*` |
| SIDE | `sideCore` | 1 (S) | 80 nuevos `side_*` |

Los tres reciben sus parámetros en cada bloque; **solo el banco o bancos del
modo activo procesan audio**. Un banco inactivo conserva sus ajustes, se guarda
y se puede automatizar, pero no suena.

### 1.2 Recorrido de señal

```
                     ┌──────────── modo STEREO ────────────┐
 IN L/R ─┬─ medidor IN, tap PRE ─► stereoCore (L,R en sitio) ─► mezcla ─► medidor OUT, tap POST ─► OUT L/R
         │                                                        ▲
         │           ┌──────────── modo M/S ───────────────┐      │
         └─► codif. ─┤ M = (L+R)/2 ─► midCore  ─► M' ─┐    ├──────┘
                     │ S = (L−R)/2 ─► sideCore ─► S' ─┴► decodif. L' = M'+S', R' = M'−S'
                     └─────────────────────────────────────┘
```

Nuevo componente DSP (sin JUCE, como el resto de `src/dsp`), propuesto como
`dsp::StereoModeEngine` (`src/dsp/StereoModeEngine.{h,cpp}`), dueño de los
tres `EqCore`, del codificador M/S, del cambio de modo y de dos búferes de
trabajo de 256 muestras (M y S) asignados en `prepare()`. El procesador sigue
sin DSP propio: le pasa ajustes y sub-bloques, como hoy.

### 1.3 Codificación y decodificación M/S, niveles

- **Convención propuesta:** `M = (L+R)/2`, `S = (L−R)/2`; `L = M+S`, `R = M−S`.
  - Con señal mono (L = R) **M = L exactamente** y S = 0: el banco MID trabaja
    al mismo nivel que STEREO, así que **los umbrales dinámicos de MID valen lo
    mismo que en STEREO** para material centrado. Es la razón principal frente
    a la alternativa ortonormal (1/√2), que bajaría M 3 dB y obligaría a
    recalibrar umbrales al cambiar de modo.
  - Ganancia del recorrido completo = 1 (no hay compensación de nivel).
- **Aritmética:** codificación y decodificación en `double`; `(L+R)` y `(L−R)`
  de dos `float` son exactos en `double` y ×0,5 es exacto, de modo que M y S
  sólo se redondean una vez al pasar a `float` (el `EqCore` trabaja en `float`).
- **Transparencia:** el ciclo codificar→decodificar sin proceso no es
  bit-exacto en general (error ≤ 1 ulp, ≈ −150 dBFS). Para que M/S con bancos
  planos sea **bit-exacto** (pruebas de nulo de los usuarios), ruta rápida:
  si `midCore` y `sideCore` están ociosos (`isIdle()`), el motor no codifica y
  deja el búfer intacto. Al salir/entrar de la ruta rápida la discontinuidad
  es ≤ 1 ulp.
- **Casos exactos** (se probarán bit a bit, sin ruta rápida):
  - Mono (L = R): `L−R = 0` exacto → S = 0 → la salida de SIDE es 0 exacto →
    `L' = R'` bit a bit. **Una entrada mono sigue siendo mono sea cual sea el
    banco SIDE.**
  - Contrafase (R = −L): M = 0 exacto, S = L exacto → MID no hace nada y
    `L' = −R'` bit a bit.

### 1.4 Detectores dinámicos en MID y SIDE

- Cada banco conserva la garantía E2b: sus 8 detectores leen **la entrada de
  su banco** (M o S antes de sus bandas), nunca la salida de bandas previas.
- Con 1 canal el enlace (`maxLinked`) es irrelevante: el nivel es el del
  propio M o S. Se probará que `EqCore` de 1 canal con `maxLinked` es idéntico
  a `mono`.
- **Escala:** umbrales en dBFS de seno (como hoy) sobre M o S. Material
  centrado: MID ≈ STEREO. SIDE suele estar 10–20 dB por debajo: un umbral
  copiado de STEREO a SIDE normalmente no actuará. Se documentará; no se
  ajusta nada automáticamente.
- Diferencia con STEREO: en STEREO el detector usa el máximo de L y R; en M/S
  detecta sobre M y S. Por eso STEREO y M/S con los mismos ajustes **no son
  idénticos en dinámica** (sí en estática, ver §2.5).

### 1.5 Cambios de modo sin clics

- **Arranque** (`prepareToPlay`, restaurar estado): el motor arranca
  directamente en el modo del parámetro, ajustado a sus valores (sin fundido).
- **Durante la reproducción:**
  1. *Precalentamiento* de 20 ms: el banco entrante procesa una copia de la
     entrada (M/S o L/R) con estado inicial cero y rampas ajustadas a sus
     valores, pero la salida sigue siendo la del modo saliente. Absorbe el
     transitorio de arranque de los IIR y estabiliza los detectores (ventana
     ≤ 25 ms).
  2. *Fundido* lineal de 20 ms, en `double`, `out = old + w·(new − old)`, la
     misma ley convexa que el fundido de tipos de `EqBand` (nunca supera el
     mayor de los dos caminos).
  3. El banco saliente deja de procesarse. Al volver a entrar se reinicia
     (`reset()` ajusta las rampas a los valores pedidos con estado cero).
  - Petición contraria durante el fundido: se invierte en el sitio
    (`w → 1 − w`), sin salto. Durante el precalentamiento: se cancela.
  - Longitud total del cambio: 40 ms. Las reducciones dinámicas del banco
    entrante empiezan en 0 y entran con su ataque (el precalentamiento no
    cubre ataques largos; aceptado y documentado).
- **Nivel:** si los bancos MID y SIDE son copia del STEREO, el cambio es
  estáticamente transparente (§2.5), así que no hay salto de nivel.
  Si son distintos, el salto es el musicalmente pedido, en 20 ms.

### 1.6 Mono, silencio, bypass, contrafase

| Situación | Comportamiento propuesto |
|---|---|
| Disposición mono (1 canal) en M/S | **Decisión D5.** Recomendado: coherente con la matemática, M = x, S ≡ 0 → actúa el banco MID; SIDE se muestra atenuado con aviso "SIDE: no signal on mono tracks". Alternativa: forzar STEREO en mono |
| Silencio | Bancos ociosos o ruta estática sobre ceros → ceros; detectores en su suelo; sin denormales (ya protegido: `ScopedNoDenormals`, limpieza de estado) |
| Bypass del host | `processBlockBypassed` sin cambios (paso directo, medido) |
| Bypass de banda | Por banco (`mid_b3_bypass` sólo afecta a MID) |
| Contrafase | §1.3: MID recibe silencio exacto; todo pasa por SIDE |
| Recortes | Un realce de SIDE ensancha y puede subir los picos L/R; sin ganancia automática (fuera de alcance) |

### 1.7 Coste de CPU (estimación; se mide en E4b)

| Modo | Biquads por muestra | Diseños de coeficientes | Extra |
|---|---|---|---|
| STEREO | 8 bandas × 2 canales | 8 por tick (compartidos por L y R) | **ninguno** (misma ruta que hoy) |
| M/S | 2 × 8 bandas × 1 canal = igual | hasta 16 por tick (cada banco diseña los suyos) | codificar/decodificar (~6 operaciones/muestra), 2 copias de 256 muestras |
| Durante el cambio | ambos modos | ambos | 40 ms |

Detectores: igual número de filtros (1 canal × 2 bancos frente a 2 canales ×
1 banco). Estimación: M/S estático ≈ 1,1–1,3× STEREO; M/S con 16 bandas
dinámicas ≈ 1,5–2× (coste de diseño por tick duplicado). Referencia E2b:
8 bandas dinámicas a 96 kHz ≈ 0,9–1,0 % de un núcleo. **Presupuesto propuesto:**
el mismo 5 % a 96 kHz de la prueba `dyn8 perf`, con 16 bandas dinámicas en M/S,
en arm64 y x86_64.

---

## 2. Parámetros y migración de sesiones

### 2.1 ¿Dos o tres bancos? (D1)

| | **A. Tres bancos** (recomendado) | B. Dos bancos (STEREO = MID) |
|---|---|---|
| Parámetros nuevos | 1 + 160 = **161** (total 241) | 1 + 80 = 81 (total 161) |
| Los 80 existentes | Significan exactamente lo mismo (banco STEREO) | Pasan a significar "STEREO **o** MID" según el modo: **cambio de significado** de IDs existentes |
| Cambiar de modo | No toca ningún ajuste; A/B STEREO ↔ M/S inmediato | Al pasar a M/S, la curva estéreo se aplica sólo a M y SIDE queda plano: cambio audible inesperado |
| Automatización | Sin ambigüedad: cada ID, un banco | "Band 1 Gain" automatiza cosas distintas según el modo |
| Coste | Lista de automatización larga; ~3× XML | Menor |

B incumple en la práctica "conservar IDs **y significado**". Se recomienda A.

### 2.2 Nuevos parámetros públicos (D2)

- `proc_mode` — choice {`Stereo`, `Mid/Side`}, defecto `Stereo`. ID ya
  reservado en PARAMETER_SCHEMA con esos mismos valores. Automatizable (con el
  cambio sin clics de §1.5). Nombre de host: "Stereo Mode".
- Bancos: mismo conjunto, rangos, rejillas, defectos y textos que el banco
  STEREO. IDs deletreados en `ParameterIDs.h` (sin construir cadenas, como hoy):

  | Por banda n = 1..8 | MID | SIDE |
  |---|---|---|
  | frecuencia, ganancia, Q, bypass, tipo | `mid_b{n}_freq`, `_gain`, `_q`, `_bypass`, `_type` | `side_b{n}_freq`, … |
  | dinámica | `mid_b{n}_dyn_on`, `_threshold`, `_range`, `_attack`, `_release` | `side_b{n}_dyn_on`, … |

  Nombres de host: "Mid Band 1 Gain", "Side Band 1 Gain". Los 80 existentes
  conservan su nombre ("Band 1 Gain") para no alterar lo que el usuario ya ve.
- Frecuencias por defecto de MID y SIDE: las mismas que STEREO (40, 100, 250,
  1k, …), todas a 0 dB → transparentes.
- `versionHintE4 = 5` para los 161.
- **Orden de host (sólo añadir):** 0–79 sin cambios; 80 = `proc_mode`;
  81–160 MID (b1..b8, cada uno freq, gain, q, bypass, type, dyn_on, threshold,
  range, attack, release); 161–240 SIDE (igual). Protege a hosts o
  envoltorios que asocian por índice.
- Grupos de parámetros (`AudioProcessorParameterGroup`) "Mid" y "Side" sólo
  para los nuevos, para que Logic los muestre en submenús; los 80 existentes
  sin grupo. Se verifica en E4a con auval y Logic; si alterase algo de lo
  existente, se retira.
- `b{n}_place` sigue reservado y **no** se usa (el modelo de bancos lo
  sustituye en E4). No se reutiliza ningún ID.
- **No** son parámetros: el banco que se edita (MID/SIDE) ni la selección de
  banda; son estado de interfaz (§3).

### 2.3 Migración v4 → v5

- `currentStateVersion = 5`. La migración genérica actual ya añade cualquier
  parámetro ausente con su defecto explícito: un estado v1–v4 llega a v5 con
  241 `PARAM`, `proc_mode = Stereo` y MID/SIDE planos. **Suena exactamente
  como antes** (modo STEREO, banco STEREO intacto).
- Re-guardado determinista byte a byte en cualquier instancia (misma garantía
  que v4; probado con fixtures auténticos).
- **Fixtures nuevos:** estados v4 auténticos generados con el build de
  152d46c (por defecto y personalizado con dinámica), como se hizo en cada fase.
- **Bajar de versión:** una sesión v5 abierta en una beta anterior (p. ej. la
  VST3 de Miguel) ignora los `PARAM` desconocidos: carga el banco STEREO y
  suena en STEREO; se pierde el M/S, sin fallo. Se comprobará con el arnés
  de compatibilidad y se comunicará.

### 2.4 ¿Qué pasa con los ajustes al cambiar de modo? (D6)

- **Cambiar de modo nunca escribe parámetros.** Escribirlos desde el plugin
  generaría automatización no pedida y haría el resultado dependiente del
  orden de reproducción de la automatización del modo.
- Comandos explícitos (gestos de host normales), en el menú del icono de
  ajustes, sección "M/S", sin elementos nuevos en el chasis:
  - **Copy STEREO → MID + SIDE** (propuesto)
  - **Copy MID → SIDE / Copy SIDE → MID** (propuesto)
  - **Reset MID / Reset SIDE** (opcional)

  Si el host está en escritura, la copia queda grabada como automatización
  (documentado).

### 2.5 Propiedad que justifica la copia

Con el **mismo** ajuste estático en MID y SIDE: `L' = H·M + H·S = H·L`,
`R' = H·R` (filtros lineales e invariantes, codificación lineal). Copiar
STEREO → MID + SIDE da **la misma respuesta estática** que STEREO (diferencia
de redondeo ≈ ulp). Sólo difiere la dinámica (detección sobre M y S frente a
máximo L/R). Se probará; hace que el paso a M/S tras copiar sea inaudible
en estática.

### 2.6 ¿Mantener un banco STEREO además de MID y SIDE?

Sí (opción A): es lo que permite conservar el significado de los 80 IDs, la
recuperación exacta de sesiones y el A/B de modos sin perder ajustes.

---

## 3. Controles y comportamiento de la interfaz

### 3.1 Bloque STEREO MODE (medido en el PNG maestro, ±1 px; medición final en E4c con el procedimiento de UI_IMPLEMENTATION)

| Elemento | Maestro | Propuesta |
|---|---|---|
| Carcasa | x 460–766, y 81–137 | sin cambios |
| STEREO | x ≈ 469–556, y ≈ 91–127 (encendido verde) | botón real de `proc_mode = Stereo` |
| M/S | x ≈ 565–655 | botón real de `proc_mode = Mid/Side` |
| L/R | x ≈ 668–754 (86 × 36) | **sustituido** por dos botones contiguos: **MID** {668, 91, 42, 36} y **SIDE** {712, 91, 42, 36}, separación 2 px |

- Estética: caras de botón recortadas del propio maestro (STEREO encendido,
  M/S apagado, sin texto) y ajustadas en 9 tramos, como `viewtab_light`.
  Texto con la misma fuente que los botones del maestro, reducido lo
  necesario para que "SIDE" quepa en 42 px.
- Fondo derivado: el generador borra sólo la tinta y la cara de L/R. **El PNG
  maestro no se toca.** Prueba de diferencia de píxeles: el `background.png`
  nuevo sólo cambia dentro del hueco L/R.
- Se retira el `InactiveArea` "Stereo Mode" (POLARITY sigue inactivo).
- **Riesgo de legibilidad:** al zoom del 40 % cada botón mide ≈ 17 × 14 px.
  Si las capturas de E4c no son legibles, alternativa (D7): un solo botón
  conmutador en el hueco L/R que muestra "MID" o "SIDE".

### 3.2 Estados y realimentación

| Modo | STEREO | M/S | MID / SIDE | Banco mostrado |
|---|---|---|---|---|
| STEREO | encendido | apagado | atenuados al 35 % (como el Dynamic EQ deshabilitado), sin clic, aviso "MID / SIDE: M/S mode only" | STEREO |
| M/S | apagado | encendido | el banco editado encendido, el otro apagado | MID o SIDE |

- La selección de banco **no** activa ni anula ningún banco (spec §1).
- Identificación inequívoca: en M/S la cabecera del panel pasa de
  "● BAND 4" a "● MID · BAND 4" / "● SIDE · BAND 4" (misma fuente y color;
  hay sitio hasta el selector de tipo en x 444).
- Los colores de banda no cambian (son los de los nodos y botones 1–8); el
  banco se distingue por botón y texto, no recoloreando.
- Banco editado y banda seleccionada: estado de interfaz en el procesador
  (como `uiSelectedBand`), en memoria por instancia; al entrar en M/S, MID.
  La banda seleccionada se comparte entre bancos. (D10: guardarlo en el
  estado como propiedad no-parámetro, si se prefiere.)

### 3.3 Qué sigue al banco editado

Potenciómetros, displays, selector de tipo, interruptor dinámico, indicador de
bypass, botones ON/OFF 1–8, nodos y tiradores de Q, curva y relleno
dinámico: **todo** se vuelve a enlazar a los IDs del banco editado mediante un
único acceso (`params::bank(Bank).band[n]`), sustituyendo los 11 usos directos
de `bandIDs` / `dynamicIDs` en el editor.

### 3.4 Mostrar los dos bancos sin confusión (D8)

En M/S, en EQ y ALL: curva compuesta del **otro** banco como línea fina gris
al 35 %, sin relleno, sin nodos, no editable, con una etiqueta pequeña
"SIDE" / "MID" en su extremo derecho. Sin ella el usuario no ve qué hace el
banco que no edita; con nodos sería ambiguo. Si se prefiere, se omite.

### 3.5 Vistas

| Vista | STEREO | M/S |
|---|---|---|
| EQ | como hoy | curva y nodos del banco editado + fantasma del otro |
| SPECTRUM | como hoy | espectro según la fuente del analizador (§3.6) |
| DYN | reducciones de las 8 bandas | reducciones de las 8 bandas **del banco editado** |
| ALL | como hoy | EQ + espectro + dinámica del banco editado |

### 3.6 Analizador PRE/POST en M/S (D9)

- Las tomas PRE/POST siguen siendo L/R en el hilo de audio (**sin cambios en
  audio**). El editor calcula M = (L+R)/2 o S = (L−R)/2 a partir de ellas
  antes de la FFT; POST M/S derivado de la salida es el M'/S' procesado salvo
  redondeo.
- En M/S, por defecto el analizador **sigue al banco editado**: PRE = M (o S)
  antes del banco, POST = después. Es lo que corresponde a la curva que se ve.
- Opción en el menú del analizador (sólo en M/S): "Signal: Edited bank / Output
  L/R". En STEREO no aparece.
- Rótulo del selector: "Post · M", "Pre · S", "Post · L/R" (formas cortas que
  caben en los 108 px de texto; se mide en E4d).
- Medidores IN/OUT: siempre L/R final, sin cambios.

---

## 4. Riesgos reales identificados

| # | Riesgo | Mitigación |
|---|---|---|
| R1 | **Regresión de STEREO** al reorganizar `processBlock` y el motor | STEREO estable = misma llamada a `stereoCore.process` con los mismos sub-bloques; compat estricto contra 152d46c (nuevo caso `ui2c`) en arm64 y x86_64; prueba de que ningún ajuste MID/SIDE cambia un bit en STEREO |
| R2 | **Caché de AU de Logic**: la lista de parámetros cambia con la versión 0.0.1 | Subir versión (propuesta 0.0.1 → 0.1.0) en E4a; en SMOKE-7, comprobar que Logic revalida |
| R3 | Hosts o envoltorios que asocian por índice (entorno de Miguel) | Orden sólo-añadir; verificación de Miguel con una sesión antigua |
| R4 | 241 parámetros: listas de automatización largas, nombres truncados | Prefijos "Mid" / "Side", grupos; revisar en Logic y en el host de Miguel |
| R5 | Transitorio al cambiar de modo (estado cero del banco entrante) | Precalentamiento 20 ms + fundido 20 ms; prueba de clics |
| R6 | Umbrales no trasladables entre modos (sobre todo SIDE) | Convención M = (L+R)/2; documentación; sin ajuste automático |
| R7 | **Editar el banco equivocado** si queda algún uso directo de `bandIDs` en la interfaz | Acceso único por banco; prueba de editor: en SIDE, cada control y cada nodo escribe sólo IDs `side_*` |
| R8 | Automatizar un banco inactivo no suena (confuso) | Documentado; la curva fantasma ayuda en M/S |
| R9 | Redondeo M/S en pruebas de nulo | Ruta rápida bit-exacta con bancos ociosos; error ≤ 1 ulp probado |
| R10 | Semántica en pistas mono | Decisión D5 explícita y probada |
| R11 | Telemetría: semántica del instante con mayor reducción (A2) con dos bancos | El editor indica qué banco observa; la instantánea lleva etiqueta de banco y el editor descarta las de otro banco |
| R12 | Legibilidad de MID/SIDE a 40 % | Capturas a los seis zooms; alternativa de botón único |
| R13 | Pro Tools/Windows VST3 no se puede probar aquí | Lista de comprobación para Miguel en E4e |
| R14 | Bajar de versión pierde el M/S | Documentado (§2.3) |

---

## 5. Plan de trabajo, archivos y criterios de aceptación

Cada etapa: build limpio AU + VST3 universal, suite completa en verde,
informe y **parada para aprobación** antes de la siguiente.

### E4a — Arquitectura, parámetros y compatibilidad (sin DSP nuevo)

- **Archivos:** `src/parameters/ParameterIDs.h`, `ParameterLayout.cpp`,
  `StateMigration.{h,cpp}`; `src/plugin/PluginProcessor.{h,cpp}` (punteros a
  los 161 nuevos, leídos pero sin efecto en audio); `CMakeLists.txt` (versión);
  `tests/StateMigrationTests.cpp`, `ProcessorTests.cpp`; fixtures v4
  auténticos de 152d46c; `tests/compat/` (+ base `ui2c`);
  `docs/PARAMETER_SCHEMA.md`, `ROADMAP.md`.
- **Aceptación:** 241 parámetros; índices 0–79 idénticos a v4 en ID, nombre,
  rango, defecto y version hint (prueba automática contra la lista v4);
  migración v1–v4 → v5 determinista byte a byte; v5 ida y vuelta byte a byte;
  compat estricto contra todas las bases incluida `ui2c`; auval arm64/x86_64.

### E4b — Motor M/S y pruebas de audio

- **Archivos:** nuevos `src/dsp/MidSide.h`, `src/dsp/StereoModeEngine.{h,cpp}`;
  `PluginProcessor.{h,cpp}`; nuevos `tests/MidSideTests.cpp`,
  `tests/ProcessorMidSideTests.cpp`; `tests/RealtimeTests.cpp`;
  `tests/bench/EqBenchmark.cpp`; `docs/MIDSIDE_E4.md`.
- **Aceptación (pruebas):**
  1. STEREO bit a bit: compat estricto 152d46c; ajustes MID/SIDE aleatorios no
     cambian ningún bit en STEREO.
  2. Transparencia M/S: bancos planos → bit-exacto; codificador sin ruta
     rápida → ≤ 1 ulp en valores aleatorios y límite (0, ±1, denormales,
     ±FLT_MAX/2).
  3. Imagen: mono → L' = R' bit a bit con cualquier SIDE; contrafase → L' = −R'
     con cualquier MID; ruido con correlación conocida: realce MID sube y realce
     SIDE baja la correlación como predice |H|; energía M y S por banda dentro
     de ±0,1 dB de |H|.
  4. Independencia: cambiar cada uno de los 80 parámetros MID no altera
     coeficientes, estado ni salida S (y viceversa); el banco STEREO no altera
     nada en M/S.
  5. Dinámica por banco: tono sólo en M → reducción en MID y 0 en SIDE;
     sólo en S → al revés; seno mono de amplitud A lee 20·log10(A) en MID
     igual que en STEREO; 1 canal `maxLinked` = `mono`.
  6. Equivalencia estática de §2.5 (copia → misma salida que STEREO, ≤ ulp).
  7. Cambio de modo en marcha: sin escalones (cota de derivada como las pruebas
     de clics existentes), 20 + 20 ms exactos, inversión a mitad, cambios
     rápidos repetidos, salida finita; independencia del tamaño de bloque del
     host.
  8. Sin asignaciones ni bloqueos en ningún modo ni transición
     (`AllocationGuard`).
  9. CPU: STEREO sin cambios (±ruido) frente a 152d46c; M/S con 16 bandas
     dinámicas ≤ 5 % de un núcleo a 96 kHz en arm64 y x86_64 (Rosetta);
     44,1/96/192 kHz.

### E4c — Interfaz MID/SIDE y gestión de bancos

- **Archivos:** `src/ui/UiGeometry.h`; `tools/ui/GenerateUiAssets.cpp`
  (borrar L/R, sprites de botón); `assets/ui/*` regenerados; `CMakeLists.txt`
  (datos binarios); `src/ui/LabQ8Editor.{h,cpp}`; `src/ui/UiModel.{h,cpp}`;
  `PluginProcessor.h` (banco editado); `tests/EditorTests.cpp`;
  `tools/ui/UiSnapshot.cpp`; `docs/UI_IMPLEMENTATION.md`.
- **Aceptación:** diferencia de píxeles del fondo limitada al hueco L/R;
  capturas a los seis zooms en STEREO, M/S-MID y M/S-SIDE; botones de modo
  enlazados a `proc_mode` (también desde automatización); en SIDE cada
  control y nodo escribe sólo `side_*`; comandos de copia correctos y con
  gestos; curva fantasma (si se aprueba) no interactiva.

### E4d — Medidores, FFT y visualización dinámica

- **Archivos:** `src/plugin/DynamicsTelemetry.h`, `PluginProcessor.cpp`
  (`offerDynamics` por banco), `src/ui/DynamicsOverlay.{h,cpp}`,
  `SpectrumAnalyzer.{h,cpp}` (combinación L/R, M, S), `SpectrumView.{h,cpp}`,
  `LabQ8Editor.cpp` (menú del analizador); `tests/SpectrumTests.cpp`,
  `DynamicTests.cpp`, `MeterTests.cpp`.
- **Aceptación:** DYN y relleno dinámico del banco editado, sin mezclar
  instantes ni bancos (instantánea etiquetada); FFT de M y S contra
  referencia calculada; STEREO idéntico a UI-2c; medidores sin cambios.

### E4e — Pruebas completas y smoke físico

- Suite completa, compat de todas las bases, benchmarks en los dos
  arquitecturas, auval arm64/x86_64, validación VST3, **SMOKE-7 en Logic**
  (abrir sesiones de SMOKE-6, A/B de modos en reproducción, automatización de
  modo y de un parámetro MID y uno SIDE, guardar y reabrir, pista mono, zooms),
  lista de comprobación para el VST3 de Miguel en Windows (sesión antigua,
  automatización, cambio de modo). Commit sólo tras aprobación.

---

## 6. Decisiones que necesitan aprobación

| | Decisión | Recomendación |
|---|---|---|
| D1 | Número de bancos | Tres (STEREO + MID + SIDE), 161 parámetros nuevos, total 241 |
| D2 | IDs y orden | `proc_mode`; `mid_b{n}_*` / `side_b{n}_*`; hint 5; sólo-añadir (80, 81–160, 161–240) |
| D3 | Normalización | M = (L+R)/2, S = (L−R)/2 |
| D4 | Cambio de modo | Automatizable; 20 ms de precalentamiento + 20 ms de fundido lineal |
| D5 | Pista mono en M/S | Actúa MID (S ≡ 0), SIDE atenuado con aviso |
| D6 | Ajustes al cambiar de modo | Nunca automáticos; comandos de copia en el menú de ajustes |
| D7 | Controles MID/SIDE | Dos botones de 42 px en el hueco L/R; alternativa: un conmutador |
| D8 | Otro banco en pantalla | Curva fantasma fina sin nodos |
| D9 | Analizador en M/S | Sigue al banco editado; opción "Output L/R" |
| D10 | Banco editado | Estado de interfaz en memoria, como la banda seleccionada |
| D11 | Versión | 0.0.1 → 0.1.0 en E4a |
