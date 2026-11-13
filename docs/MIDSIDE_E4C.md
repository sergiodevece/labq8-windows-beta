# LABQ8 E4c — Interfaz STEREO / MID / SIDE

**Base:** `d27b9b1` (E4b); checkpoint `500b381`. **Estado:** revisión técnica
y visual **aprobadas** (13-11-2026), con el prefijo de la cabecera a
mayúscula 11. Smoke físico pendiente de autorización. Auditoría y decisiones:
[MIDSIDE_E4C_AUDIT.md](MIDSIDE_E4C_AUDIT.md).

**Sin cambios:**
- `src/dsp/*` y `src/parameters/*`: el contrato de 241 parámetros y el
  estado v5 no cambian;
- el PNG maestro;
- la telemetría DSP, las tomas del analizador y los medidores.

**Instalación:** ninguna; el AU de SMOKE-7 sigue intacto.

---

## 1. Lo implementado

| Decisión | Implementación |
|---|---|
| **D1** | STEREO y M/S son botones reales, enlazados a `proc_mode` con un gesto completo por clic. El encendido usa la **cara verde del maestro**. El banco seleccionado (MID o SIDE) también se marca con la cara verde |
| **D2** | "MID" y "SIDE" usan Helvetica Neue Condensed Bold con mayúscula de 13 px, igual que "M/S" |
| **D3 (revisado)** | En M/S la cabecera muestra "SIDE  BAND 4": prefijo pequeño en el gris de las etiquetas del chasis, y "BAND n" sin cambios. Tras la revisión visual, el prefijo tiene **mayúscula de 11 px de referencia** (antes 9; ver §4). Acaba como mucho en x 299 ("SIDE  BAND 8"), lejos de "Filter Type" (x 346) |
| **D4** | En STEREO, MID y SIDE aparecen atenuados y no responden, y el banco STEREO se edita como siempre |
| **D5** | Curva compuesta del banco contrario: gris (200,200,190, 40 %), de 1 px, sin relleno ni nodos ni clics. Solo en M/S y en las vistas EQ y ALL, con la etiqueta "MID" o "SIDE" pequeña en su extremo izquierdo |
| **D6** | El banco editado es estado de interfaz por instancia (`getUiEditBank`, MID por defecto). No es un parámetro y no forma parte del estado v5. Una sesión restaurada muestra el banco que corresponde a su modo: STEREO, o MID/SIDE |
| **D7 (corregida)** | En M/S la curva editable es exactamente la del banco MID o SIDE. La superposición dinámica se desactiva (solo conoce el banco STEREO), y en DYN y ALL una nota discreta dice "M/S dynamics display: E4d". El analizador se identifica como **"L/R Pre / L/R Post / L/R Pre+Post"**, con su tooltip. No se ha tocado la telemetría |
| **D9** | El número de banda seleccionada se mantiene al cambiar de banco |
| **D10 (corregida)** | En pistas mono, SIDE se puede seleccionar y editar, sin bloqueos. El motor sigue sin generar estéreo (prueba T11) |
| **D8** | Comandos en el menú del icono de ajustes ("Mid/Side banks"): Copy STEREO → MID + SIDE, Copy MID → SIDE y Copy SIDE → MID (§3) |

### Vinculación de parámetros: los seis puntos

Todo pasa por un único **banco mostrado** (`params::bankOf (shownBank)`):

1. **`parameterId (bank, band, control)`**: los 7 potenciómetros.
2. **`attachPanel()`**: el selector de tipo, el interruptor dinámico y el
   indicador de bypass.
3. **Los 8 `BandButton`:** se vuelven a enlazar al bypass del banco
   mostrado.
4. **`ResponseScreen::readBands (bank)`:** curva, nodos, tiradores y
   arrastres. Cada arrastre toma sus parámetros al empezar.
5. **`parameterSnapshot()`:** los 120 parámetros estáticos de los tres
   bancos más `proc_mode`. La pantalla se redibuja si se automatiza el banco
   mostrado o el gris.
6. **`DynamicsOverlay`:** lee las bandas del banco mostrado a través de la
   pantalla. En M/S está desactivada (D7).

Ya no queda ningún uso de `params::bandIDs` / `params::dynamicIDs` en
`src/ui`.

**Gestos:**
- Un cambio de banco o de modo, por ejemplo por automatización, **espera** si
  hay un potenciómetro o un nodo arrastrándose. El gesto se cierra sobre el
  parámetro en que empezó y después se cambia de banco.
- **Detalle de JUCE:** `Slider::stoppedDragging()` llega *antes* de que el
  enlace cierre el gesto, así que el cambio se aplica de forma asíncrona (o
  en el siguiente tick de 30 Hz). Lo descubrió la prueba T6.
- El menú de tipo de filtro aplica la elección solo al parámetro para el que
  se abrió. Si el banco ha cambiado mientras tanto, la elección se descarta.

### Assets

`tools/ui/GenerateUiAssets.cpp`:

- borra el botón L/R del fondo derivado (interpolación por filas del pozo
  oscuro);
- recorta del maestro dos caras sin texto (`mode_face_lit.png` del STEREO
  encendido y `mode_face_unlit.png` del M/S), que el editor compone en 9
  tramos **en una sola imagen** por tamaño.

El generador reproduce los demás assets **idénticos byte a byte**. El
`background.png` nuevo solo cambia **4691 píxeles, todos dentro de x 662–761,
y 86–132** (el hueco de L/R). **El PNG maestro no cambia** (SHA-256
`4e14b925…`).

---

## 2. Validación

| | Resultado |
|---|---|
| **Suite Release** (`build/e4b-release-universal`) | **209/209 en arm64 y 209/209 en x86_64** tras la revisión visual. Hay 210 pruebas; la omitida es el generador de fixtures. 17 son nuevas (`tests/EditorBankTests.cpp`). En la primera entrega eran 207/207, con 15 |
| Pruebas UI-1…UI-2c existentes | Solo dos ajustes: el ayudante `randomise()` fija el modo Stereo (esas pruebas tratan del banco STEREO), y el área inactiva "Stereo Mode" se retira de su lista |
| **T1** Escrituras por banco | En STEREO, MID y SIDE, y en 3 bandas cada uno: 7 potenciómetros más su reinicio, tipo, interruptor dinámico, arrastre de nodo, tirador de Q y los 8 botones ON/OFF. **El conjunto de IDs cambiados es exactamente el esperado del banco mostrado** |
| **T2** Botones | STEREO y M/S escriben solo `proc_mode`, con 1 gesto. El encendido, MID y SIDE no escriben nada ni abren gestos |
| **T3** Conservación | Cambiar de banco o de modo no cambia ninguno de los 241 valores, y el panel, los botones ON/OFF y la pantalla muestran exactamente el banco nuevo |
| **T4 y T5** Automatización | El modo automatizado cambia el banco mostrado en el siguiente tick. La automatización del otro banco redibuja solo la curva gris (la caché se invalida; se reutiliza si nada cambia) |
| **T6** Arrastre y modo | Potenciómetro y nodo: el cambio espera, los gestos quedan equilibrados y no se escribe nada fuera de la banda MID arrastrada ni en el banco STEREO |
| **T7** | En STEREO, MID y SIDE no responden y el banco STEREO es editable |
| **T8** | El audio es idéntico bit a bit con el editor abierto o cerrado mientras cambia de banco, banda y vista, en Stereo y en M/S |
| **T9** Geometría | Los cuatro botones están en sus marcos medidos, sin solaparse. La cabecera no se recorta y acaba en x 299 como máximo |
| Enrutado | En los seis zooms, 40 % incluido, los clics en el centro y a 6 px de cada borde de STEREO, M/S, MID y SIDE llegan a ese botón. MID y SIDE miden 18,8 × 17,2 pt al 40 % |
| **T10** Sesiones | El banco editado sobrevive a cerrar y abrir el editor; una instancia nueva empieza en MID. Una sesión v5 en M/S abre en MID y una v4 en STEREO. Una sesión cargada con el editor abierto también se sigue |
| **T11** Mono | SIDE se puede seleccionar y editar en una pista mono, y sus cambios no alteran el audio |
| Cambios rápidos | 600 acciones aleatorias (botones, automatización del modo, bandas): el panel siempre muestra el banco correcto, solo se escribe `proc_mode` y los gestos quedan equilibrados |
| D7 | Superposición dinámica, nota, curva gris y texto del analizador verificados en las 4 vistas × 2 modos |
| **Copia** | Tres pruebas (§3) |
| **Mutaciones de interfaz** | **14/14 detectadas** (`build/e4c-mutation.txt`): botones ON/OFF, potenciómetros, nodos, tipo e interruptor fijos a STEREO; botón de banco que escribe un parámetro; cambio aplicado dentro de `stoppedDragging`; cambio no diferido; copia sobre el origen; copia sin comprobar si ya es igual; superposición en M/S; cabecera recortada; curva gris en Stereo; botón de modo sin sincronizar; instantánea sin MID/SIDE |
| **DSP** | `git diff d27b9b1 -- src/dsp src/parameters` está vacío. En `PluginProcessor.h` solo se añade `get/setUiEditBank` (estado de interfaz) |
| **Compatibilidad estricta** | Ver §5 |

**Fallos reales que encontraron las pruebas y las capturas durante E4c, ya
corregidos:**

1. **Cierre del gesto** al cambiar de banco al final de un arrastre de
   potenciómetro (T6, explicado en §1).
2. **Botones de modo:** el enlace de JUCE no avisa a su propio componente, y
   el editor no cambiaba de banco al pulsar M/S (T2).
3. **Cabecera recortada** ("SIDE BAN…", visto en las capturas). T9 comprueba
   ahora el recorte.
4. **Líneas finas** en las caras de los botones a zoom no entero (visto en
   las capturas). Ahora cada cara es una sola imagen compuesta.
5. **Copias repetidas** que volvían a escribir frecuencia, Q y booleanos
   (§3).

**Corrección de una evidencia de E4b:**
- **El fallo:** el script de mutaciones de E4b podía probar un binario
  anterior. `make` en macOS compara fechas con resolución de 1 s y no
  recompila si el archivo mutado se escribe en el mismo segundo.
- **Repetición:** con la recompilación forzada, **las 12 mutaciones de E4b
  vuelven a quedar detectadas**, cada una por las pruebas que corresponden a
  su fallo (`build/e4b-report/mutation-rerun.txt`).
- **Conclusión:** el 12/12 se mantiene. Las 14 de E4c ya se pasaron con el
  script corregido.

---

## 3. Comandos de copia (D8)

Están en el menú del icono de ajustes, sección "Mid/Side banks", y solo se
ejecutan por acción explícita del usuario.

- **Qué copia:** los 10 parámetros de cada una de las 8 bandas (frecuencia,
  ganancia, Q, bypass, tipo, dinámica on, umbral, rango, ataque y release).
  80 por banco de destino; Copy STEREO → MID + SIDE escribe hasta 160.
- **Cómo:** cada parámetro que cambia se escribe como **un gesto completo**
  (`begin`, `setValueNotifyingHost`, `end`). El host recibe una notificación
  por parámetro.
- **Cuándo no escribe:** si el destino ya tiene ese valor, no escribe nada.
  Repetir una copia no genera tráfico (probado).
- **Qué no toca:** nunca escribe el banco de origen, `proc_mode` ni el banco
  mostrado (probado). Cambiar de modo o seleccionar un banco nunca copia
  nada (T3).
- **Exactitud de los valores:** 154 de 160 son exactos. Los 6 restantes son
  todos frecuencias, a unos pocos ulp de float, por el mapeo logarítmico de
  JUCE (el mismo efecto que en una ida y vuelta de guardado). La mayor
  desviación es de **0,00074 cents** en frecuencia y **0 %** en Q (prueba).
  El umbral de percepción de frecuencia está en torno a 5 cents, más de
  6000 veces por encima.
- **Diferencia de audio "4,8·10⁻⁷", unidades y método:**
  - **Prueba:** "e4c copy: after STEREO -> MID + SIDE (static bank)…".
  - **Montaje:** 241 parámetros aleatorios, con la dinámica de STEREO
    apagada para comparar la parte estática. Copy STEREO → MID + SIDE desde
    el menú.
  - **Entrada:** la misma en los dos casos, 1 s a 48 kHz (100 bloques de
    480): ruido ±0,3 más dos senos, estéreo.
  - **Proceso:** primero en Stereo y, tras un `prepareToPlay`, en Mid/Side.
  - **Métrica:** máximo de |salida Stereo − salida M/S| en todas las
    muestras de los dos canales, en **valor de muestra float lineal (1,0 =
    0 dBFS)**.
  - **Resultado:** **4,77·10⁻⁷ = 2⁻²¹** (−126,4 dBFS), para un pico de salida
    de 2,36 (+7,5 dBFS, por las ganancias aleatorias). Es **−133,9 dB
    relativo al pico**, unos 2 ulp de float a esa amplitud.
  - **Origen:** el redondeo del codificador M/S (≤ 1 ulp) más esos pocos ulp
    de frecuencia.
  - **Lectura:** no es un cambio audible ni significativo.
- **Detectores tras copiar:** con dinámica activa copiada a MID y SIDE, un
  tono solo en M reduce MID 9 dB y SIDE 0 dB, y un tono solo en S al revés
  (prueba). Copiar ajustes no une los detectores.
- **No se refactoriza** para eliminar esos pocos ulp: no hay ninguna
  necesidad audible ni funcional, y obligaría a escribir valores sin pasar
  por la vía normal de parámetros del host.
- **Persistencia:** el banco copiado se recupera tras guardar y reabrir
  (probado).
- **Contrato público:** sin cambios. Se usan los parámetros y el estado v5
  existentes.
- **Undo:** JUCE no ofrece deshacer desde el plugin. El deshacer depende del
  host: cada parámetro es un gesto independiente, así que **no es una
  transacción atómica** y Logic puede registrarlo como varios pasos. Queda
  pendiente comprobarlo en Logic.
- **Automatización:** si el host está en escritura, la copia queda grabada.
  Si el banco de destino tiene automatización en lectura, el host la vuelve a
  aplicar durante la reproducción, como con cualquier edición.
- **Riesgo:** bajo; no cambia ningún ID ni el estado. **Se mantienen en la
  primera entrega.**

---

## 4. Capturas reales y legibilidad

Generadas con el editor real: `labq8_ui_snapshot … --e4c`, salida en
`build/e4c-captures/`.

- **Vistas:** STEREO / ALL, MID / ALL, SIDE / ALL, SIDE / EQ y MID / DYN.
- **Zooms:** 40, 60 y 100 %, en píxeles 1x. El 40 % también en Retina (2x).
- **Detalle a 200 %:** el bloque STEREO MODE y la cabecera
  (`*_modeblock_200.png`, `*_header_200.png`).
- **Hoja de legibilidad ampliada:** `legibility_40_60_100_zoomed.png`.

**Legibilidad y geometría:**

**Prefijo de la cabecera, antes y después** (`header_before_after.png`;
altura de mayúscula en píxeles de pantalla):

| Zoom | Mayúscula 9 (1ª entrega) | **Mayúscula 11 (actual)** | Valoración |
|---|---|---|---|
| 40 % en 1x | 3,6 px, apenas legible | **4,4 px**, legible | Mejora clara |
| 40 % en Retina | 7,2 px | **8,8 px** | Legible en los dos casos |
| 60 % en 1x | 5,4 px | **6,6 px** | Mejora |
| 100 % en 1x | 9 px | **11 px** | Claro en los dos casos |

- **El cambio afecta solo a una constante** (`bankPrefixCapHeight`), usada
  únicamente por `BandHeader`.
- **Sin cambios:** el color, la posición de inicio, la separación y "BAND n".
- **Ancho:** el texto más largo pasa de x 292 a x 299, frente a los 346 de
  "Filter Type".

**Botones STEREO, M/S, MID y SIDE:**
- Claros al 100 y al 60 %.
- Al 40 % en 1x, MID y SIDE se leen igual que STEREO y M/S, que ya eran así
  en el diseño.
- Al 40 % siguen siendo utilizables: 18,8 × 17,2 pt, con los clics
  verificados en el centro y junto a los bordes.

---

## 5. Compatibilidad estricta de STEREO

`tests/compat/run_compat.sh all` contra **14 bases** (E0 … UI-2c, E4a
`c9ea214` y la nueva base E4b `d27b9b1`), en arm64 y x86_64, todas OK
(`build/e4c-report/compat-all.txt`).

- **E0 y E1b:** solo las diferencias ya documentadas (0 dB nominal y límite
  de Q).
- **Las otras 12 bases:** **sin ninguna diferencia**, incluidas UI-2c, E4a y
  E4b.
- **Casos de dinámica y casos v5 en Stereo con MID/SIDE configurados y
  automatizados:** 72 de 72 idénticos bit a bit.
- **Base E4b:** el arnés de compatibilidad compila también la interfaz, y la
  base E4b añade comparar contra el motor aprobado.

---

## 6. Riesgos pendientes

1. **D3:** legibilidad del prefijo de la cabecera al 40 % en pantallas 1x
   (§4).
2. **Undo de las copias en Logic:** sin comprobar. No es atómico.
3. **Hasta E4d,** en M/S no hay visualización dinámica y el analizador
   muestra L/R, lo que la interfaz indica.
4. **Smoke físico en Logic de E4b y E4c:** pendiente de autorización.
   Revisar en él la escritura de automatización de `proc_mode` desde los
   botones y el comportamiento de una copia con automatización en lectura.
