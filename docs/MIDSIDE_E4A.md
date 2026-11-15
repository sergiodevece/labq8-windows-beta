# LABQ8 E4a — Contrato de parámetros, estado v5 y validación previa a E4b

**Base:** `152d46c` (UI-2c). **Arquitectura aprobada:**
[MIDSIDE_E4_AUDIT.md](MIDSIDE_E4_AUDIT.md) (tres bancos, STEREO intacto).
**Alcance de E4a:** solo parámetros, estado y pruebas. **No hay procesamiento
M/S, ni crossfade, ni cambios gráficos.** `proc_mode` y los bancos MID/SIDE
existen, se guardan y se pueden automatizar, pero el DSP sigue reproduciendo
solo el banco STEREO, exactamente como en 152d46c.

Archivos de producto modificados: `src/parameters/ParameterIDs.h`,
`ParameterLayout.cpp`, `StateMigration.{h,cpp}`. `PluginProcessor`, DSP e
interfaz sin cambios.

---

## 1. Contrato completo de 241 parámetros

El inventario completo, una línea por parámetro en orden de host, está
congelado en [`tests/fixtures/contract_v5_e4a.txt`](../tests/fixtures/contract_v5_e4a.txt).
Cada línea incluye el índice, el ID, el nombre, la clase, el version hint,
si es automatizable, los IDs de host VST3 y AU, el valor por defecto, el
rango y la rejilla, el mapeo normalizado en cinco puntos, los pasos, la
unidad, las opciones y el texto mostrado. Lo genera
`tests/ParameterContract.h`, que es el mismo código que se compiló contra
152d46c para congelar el contrato v4
([`contract_v4_ui2c.txt`](../tests/fixtures/contract_v4_ui2c.txt)).
El texto es idéntico en arm64 y x86_64.

### 1.1 Orden de host

| Índices | Contenido | Hint |
|---|---|---|
| 0–79 | Banco **STEREO**: los 80 de v4, **sin ningún cambio** (verificado línea a línea contra 152d46c) | 1–4 |
| 80 | `proc_mode` | 5 |
| 81–160 | Banco **MID**: b1..b8; en cada banda, freq, gain, q, bypass, type, dyn_on, threshold, range, attack, release | 5 |
| 161–240 | Banco **SIDE**: igual que MID | 5 |

### 1.2 `proc_mode`

| ID | Nombre de host | Tipo | Opciones (índice congelado) | Por defecto | Automatizable |
|---|---|---|---|---|---|
| `proc_mode` | Stereo Mode | choice | 0 = `Stereo`, 1 = `Mid/Side` | Stereo | sí |

Ya estaba reservado en PARAMETER_SCHEMA con esos mismos valores. Solo elige
qué banco procesa; **nunca reescribe los parámetros de ningún banco**.

### 1.3 Bancos MID y SIDE (`n` = 1..8)

| ID MID | ID SIDE | Nombre de host | Tipo | Rango | Por defecto | Mapeo |
|---|---|---|---|---|---|---|
| `mid_b{n}_freq` | `side_b{n}_freq` | Mid/Side Band n Frequency | float | 20–20 000 Hz | igual que `b{n}_freq` (1k, 40, 100, 250, 2,5k, 5k, 10k, 16k) | log |
| `mid_b{n}_gain` | `side_b{n}_gain` | … Gain | float | ±24 dB, paso 0,01 | 0 | lineal |
| `mid_b{n}_q` | `side_b{n}_q` | … Q | float | 0,1–18 | 1 | log |
| `mid_b{n}_bypass` | `side_b{n}_bypass` | … Bypass | bool | off/on | off | — |
| `mid_b{n}_type` | `side_b{n}_type` | … Type | choice | los 6 tipos de `b{n}_type` | Bell | índice |
| `mid_b{n}_dyn_on` | `side_b{n}_dyn_on` | … Dynamic EQ | bool | off/on | off | — |
| `mid_b{n}_threshold` | `side_b{n}_threshold` | … Threshold | float | −80…+24 dBFS, paso 0,1 | −20 | lineal |
| `mid_b{n}_range` | `side_b{n}_range` | … Range | float | 0–24 dB, paso 0,1 | 6 | lineal |
| `mid_b{n}_attack` | `side_b{n}_attack` | … Attack | float | 0,5–500 ms, rejilla 0,01 | 10 | log |
| `mid_b{n}_release` | `side_b{n}_release` | … Release | float | 5–5000 ms, rejilla 0,1 | 100 | log |

- **Mismo significado que STEREO:** una prueba comprueba, parámetro a
  parámetro, que cada MID/SIDE tiene la misma descripción que su homólogo
  STEREO (clase, rango, rejilla, mapeo, valor por defecto, unidad, opciones y
  textos). Solo cambian el ID, el nombre y el hint.
- **Nombres:** los 80 STEREO conservan "Band n …"; los nuevos llevan
  "Mid Band n …" o "Side Band n …".
- **Sin grupos de parámetros** en E4a. Agrupar cambia la presentación en el
  host y no se puede comprobar sin Logic. Se reconsiderará en E4e.
- **IDs no reutilizados:** `b{n}_place`, `b{n}_slope`, `b{n}_sc`, `out_gain` y
  `master_bypass` siguen reservados y no existen (probado). El modelo de
  bancos sustituye a `b{n}_place` para E4, pero ese ID no se usa para otra cosa.

### 1.4 Automatización: direcciones de host

JUCE 9.0.3 deriva los IDs de host del texto del ID, con
`String::hashCode() & 0x7fffffff`: VST3 `convertJuceParameterId` y AU
`generateAUParameterID`, con la configuración por defecto
`JUCE_USE_STUDIO_ONE_COMPATIBLE_PARAMETERS = 1`. Verificado:

- Los 80 IDs de host de v4 no cambian (forman parte del contrato congelado).
- Los 241 IDs de host son distintos entre sí y ninguno coincide con los
  reservados por el wrapper VST3 (`'prst'` programas, `'byps'` bypass).
- **Orden AU:** JUCE ordena la lista por ID de host y después, de forma
  estable, por hint. El hint 5 va después del 1–4, así que los 80 de v4
  conservan su orden AU y los nuevos van detrás (probado).
- **Orden por índice (VST3 y envoltorios):** solo se ha añadido al final; los
  índices 0–79 no cambian.
- **Verificado en los binarios reales (VST3).** Se cargaron el VST3 de
  SMOKE-6 (152d46c) y el de E4a en un host VST3 de JUCE. Los ParamID que ve
  el host coinciden con los del contrato en los 241 parámetros; los 80 de v4
  mantienen ParamID, índice y nombre; los 242 ParamID (241 + bypass del
  wrapper) son distintos.
- **Bypass del wrapper VST3 (hallazgo):** el wrapper VST3 de JUCE añade su
  propio parámetro "Bypass" (ParamID `'byps'` = 1652125811), siempre al
  final de la lista. En 152d46c ocupa el índice de host 80; en E4a, el 241,
  y el 80 pasa a ser `proc_mode`. Su ParamID no cambia, y los hosts VST3
  direccionan por ParamID, así que la automatización del bypass no se ve
  afectada. Un host o envoltorio que direccionara por índice sí lo estaría:
  conviene comprobarlo en el entorno de Miguel. El AU no tiene este
  parámetro (usa `kAudioUnitProperty_BypassEffect`).

---

## 2. Migración de sesiones v4: sonido y automatización

- **Mecanismo (sin cambios de código):** la migración genérica añade cada
  parámetro ausente con su valor por defecto **explícito**. Una sesión v1–v4
  llega a v5 con 241 `PARAM`, `proc_mode = Stereo` y MID/SIDE en sus valores
  por defecto. Ningún valor guardado se modifica.
- **Sonido:** el DSP sigue sin leer `proc_mode` ni los bancos MID/SIDE, así
  que una sesión v4 suena igual bit a bit. Lo prueban:
  - la compatibilidad estricta contra 152d46c en arm64 y x86_64, con casos
    nuevos de dinámica: las sesiones UI-2c auténticas `ui2c_default` y
    `ui2c_custom` y automatización aleatoria de la dinámica y del encendido;
  - en proceso, la salida en Stereo es idéntica bit a bit tenga lo que tenga
    MID/SIDE, en mono y estéreo, a 44,1, 48 y 96 kHz y con bloques de 64 y 512.
- **Determinismo:** cargar un estado antiguo en una instancia nueva o en una
  con los 241 parámetros alterados (incluido `proc_mode = Mid/Side`) produce
  exactamente los mismos bytes al volver a guardar. Guardar de nuevo es un
  punto fijo.
- **Automatización:** las direcciones de host no cambian (§1.4). La
  automatización existente sigue apuntando al banco STEREO, que conserva su
  significado.
- **Al pasar a M/S (desde E4b):** una sesión antigua tiene MID y SIDE planos.
  Cambiar a M/S la deja sin la EQ estéreo **hasta que el usuario copie los
  ajustes de forma explícita** (comandos de copia, E4c). Ningún cambio de modo
  modifica otros bancos.

---

## 3. Serialización v5

```xml
<LABQ8 labq8StateVersion="5">
  <PARAM id="b1_attack" value="10.0"/>
  …                                      (80 STEREO)
  <PARAM id="proc_mode" value="0.0"/>    (índice de la opción)
  <PARAM id="mid_b1_freq" value="1000.0"/>
  …                                      (80 MID)
  <PARAM id="side_b1_freq" value="1000.0"/>
  …                                      (80 SIDE)
</LABQ8>
```

- Mismo formato que v1–v4: `copyXmlToBinary` del árbol de
  `AudioProcessorValueTreeState`, raíz `LABQ8`, un único atributo
  `labq8StateVersion`, un `PARAM` por parámetro con su valor desnormalizado.
  El orden de los hijos lo decide JUCE y no tiene significado: la carga
  busca cada `PARAM` por su ID. No hay subárboles por banco: el banco de cada
  `PARAM` se deduce de su ID.
- **Probado:** exactamente 241 `PARAM`, cada uno con `id` y `value` y nada
  más; ida y vuelta exacta de los 241 valores, como los lee el DSP, en 20
  sesiones aleatorias. Excepción: frecuencia y Q, congelados desde E0,
  pueden moverse un ulp en el primer ciclo por su mapeo logarítmico, como en
  v4. El segundo guardado ya es un punto fijo.
- Estado incompleto: los parámetros que faltan reciben su valor por defecto.
  Los `PARAM` desconocidos se conservan y se vuelven a guardar sin cambios.
- El banco que se edita (MID/SIDE) es estado de interfaz y **no** se guarda
  en v5.

---

## 4. Cómo se evita escribir en el banco equivocado

| Vía | Garantía | Estado |
|---|---|---|
| **Automatización de host** | Cada ID corresponde a un solo parámetro de un solo banco. Probado: escribir cualquiera de los 241 por la vía del host (`setValueNotifyingHost` con gesto) cambia **solo** ese parámetro, también en el valor que lee el DSP | E4a, probado |
| **Selección de IDs en el código** | `params::bankOf (Bank)` devuelve los IDs de un banco y su prefijo de nombre. Regla: todo código que elija un ID de banda lo hace con un `Bank` explícito. El banco STEREO es exactamente `bandIDs` + `dynamicIDs` (la misma memoria; probado) | E4a: acceso creado |
| **Potenciómetros, selector de tipo, interruptor dinámico, bypass** | E4c sustituye los 11 usos directos de `bandIDs` / `dynamicIDs` del editor por `bankOf (editBank)` y vuelve a enlazar las conexiones al cambiar de banco, como ya se hace al cambiar de banda | E4c |
| **Nodos y tiradores de Q** | La pantalla lee y escribe por `bankOf (editBank)`; la curva fantasma del otro banco no tiene nodos ni recibe clics | E4c |
| **DSP** | El procesador asigna cada banco a su `EqCore` mediante `bankOf (bank)` | E4b |

**Pruebas añadidas en E4c (definidas ahora):** con el editor en SIDE, se
manipula cada potenciómetro, el tipo, el interruptor dinámico, el bypass y
los nodos y tiradores de cada banda. Solo pueden cambiar IDs `side_*`.
Después se repite con MID (solo `mid_*`) y en STEREO (solo los 80 STEREO).
También se comprueba que cambiar de banco nunca escribe parámetros y que
la automatización del banco no editado se refleja en sus valores sin mover
los controles en pantalla.

---

## 5. Sesiones v5 en versiones anteriores: incompatibilidad y política

**Comportamiento verificado con el build de 152d46c**
(fixture `ui2c_resaved_from_v5.bin`):

- Abre la sesión sin errores, restaura los 80 STEREO e **ignora**
  `proc_mode` y los bancos MID/SIDE. **Suena en STEREO aunque se guardase en
  M/S.** Esta es la incompatibilidad: una mezcla M/S cambia de sonido en una
  versión anterior, sin aviso.
- Al volver a guardar, **conserva las 241 entradas** y etiqueta el estado
  como v4. Si esa sesión se reabre en E4, recupera MID, SIDE y `proc_mode`
  (probado). Lo que el usuario haya cambiado en la versión anterior se
  conserva.
- Las versiones anteriores a UI-1 tienen el mismo mecanismo de APVTS, pero
  esto solo se ha verificado con 152d46c.

**Política propuesta:**

1. **Las versiones distribuidas no se tocan en E4a** (ni número de versión,
   ni instalación, ni la beta VST3 de Miguel).
2. **Número de versión:** antes de distribuir la primera build con M/S
   (E4e), subir de `0.0.1` a `0.1.0`. Así Logic revalida el AU y la lista de
   parámetros, y las sesiones se distinguen por versión. Las builds internas
   de E4a–E4d no se instalan ni se distribuyen.
3. **Copias:** antes de abrir una sesión de trabajo con una build E4, el
   usuario hace "Guardar como" una copia. Al distribuir E4, la nota de versión
   indicará: "una sesión guardada en M/S suena en STEREO en versiones
   anteriores; no la abráis con betas anteriores".
4. **Miguel:** no abrir con la beta actual sesiones guardadas con una build
   E4. Si ocurre, la sesión no se rompe (se conservan todos los valores),
   pero suena en STEREO hasta reabrirla en E4.
5. **Versiones futuras (v6+):** la migración debe seguir siendo aditiva o
   idempotente. Un estado de una versión más nueva se carga, conserva lo
   desconocido y se guarda con el número de la versión que lo escribe
   (probado con un v6 sintético).

---

## 6. Plan de validación de los cambios STEREO ↔ M/S (E4b)

**Los 20 + 20 ms son provisionales.** Se elegirán con medidas antes de fijarlos.

**Candidatos:** precalentamiento + fundido de 10 + 10, 20 + 20 y 40 + 40 ms;
además, fundido sin precalentamiento como referencia de lo que se evita.

**Señales:** ruido rosa estéreo (correlación 0, 0,5 y 0,9), senos de 50 Hz,
1 kHz y 8 kHz, mono (L = R), contrafase (L = −R), batería y bajo reales
(material de los smokes) y silencio. Con bancos copiados (STEREO →
MID + SIDE) y con bancos muy distintos (±12 dB, Q altos, dinámica activa).

**Medidas, cada una con un criterio:**

| Medida | Criterio provisional |
|---|---|
| Transitorio: 4ª diferencia máxima durante el cambio, frente a la señal estable (métrica de clics de E1b/E2) | Sin escalones por encima del de un cambio de tipo de filtro de 20 ms (referencia aprobada) |
| Con bancos copiados: diferencia frente a STEREO durante el cambio | Sin dinámica: ≤ −120 dB. Con dinámica: solo la diferencia de detección, sin pico en el cambio |
| Nivel RMS en ventanas de 5 ms durante el cambio | Sin hundimientos ni sobreimpulsos > 0,1 dB con bancos copiados |
| Ensuciamiento espectral (FFT del error) | Por debajo de −90 dBFS con bancos copiados |
| Inversión a mitad de fundido y conmutación cada 50 ms | Salida finita y continua; sin acumulación |
| Independencia del tamaño de bloque | Mismo resultado para bloques de 1 a 4096, con el cambio en la misma muestra |
| CPU durante el cambio | Pico por bloque dentro del presupuesto con 16 bandas dinámicas |
| Escucha (Logic, E4e) | Sin clic audible en conmutaciones manuales y automatizadas |

**Decisión:** se fijará el par más corto que cumpla todos los criterios.
Se presentará con los números antes de cerrar E4b.

---

## 7. Matriz de pruebas de señal (E4b; las de E4a ya marcadas)

| Señal → / Situación ↓ | Mono (pista 1 canal) | L = R (estéreo mono) | Estéreo real | Contrafase L = −R | Solo SIDE (L = −R, MID = 0) | Silencio |
|---|---|---|---|---|---|---|
| **STEREO, MID/SIDE cualesquiera** | bit a bit = 152d46c **(E4a ✔)** | bit a bit **(E4a ✔)** | bit a bit **(E4a ✔)** | bit a bit | bit a bit | ceros |
| **M/S, bancos planos** | salida = entrada | salida = entrada (exacto) | salida = entrada (ruta rápida) | exacto | exacto | ceros |
| **M/S, solo MID activo** | MID actúa sobre la señal | L' = R' bit a bit; = H_MID·L | MID cambia M; S intacto (≤ 1 ulp) | MID sin efecto: L' = −R' exacto | sin efecto | ceros, detectores en el suelo |
| **M/S, solo SIDE activo** | **SIDE sin señal ni efecto** (aprobado) | SIDE sin efecto: L' = R' = L exacto | SIDE cambia S; M intacto (≤ 1 ulp) | SIDE actúa sobre todo | SIDE actúa | ceros |
| **M/S, ambos activos** | solo MID | solo MID | H_MID·M + H_SIDE·S (±0,1 dB de \|H\|) | solo SIDE | solo SIDE | ceros |
| **Independencia** | — | cambiar SIDE: 0 bits | cambiar cada uno de los 80 MID no altera S ni el estado SIDE, y viceversa | idem | idem | — |
| **Dinámica por banco** | MID detecta como STEREO | tono en M: reduce MID, SIDE 0 | tono solo en S: reduce SIDE, MID 0 | detector MID en el suelo | detector MID en el suelo | sin reducción |
| **Correlación** | — | 1 | MID↑ la sube y SIDE↑ la baja, como predice \|H\| | −1 se mantiene | — | — |
| **Banco STEREO en M/S** | sin efecto | sin efecto | sin efecto (bit a bit) | sin efecto | sin efecto | — |

Todas en 44,1, 48 y 96 kHz, bloques de 64 y 512 (y de 1 a 4096 para los
cambios de modo), arm64 y x86_64, sin asignaciones (`AllocationGuard`).

---

## 8. Exactitud y límites de las rutas rápidas M/S

Medido ahora con un programa de análisis aislado (no forma parte del
producto): 4 millones de pares por clase, codificación
`M = (L+R)/2, S = (L−R)/2` y decodificación `L = M+S, R = M−S`, con
intermedio double (propuesto) y solo float, ejecutado en x86_64.

| Señal | Muestras exactas | Peor error |
|---|---|---|
| Mono L = R | **100 %** | 0 |
| Contrafase L = −R | **100 %** | 0 |
| Un canal a 0 | **100 %** | 0 |
| Aleatoria ±1 | 84,5 % | 1 ulp del canal mayor (−138,5 dB relativos) |
| Correlación 0,9 | 54,5 % | 1 ulp del canal mayor |
| Rango dinámico de 200 dB | 3,0 % | 1 ulp del canal mayor |

Intermedio double y float dan los mismos resultados. El double no mejora la
ida y vuelta, así que la elección queda para E4b.

**Conclusiones y límites:**

1. **Sin ruta rápida, el M/S no es transparente bit a bit** con material
   estéreo real (54–85 % de muestras exactas). El error tiene una cota firme
   de 1 ulp del canal mayor, ≈ −138 dB, inaudible, pero haría fallar las
   pruebas de nulo.
2. **Ruta rápida A (sin codificar):** cuando `midCore` y `sideCore` están
   ociosos (`EqCore::isIdle()`: sin dinámica activa y todas las bandas
   ociosas), el motor no codifica y la salida es la entrada, **bit a bit por
   construcción**. Límites:
   - Basta con una banda activa en cualquiera de los dos bancos para que
     codifique todo. La componente no procesada pasa con ≤ 1 ulp.
   - Una banda queda ociosa cuando termina su rampa o fundido y el estado
     residual cae por debajo de `idleStateThreshold = 1e-9` (≈ −180 dBFS).
     Al entrar en la ruta rápida la discontinuidad es ese residuo más
     ≤ 1 ulp, igual que la transición a ociosa de una banda que ya existe.
   - No se aplica durante el precalentamiento ni durante el fundido.
3. **Rutas exactas sin ruta rápida:** mono, contrafase y un canal en silencio
   dan siempre M y S exactos. Por eso "SIDE no altera una entrada mono" y
   "MID no altera una contrafase" son exactos bit a bit, no solo
   aproximados.
4. **Pista mono (1 canal):** no hay codificación. MID procesa la señal y SIDE
   no recibe señal y no puede alterar la salida (decisión aprobada).
5. **Subnormales:** `ScopedNoDenormals` activa FTZ/DAZ. Su interacción con el
   codificador (en float o en double) se probará expresamente en E4b. En la
   práctica no importa (< 1,2·10⁻³⁸) y no cuenta para la transparencia.

---

## Resultados de E4a (07-11-2026, árbol de trabajo sobre 152d46c, sin commit)

- **Pruebas (Release universal, `build/e4a-release-universal`):** 176/176 en
  arm64 y en x86_64 (Rosetta). Hay 177 pruebas: la que falta es el generador
  de fixtures, que solo corre con `LABQ8_WRITE_FIXTURES`. 14 pruebas son
  nuevas (`tests/E4aParameterTests.cpp`). En 7 pruebas anteriores solo se
  han actualizado los recuentos (80 → 241) y la versión (4 → actual). Lo que
  comprueban sobre los índices 0–79 no cambia. Salidas:
  `build/e4a-tests_{arm64,x86_64}.txt`.
- **Compatibilidad estricta (`tests/compat/run_compat.sh all`):** 12 bases
  (E0 … UI-2c) × 2 arquitecturas, todas OK. E0 y E1b, solo con las
  diferencias ya documentadas (0 dB nominal y límite de Q); el resto, sin
  ninguna diferencia. Los 4 casos nuevos de dinámica son bit a bit idénticos
  en las 6 bases que tienen dinámica. Salida: `build/e4a-compat-all.txt`.
- **Builds:** AU y VST3 universales (x86_64 + arm64) compilan sin avisos
  propios. **auval no se ha ejecutado**: necesita el componente instalado y
  la instalación no está autorizada en E4a. Queda para la etapa que la
  autorice.
- **Binarios reales VST3 (07-11-2026 por la mañana, arm64 y x86_64):** host de
  verificación JUCE (`build/e4a-report/vst3-host-check.cpp`, resultados en
  `vst3-host-check_{arm64,x86_64}.txt`):
  - IDs: ver §1.4.
  - Una sesión v4 del binario antiguo, abierta en el de E4a, suena idéntica
    bit a bit, con los valores en los mismos ParamID y MID/SIDE/`proc_mode`
    en sus valores por defecto.
  - Una sesión v5 abierta en 152d46c: restaura los 80 STEREO y suena igual
    que una instancia antigua solo con esos valores. Guardada allí y
    reabierta en E4a, recupera los 241 valores.
  - Ida y vuelta de v5 por VST3: 0 valores cambiados y punto fijo.
  - Observación: en una instancia ya usada, la caché del host de JUCE
    conserva valores booleanos sin ajustar escritos por el propio host cuando
    el valor del plugin no cambia. El estado real del plugin es idéntico byte
    a byte y 152d46c se comporta igual: es anterior a E4a y no afecta al plugin.
- **Repetición del 07-11-2026 por la mañana:** sin cambios en el código desde las
  ejecuciones anteriores; 176/176 en las dos arquitecturas y compatibilidad
  estricta contra 152d46c OK de nuevo.
- **Diffs de revisión:** `build/e4a-report/e4a-code.diff` y
  `build/e4a-report/e4a-docs.diff`.

## SMOKE-7 en Logic Pro: aprobado (11-11-2026)

**Build instalado** con `tools/install/install_au.sh`, con Logic cerrado:
- carpeta `build/e4a-smoke-release-universal`, commit `c9ea214` (árbol limpio);
- SHA-256 del binario AU: `f274c663ecc826e7ff8d234f0409f0bacd31790945c8c65760a596e3baa282ef`;
- 176/176 pruebas en arm64 y x86_64; compatibilidad estricta en 12 bases × 2
  arquitecturas; verificación de los binarios VST3 reales contra SMOKE-6;
- auval `-strict` en arm64 y x86_64: AU VALIDATION SUCCEEDED, 241 parámetros
  en el orden AU del contrato, con los 80 primeros en el mismo orden que en
  SMOKE-6;
- versión AU 0.0.1, sin cambios: JUCE no persiste la lista de parámetros y
  Logic la lee del plugin al abrirlo.

Lista de comprobaciones: `build/e4a-smoke-report/E4A_PRUEBA_LOGIC.md`
(apertura, sesiones anteriores, ocho bandas y controles, EQ estática y
dinámica Bell, medidores, FFT, DYN, automatización y bypass, zoom y
preferencias, guardar y reabrir, sin alteraciones sonoras). MID/SIDE no se
evaluó como función: no existe hasta E4b.

**Resultado:** PASS, 10/10 comprobaciones manuales superadas en Logic Pro. E4a queda cerrada.

**Copia de UI-2c (SMOKE-6):** `build/e4a-smoke-report/LABQ8.component.smoke6-backup`.
