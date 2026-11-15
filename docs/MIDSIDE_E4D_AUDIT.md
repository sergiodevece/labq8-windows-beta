# LABQ8 E4d — Auditoría: FFT MID/SIDE y visualización dinámica por banco

**Base:** `52cf2b5` (E4c cerrada; SMOKE-8 crítico PASS 8/8). **Estado:**
auditoría técnica, **nada implementado**.

**Objetivo:** que el analizador y la vista dinámica muestren MID y SIDE de
verdad, sin tocar el DSP E4b, los 241 parámetros, el estado v5, la interfaz
aprobada de E4c ni el sonido de STEREO.

Las medidas de esta auditoría se tomaron con programas aislados en el
scratchpad. Salidas en `build/e4d-audit-fft-cost_{arm64,x86_64}.txt`.

---

## 0. Situación actual (52cf2b5)

| Pieza | Hoy |
|---|---|
| `SpectrumTap` PRE / POST | **Muestras temporales L/R.** PRE se escribe antes del motor y POST después, en el mismo `processBlock`. Hay un anillo de 65 536 muestras por canal, y L y R se escriben con el **mismo índice** de muestra. Lectura sin bloqueos (seqlock) desde el editor |
| `SpectrumAnalyzer` (editor) | FFT de 8192, 16 384 o 32 768 muestras con ventana Blackman-Harris. Calibrada en dBFS de seno: un seno de amplitud A marca 20·log10 A. En estéreo promedia la potencia de L y R |
| `DynamicsTelemetry` | 8 bandas de un core por instantánea, con la regla "instante más reductor" (A2), la generación y la marca de bypass del host. El procesador solo ofrece el core **STEREO**, y solo cuando se escucha la ruta STEREO; en otro caso indica "nada que mostrar" |
| Editor en M/S (E4c, D7) | Sin superposición dinámica, con la nota "M/S dynamics display: E4d". El analizador dice "L/R …" |

---

## 1. Arquitectura propuesta

### A. FFT MID / SIDE

**Origen de las muestras:** las tomas PRE y POST que ya existen. **No hace
falta una captura nueva** ni ningún cambio en el hilo de audio.

**Método:** en el editor, sobre las muestras temporales copiadas y **antes de
la FFT**:

    M[n] = (L[n] + R[n]) / 2      S[n] = (L[n] − R[n]) / 2   (en double, como dsp::midside)

y **una sola FFT** del componente que se muestra.

**Por qué este método y no los otros:**

| Alternativa | Valoración |
|---|---|
| Combinar magnitudes FFT de L y R | **Incorrecta:** \|M\| ≠ (\|L\|+\|R\|)/2. Sin la fase, una contrafase daría M ≠ 0 |
| Combinar los espectros complejos (X_M = (X_L+X_R)/2) | Exacta (la FFT es lineal), pero cuesta **dos** FFT |
| **Transformar las muestras temporales y hacer una FFT** | **Exacta y más barata.** Las muestras de L y R de un mismo índice son simultáneas por construcción |
| Una captura específica de M/S en el hilo de audio | Innecesaria; añadiría coste y riesgo al audio |

**Exactitud y calibración:**

- **PRE M/S** es exactamente la entrada de los cores MID y SIDE, con la
  misma codificación y el mismo redondeo.
- **POST M/S** es la salida de MID y SIDE salvo ≤ 1 ulp (≈ −138 dB): la
  salida se decodificó a L/R y aquí se vuelve a codificar. Con la ruta plana
  es idéntica a PRE.
- **Calibración:** igual que hoy en dBFS de seno. Un seno de amplitud A en M
  marca 20·log10 A. Una señal mono (L = R = A) da M = A, el mismo nivel que
  muestra hoy el analizador L/R para esa señal.

**Casos límite:**

| Señal | MID | SIDE |
|---|---|---|
| Mono (L = R) | Igual que L | **Cero exacto: no se dibuja nada** |
| Contrafase (R = −L) | **Cero exacto** | Igual que L |
| Pista mono (1 canal) | El canal | No hay canal: nada |
| Silencio | Nada (por debajo de −120 dB) | Nada |
| Bypass del host | PRE = POST (como hoy) | PRE = POST |

**Sincronía PRE / POST:**

- **Igual que hoy:** las dos tomas se leen por separado y pueden estar
  desfasadas hasta un bloque del host (≈ 10 ms frente a una ventana de
  170 ms). Pasa lo mismo en el "Pre + Post" actual.
- **Opción D3:** alinear PRE con el final de POST (mismo índice de muestra).
  Es un cambio solo del editor.

**STEREO:** exactamente el código actual (media de potencia de L y R), sin
regresiones.

### B. Visualización dinámica

**Origen:** los mismos valores que ya calcula el DSP (`EqBand::getCurrent*` y
`DynamicSection::getGainReductionDb`), pero del **core del banco observado**.

1. **Banco observado.** El editor indica a `DynamicsTelemetry` qué banco
   observa: STEREO, MID o SIDE, el banco mostrado de E4c. Es un
   `std::atomic<int>`, igual que el `setActive` actual.
2. **Qué ofrece el procesador** (`offerDynamics`), después de cada
   sub-bloque:
   - **Observa STEREO:** valores reales solo si se escucha la ruta STEREO sin
     transición. Es lo de hoy.
   - **Observa MID o SIDE:** valores reales de ese core solo si se escucha la
     ruta M/S sin transición.
   - **En cualquier otro caso** (la ruta en reserva, o los 20 ms del
     fundido): "nada que mostrar". **Nunca** se muestra un banco que no se
     escucha.
3. **Etiqueta de banco** en cada instantánea. La superposición descarta las
   instantáneas de otro banco, por ejemplo una ofrecida justo antes de
   cambiar de MID a SIDE. Al cambiar de banco se reinicia como en una nueva
   generación.
4. **Coherencia:** la superposición ya lee la curva estática del banco
   mostrado desde la pantalla (E4c). Con la telemetría del mismo banco, la
   curva estática, la dinámica y los nodos coinciden.
5. **Límite efectivo de ganancia:** se mantiene
   `effectiveReductionDb` (±24 dB con la ganancia estática del propio core).
6. **Casos:**
   - **Bypass del host:** "nada" (como hoy).
   - **Silencio:** la reducción vuelve con su release, y la superposición se
     retira tras 100 ms sin audio (como hoy).
   - **Pista mono en SIDE:** el core SIDE no procesa y su reducción es 0, así
     que no se dibuja nada (correcto).
   - **Cambio de modo:** nada durante el fundido de 20 ms, y después el banco
     nuevo.
7. **Independencia:** cada core tiene su sección dinámica (probado en E4b).
   La telemetría lee uno solo por instantánea, sin mezclarlos.

### C. Interfaz: sin controles nuevos

| Vista | STEREO (sin cambios) | M/S (E4d) |
|---|---|---|
| EQ | Curva y nodos | Banco editado más la curva gris del otro (E4c, sin cambios) |
| SPECTRUM | L/R | **Componente del banco editado** (MID o SIDE), u "Output L/R" si se elige |
| DYN | Dinámica STEREO | **Dinámica del banco editado**, con sus cifras |
| ALL | Todo | Todo lo anterior. La curva gris sigue siendo estática |

- **Identificación del banco:** el botón MID/SIDE encendido y la cabecera
  ("SIDE  BAND 4"), como en E4c.
- **Identificación del dominio:** el texto del selector del analizador. Todos
  los rótulos caben en su caja (108 px, medidos con la fuente real):
  - "MID Post" (58 px);
  - "SIDE Pre + Post" (99 px);
  - "L/R Post" / "L/R Pre+Post" (83 px).
- **Menú del analizador:** en M/S gana una sección "Signal": **Edited bank
  (MID/SIDE)** / **Output (L/R)**. Es el mismo menú de hoy, no un control
  nuevo. En STEREO no aparece.
- **Se retira** la nota "M/S dynamics display: E4d".
- **Sin cambios:** el chasis, el PNG maestro, la geometría y los controles de
  E4c.

### D. Rendimiento y concurrencia

| Lugar | Cambio de coste |
|---|---|
| Hilo de audio | **Ninguno en las tomas.** La telemetría lee un core en vez de otro: 8 bandas, igual que hoy, más una lectura atómica |
| Editor (medido) | M o S cuesta **menos** que L/R, porque hace 1 FFT en vez de 2 |

| Frecuencia, FFT | L/R hoy: PRE + POST a 30 fps | M o S: PRE + POST a 30 fps |
|---|---|---|
| 48 kHz, 8192 | arm64 0,46 % · x86_64 0,47 % | arm64 **0,28 %** · x86_64 **0,33 %** |
| 96 kHz, 16 384 | 0,80 % · 0,97 % | **0,48 %** · **0,67 %** |
| 192 kHz, 32 768 | 1,18 % · 2,08 % | **0,72 %** · **1,34 %** |

(Porcentaje de un núcleo, en el hilo de mensajes y por instancia con el
editor abierto. x86_64 es Rosetta.)

**Concurrencia:** no hay estructuras nuevas compartidas con el audio, salvo el
entero atómico del banco observado. Se repetirán TSan, la auditoría de
tiempo real (cero asignaciones, bloqueos y esperas) y la identidad del audio
con el editor abierto y cerrado.

### E. Compatibilidad

- **Sin cambios:** `src/dsp`, `src/parameters`, el estado v5 y el PNG
  maestro.
- **Procesador:** solo cambia la elección de qué core se ofrece a la
  telemetría. Es lectura y no altera el audio.
- **Estado de interfaz** (el dominio del analizador en M/S): por instancia y
  en memoria, como la fuente del analizador. No se guarda en la sesión.
- **STEREO:** idéntico bit a bit (compatibilidad estricta con las 14 bases,
  más E4c como base 15). El analizador y la dinámica STEREO usan el mismo
  código.

---

## 2. Origen exacto de muestras y telemetría

| Dato | Escrito por | Dónde | Cuándo |
|---|---|---|---|
| PRE L/R | Hilo de audio, `preTap.write` | `processBlock`, antes de `engine.process` | Solo si el analizador está visible |
| POST L/R | Hilo de audio, `postTap.write` | `processBlock`, después del motor | Igual |
| M/S PRE / POST | **Editor**, al copiar del anillo | `SpectrumView`, antes de `analyse` | Unos 30 fps |
| Bandas dinámicas | Hilo de audio, `offerDynamics` | Tras cada sub-bloque de ≤ 256 muestras | Si la telemetría está activa |
| Banco observado | Editor | `DynamicsTelemetry` (atómico) | Al cambiar el banco mostrado |

---

## 3. Archivos afectados (previstos)

| Archivo | Cambio |
|---|---|
| `src/plugin/DynamicsTelemetry.h` | Banco observado (atómico); etiqueta de banco en la instantánea |
| `src/plugin/PluginProcessor.cpp` | `offerDynamics`: el core del banco observado, solo cuando su ruta se escucha. **No toca el DSP** |
| `src/plugin/PluginProcessor.h` | `get/setUiAnalyzerDomain` (estado de interfaz) |
| `src/ui/SpectrumView.{h,cpp}` | Dominio: L/R, MID o SIDE; codificación M/S de las muestras antes de `analyse`; alineación PRE/POST (si se aprueba D3) |
| `src/ui/DynamicsOverlay.{h,cpp}` | Descartar instantáneas de otro banco; reinicio al cambiar de banco |
| `src/ui/LabQ8Editor.{h,cpp}` | Banco observado; dominio del analizador y sus rótulos; menú "Signal"; superposición activa en M/S; retirada de la nota |
| `tests/SpectrumTests.cpp`, `tests/DynamicsTests.cpp`, `tests/EditorBankTests.cpp` (o un `tests/MidSideViewTests.cpp` nuevo) | Pruebas (§5) |
| `tools/ui/UiSnapshot.cpp` | `--e4d`: capturas |
| `docs/MIDSIDE_E4D.md`, `docs/ROADMAP.md`, `docs/UI_IMPLEMENTATION.md` | Documentación |

**Sin cambios:** `src/dsp/*`, `src/parameters/*`, los assets y el PNG
maestro, `SpectrumTap` (ni su escritura ni su lectura) y
`SpectrumAnalyzer::analyse`. El analizador ya acepta un solo canal.

---

## 4. Riesgos técnicos

| # | Riesgo | Mitigación |
|---|---|---|
| R1 | Mostrar dinámica del banco STEREO (en reserva) como si fuera MID/SIDE | Banco observado más etiqueta en la instantánea más "nada" cuando la ruta no se escucha. Prueba con dinámica distinta en cada banco |
| R2 | Instantánea del banco anterior justo al cambiar de banco | Descarte por etiqueta y reinicio al cambiar |
| R3 | Desfase PRE/POST de un bloque | Igual que hoy; D3 lo elimina si se aprueba |
| R4 | SIDE suele ser débil: espectro casi vacío | Es el valor real. Mono o centrado da SIDE = 0 y no se dibuja nada; se documenta |
| R5 | Que cambie el analizador STEREO | En STEREO, el mismo código y los mismos datos. Prueba de identidad de los niveles por punto |
| R6 | Carreras de datos | Solo un atómico nuevo. TSan y la auditoría de tiempo real |
| R7 | Usabilidad (incidencia de SMOKE-8): en M/S la dinámica de STEREO no suena | Con E4d al menos se verá que MID/SIDE no reducen. Un aviso específico sería un elemento nuevo (D6) |

---

## 5. Pruebas y criterios de aceptación

| # | Prueba | Criterio |
|---|---|---|
| F1 | Seno de amplitud A solo en M (L = R) y solo en S (L = −R), a varias frecuencias | MID PRE/POST marca 20·log10 A ± 0,01 dB en el bin (como el analizador actual); SIDE sin nada, y al revés |
| F2 | Señal mono con SIDE muy procesado | SIDE PRE y POST quedan por debajo del suelo: nada dibujado |
| F3 | MID con +6 dB en f0 | MID POST − MID PRE = +6 dB ± 0,05 en f0. SIDE no cambia |
| F4 | Ruta plana | MID/SIDE POST = PRE, bit a bit en los niveles por punto |
| F5 | STEREO | Niveles por punto idénticos a 52cf2b5 con las mismas muestras |
| F6 | Pista mono | MID = canal, SIDE nada |
| F7 | Silencio y bypass del host | Nada en silencio; PRE = POST en bypass |
| D1 | Dinámica distinta en STEREO, MID y SIDE, en M/S | Al observar MID, la reducción coincide con la del core MID, nunca con la de STEREO; lo mismo con SIDE |
| D2 | Tono solo en M, y luego solo en S | Solo el banco observado correspondiente muestra reducción |
| D3 | Cambio de banco o de modo, en marcha | Ninguna instantánea de otro banco se dibuja; nada durante el fundido |
| D4 | Límite efectivo | Con ganancia estática −20 y rango 12, la reducción dibujada es la efectiva (clamp a ±24) |
| D5 | Bypass, silencio y pista mono en SIDE | "Nada", o retirada tras 100 ms, según el caso |
| U1 | Rótulos y menú | "MID Post", "SIDE Pre + Post" y "L/R …" correctos. La sección "Signal" solo en M/S. STEREO sin cambios |
| T1 | Tiempo real | 0 asignaciones, bloqueos y esperas con la interposición (todas las vistas, M/S, transiciones); TSan sin avisos |
| T2 | Audio | Identidad bit a bit con el editor abierto o cerrado; compatibilidad estricta de STEREO con 15 bases, arm64 y x86_64 |
| T3 | CPU | Editor: M/S ≤ coste L/R actual. Audio: sin cambio medible (banco de medidas). Varias instancias y tamaños de bloque |
| T4 | Mutaciones dirigidas | Banco observado ignorado; etiqueta ignorada; M/S derivado de magnitudes; signo de S; STEREO ofrecido en M/S |
| T5 | Capturas | STEREO, MID y SIDE en SPECTRUM, DYN y ALL, al 40, 60 y 100 % |
| S | Smoke físico (SMOKE-9) | Espectros MID/SIDE y dinámica por banco en Logic con material real |

---

## 6. Coste de CPU (resumen)

- **Hilo de audio:** sin cambio. Las tomas son las mismas, y la telemetría
  lee 8 bandas de otro core más un atómico.
- **Editor:** en M/S, de **0,28 a 1,34 %** de un núcleo con el analizador
  visible (PRE + POST), **por debajo** del coste L/R actual (0,46–2,08 %).

---

## 7. Decisiones para aprobar

| | Decisión | Recomendación |
|---|---|---|
| D1 | Dominio por defecto del analizador en M/S | **El componente del banco editado** (MID o SIDE), con "Output (L/R)" en el menú |
| D2 | Rótulos | "MID Post / MID Pre / MID Pre + Post", igual con SIDE, y "L/R Post" para la salida (todos caben) |
| D3 | Alinear PRE con POST al mismo índice de muestra | Sí, solo en M/S (el L/R de STEREO no cambia); es una mejora del editor |
| D4 | Dónde se guarda el dominio elegido | En memoria por instancia, como la fuente del analizador; no en la sesión |
| D5 | Durante el fundido de 20 ms de un cambio de modo | No mostrar dinámica (como hoy) |
| D6 | Aviso si en M/S MID y SIDE no tienen dinámica y STEREO sí | No añadir elementos nuevos en E4d. E4d ya hará visible que MID/SIDE no reducen; un aviso se trataría aparte |
| D7 | La nota "M/S dynamics display: E4d" | Retirarla |
