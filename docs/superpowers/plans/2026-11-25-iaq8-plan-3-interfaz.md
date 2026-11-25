# IAQ8 · Plan 3: interfaz — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sustituir el editor provisional (`DebugEditor`) por la interfaz
aprobada: chasis de rack gastado de LABQ8, pantalla de fósforo verde con la
conversación y la curva, 8 teclas de respuestas rápidas, fila superior
(SOURCE, COMPARE, STATUS), panel ASSISTANT con línea de comando, casillas de
valores y teclas, y medidores IN/OUT reales. Todo ligado a la `Session` del
Plan 1.

**Architecture:** Mismo patrón que LABQ8 (`src/ui/LabQ8Editor`):
- Un componente de contenido en coordenadas de referencia (1672 × 941),
  escalado con `AffineTransform` según el zoom elegido.
- El fondo es la imagen del chasis, reescalada y cacheada.
- Encima, componentes propios: pantalla CRT, teclas y casillas.

Se reutiliza sin modificar todo lo de LABQ8 que ya encaja, porque el chasis
de IAQ8 se deriva del mismo fondo y conserva su geometría:
- `LevelMeterView` y la geometría de los medidores (`src/ui/UiGeometry.h`);
- `LevelMeter` (`src/plugin/LevelMeter.h`);
- los PNG `meter_lit`, `mode_face_lit` y `mode_face_unlit`.

**Tech Stack:** C++20, JUCE 9.0.3 (`juce_gui_basics`, `juce_graphics`),
fuentes OFL embebidas como BinaryData, el arnés de pruebas de LABQ8 y una
herramienta de instantáneas sin pantalla (patrón de `tools/ui/UiSnapshot.cpp`).

**Spec:** `docs/superpowers/specs/2026-11-24-iaq8-design.md` §5 (flujo, pantalla,
idioma, sin conexión). **Maqueta aprobada:** `docs/superpowers/specs/iaq8-mockup-v2/`
(`index.html` con las coordenadas en px de 1672 × 941, `iaq8_chassis.png` y
`make_chassis.py`).

## Global Constraints

- **LABQ8 no se toca:** solo cambian `iaq8/` y `docs/superpowers/`. Los
  archivos de LABQ8 (`src/ui/LevelMeterView.cpp`, `src/ui/UiGeometry.h`,
  `src/plugin/LevelMeter.h`, `assets/ui/*.png`) se reutilizan compilándolos o
  incluyéndolos desde `iaq8/CMakeLists.txt`, nunca editándolos.
- **Espacio de referencia:** 1672 × 941 px. Las posiciones se copian de la
  maqueta aprobada (`iaq8-mockup-v2/index.html`, estilos `left/top/width/height`
  en px).
- **Zoom:** las mismas seis opciones que LABQ8 (0,4 / 0,6 / 0,8 / 1,0 / 1,2 /
  1,4; por defecto 0,6), en un archivo de preferencias **propio de IAQ8**
  (`VitaPlug/IAQ8/IAQ8.ui`). No se comparte con el de LABQ8.
- **Aspecto:** fiel a la maqueta aprobada:
  - pantalla de fósforo verde con texto VT323 (o equivalente de matriz),
    brillo, líneas de barrido, viñeta y reflejo;
  - rótulos serigrafiados en inglés con desgaste;
  - conversación, teclas de acción y avisos en español;
  - APLICAR es la única tecla naranja.
- **Fuentes:** VT323, Barlow y Barlow Semi Condensed, todas con licencia SIL
  OFL 1.1. Se embeben con su `OFL.txt` en `iaq8/assets/fonts/`. Descargarlas
  requiere el **permiso explícito del autor** en el chat. Sin permiso, se
  usan fuentes del sistema (Menlo para la pantalla y Helvetica Neue para los
  rótulos) y queda anotado.
- **Textos con tildes:** siempre con
  `juce::String (juce::CharPointer_UTF8 ("..."))`.
- **Hilo de mensajes:** el editor nunca bloquea, nunca hace red y nunca lee la
  captura. Toda la información sale de `Session` y del procesador, en su API
  de hilo de mensajes.
- **Pintado:** las capas estáticas (fondo, efectos del CRT, rótulos) se
  cachean en imágenes. Solo se repinta lo que cambia; el cursor parpadea a
  2 Hz y se repinta como un rectángulo pequeño. Con el plugin en reposo no hay
  repintado continuo.
- **Teclado:** `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` en IAQ8, porque hay entrada
  de texto. Enter envía.
- **Sin cambios de DSP, parámetros ni estado.**

## Review Focus

1. **Escalado:** en cada uno de los seis zooms, el texto de la pantalla sigue
   legible y no se sale de sus marcos, y los clics caen en la tecla que se
   ve. Prueba en la Tarea 2 (cajas de clic escaladas) y en la Tarea 8
   (instantáneas a 0,4 y 1,4).
2. **Conversación larga:** con 200 mensajes, la pantalla muestra los últimos
   y deja desplazarse hacia arriba sin bloquear el hilo. Prueba en la
   Tarea 4.
3. **Teclas que no aplican:** APLICAR desactivada sin propuesta, DESHACER sin
   historial y las respuestas rápidas vacías no hacen nada. Pruebas en las
   Tareas 5 y 6.
4. **El editor se cierra y se reabre en mitad de una petición o de una
   escucha:** no hay fallo y el estado se recupera de `Session`. Prueba en la
   Tarea 8.
5. **Pista mono:** los medidores muestran el mismo canal en L y R, como en
   LABQ8. Prueba en la Tarea 3.

---

## File Structure

```
iaq8/
  assets/ui/iaq8_chassis.png        generado con make_chassis.py desde ../assets/ui/background.png (comprometido)
  assets/fonts/{VT323-Regular.ttf, Barlow-Medium.ttf, Barlow-SemiBold.ttf, BarlowSemiCondensed-SemiBold.ttf, OFL.txt}
  src/ui/Iaq8Geometry.h             rectángulos de referencia (copiados de la maqueta)
  src/ui/Iaq8Preferences.{h,cpp}    zoom propio de IAQ8
  src/ui/Iaq8LookAndFeel.{h,cpp}    fuentes, colores, dibujo de teclas/desplegable/serigrafía gastada
  src/ui/PhosphorScreen.{h,cpp}     pantalla CRT: barra de estado, curva, conversación, efectos
  src/ui/RackKey.{h,cpp}            tecla de rack con LED (normal / naranja / desactivada)
  src/ui/Iaq8Editor.{h,cpp}         editor: compone todo y lo liga a Session y al procesador
  tools/Iaq8SnapshotMain.cpp        iaq8_ui_snapshot: PNG de estados de la interfaz
  tests/EditorUiTests.cpp
```

`iaq8/CMakeLists.txt` crea `juce_add_binary_data(iaq8_ui_assets HEADER_NAME LabQ8Assets.h NAMESPACE LabQ8Assets …)`
con `../assets/ui/meter_lit.png`, `mode_face_lit.png` y `mode_face_unlit.png`,
más el chasis y las fuentes de IAQ8. Se usa el mismo encabezado y el mismo
espacio de nombres que LABQ8 para que `../src/ui/LevelMeterView.cpp` compile
sin cambios; hay que comprobar en ese archivo qué símbolos usa. Además,
añade `../src/ui/LevelMeterView.cpp` a los objetivos del plugin y de las
pruebas.

---

### Task 1: Recursos: chasis y fuentes

**Files:**
- Create: `iaq8/assets/ui/iaq8_chassis.png`, `iaq8/assets/fonts/*` + `OFL.txt`, `iaq8/assets/README.md` (origen, licencias, cómo regenerar el chasis)
- Modify: `iaq8/CMakeLists.txt` (binary data `iaq8_ui_assets`)

- [ ] **Step 1:** Generar el chasis a resolución completa con
  `docs/superpowers/specs/iaq8-mockup-v2/make_chassis.py`. Necesita numpy y
  Pillow en un venv del scratchpad. Se ejecuta con
  `python make_chassis.py assets/ui/background.png iaq8/assets/ui/iaq8_chassis.png`
  desde la raíz del repo. Comprobar que el resultado mide 1672 × 941 y tiene
  el logotipo "IAQ8".
- [ ] **Step 2: Fuentes.** **Pedir permiso al autor antes de descargar**
  (controlador). Con permiso: VT323 Regular, Barlow Medium y SemiBold, y
  Barlow Semi Condensed SemiBold, del repositorio oficial de Google Fonts en
  GitHub (`google/fonts`, carpeta `ofl/`), con su `OFL.txt`. Sin permiso,
  saltar este paso y anotarlo; Iaq8LookAndFeel usará fuentes del sistema.
- [ ] **Step 3: Prueba que falla** (`EditorUiTests.cpp`):
  `"ui assets: chassis and LABQ8 sprites load at reference size"`. Carga
  `LabQ8Assets::iaq8_chassis_png` (1672 × 941), `meter_lit_png` y
  `mode_face_lit_png`, todos válidos. Si hay fuentes, `"ui assets: fonts load"`
  comprueba que `Typeface::createSystemTypefaceFor` devuelve un tipo válido
  para cada una.
- [ ] **Step 4:** Añadir el target de binary data y enlazarlo en el plugin y
  en las pruebas. Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P3.1: chassis and font assets"`.

### Task 2: Estructura del editor, zoom y geometría

**Files:**
- Create: `iaq8/src/ui/Iaq8Geometry.h`, `iaq8/src/ui/Iaq8Preferences.{h,cpp}`, `iaq8/src/ui/Iaq8LookAndFeel.{h,cpp}`, `iaq8/src/ui/Iaq8Editor.{h,cpp}`
- Modify: `iaq8/src/plugin/Iaq8Processor.cpp` (`createEditor` devuelve `Iaq8Editor`), `iaq8/CMakeLists.txt` (`EDITOR_WANTS_KEYBOARD_FOCUS TRUE`)

**Interfaces:**
- `namespace iaq8::ui::geometry`, con `referenceWidth = 1672` y
  `referenceHeight = 941`. Rectángulos (`juce::Rectangle<float>`) copiados de
  `iaq8-mockup-v2/index.html`:
  - fila superior: `sourceLabel`, `sourceTray`, `sourceDrop`,
    `compareLabel`, `compareTray`, `faceAntes`, `faceDespues`,
    `statusLabel`, `lamps[3]`, `lampCaps[3]`;
  - pantalla: `crt` (346, 193, 980, 314);
  - teclas rápidas: `quickKeys[8]` (x = 365 + i·120,4, y = 540, 99 × 40);
  - panel inferior: `assistantHead`, `assistantRule`, `inbox`, `hint`,
    `valueLabels[4]`, `valueBoxes[4]`, `keyListen`, `keySend`, `keyUndo`,
    `keyApply`;
  - texto bajo el logotipo: `desc1`, `desc2`, `desc3`.

  Los medidores usan `labq8::ui::geometry::meterColumns`.
- `namespace iaq8::ui::preferences { constexpr std::array<float,6> zoomChoices {0.4f,0.6f,0.8f,1.0f,1.2f,1.4f}; constexpr float defaultZoom = 0.6f; float loadZoom(); void saveZoom(float); }`
  usa `PropertiesFile` en `VitaPlug/IAQ8/IAQ8.ui` y tiene un gancho de
  pruebas para la carpeta, como `assist::Settings`.
- `class Iaq8Editor final : public juce::AudioProcessorEditor`:
  `explicit Iaq8Editor (Iaq8AudioProcessor&)`, `bool setZoom (float)`,
  `float getScale() const`, `juce::PopupMenu createSettingsMenu() const` y
  `void handleSettingsMenuResult (int)`.
  - El menú de ajustes se abre al hacer clic en el tornillo superior
    izquierdo del panel o en el logotipo, y tiene tres partes: zoom; "Código
    de acceso…", que abre un `AlertWindow` con el campo enmascarado y
    `Settings::setAccessCode`; y "Acerca de IAQ8".
  - El fondo es `iaq8_chassis_png` reescalado y cacheado, como en LABQ8.
  - El contenido de referencia se escala con `AffineTransform`.
  - Los rótulos de la fila superior y el texto bajo el logotipo se pintan
    con serigrafía gastada: color #dcd8c8 con una máscara de ruido fija,
    generada una vez con semilla determinista.

- [ ] **Step 1: Pruebas que fallan:**
  - `"editor: constructs at every zoom and maps reference rects to scaled bounds"`:
    para cada zoom, `getScale()` coincide y la caja escalada de
    `quickKeys[0]` es `quickKeys[0] × zoom` (±1 px). Las seis caben en una
    pantalla grande simulada, y la de 0,4 siempre cabe.
  - `"editor: zoom preference is IAQ8's own"`: guardar 1,2 en un directorio
    temporal hace que se cargue 1,2, y el archivo de LABQ8 no se toca.
  - `"editor: settings menu has zoom, access code and about"`.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar. `DebugEditor` sigue en el repositorio hasta la
  Tarea 8, pero sin usarse.
- [ ] **Step 4:** Ejecutar la suite completa. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P3.2: editor shell, zoom and geometry"`.

### Task 3: Medidores IN/OUT

**Files:**
- Modify: `iaq8/src/plugin/Iaq8Processor.{h,cpp}` (dos `labq8::LevelMeter`, entrada antes de la EQ y salida después; `getInputMeter()`, `getOutputMeter()`), `iaq8/src/ui/Iaq8Editor.cpp` (añade `labq8::LevelMeterView`), `iaq8/CMakeLists.txt` (compila `../src/ui/LevelMeterView.cpp`)

- [ ] **Step 1: Pruebas que fallan:**
  - `"meters: processor measures input before and output after the EQ"`:
    con un seno a −6 dBFS y una campana de −12 dB a esa frecuencia aplicada,
    la entrada da ~−6 dB y la salida ~−18 dB (±1 dB).
  - `"meters: processBlock still does not allocate"`: la prueba de
    asignaciones existente sigue pasando.
  - `"meters: mono shows the same channel in both columns"` (Review
    Focus 5), como en LABQ8.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar siguiendo el uso de `LevelMeter` en
  `src/plugin/PluginProcessor.cpp` de LABQ8: `measure` en `processBlock` y
  también en `processBlockBypassed`.
- [ ] **Step 4:** Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P3.3: real IN/OUT meters"`.

### Task 4: Pantalla de fósforo (`PhosphorScreen`)

**Files:**
- Create: `iaq8/src/ui/PhosphorScreen.{h,cpp}`
- Modify: `iaq8/src/ui/Iaq8Editor.cpp`

**Interfaces:**
- `class PhosphorScreen final : public juce::Component`:
  - `struct Line { enum class Who { assistant, user, system } who; juce::String text; };`
  - `void setStatus (juce::String left, juce::String middle, juce::String right);`
  - `void setLines (std::vector<Line>);`
  - `void setCurves (const dsp::BandSet& applied, std::optional<dsp::BandSet> preview);`
  - `void setListening (double progress0to1);`, que se muestra como una barra
    de bloques "ESCUCHANDO ▮▮▮▯▯".
  - `void setDetailVisible (bool)`: con la vista de detalle activa, la pantalla
    sustituye la curva y la conversación por una tabla en solo lectura de
    las bandas activas (tipo, frecuencia, ganancia y Q, en palabras
    sencillas).
  - `void scrollToEnd();`
- **Dibujo** (como en la maqueta):
  - **Fondo:** degradado radial `#0d2611` → `#010502`.
  - **Barra de estado:** VT323 19 px, `#5fbf55`, con una línea discontinua
    debajo.
  - **Curva:** 920 × 112 px de 20 Hz a 20 kHz en escala log, ±12 dB. Lleva una
    rejilla punteada, la curva aplicada en verde tenue y la de la
    previsualización en verde brillante con brillo, más un punto y una
    etiqueta "−4 DB" en cada banda activa. La respuesta se calcula con
    `labq8::dsp::designFilter` y la magnitud de cada biquad a 48 kHz.
  - **Conversación:** VT323 24 px, prefijo `IAQ8>` para el asistente y `TÚ>`
    para el usuario (en `#6fd062`), con ajuste de línea por palabras.
    Muestra las últimas líneas que caben y la rueda del ratón desplaza.
  - **Cursor:** bloque parpadeante; lo dibuja `Iaq8Editor` en la línea de
    comando, no en esta pantalla.
  - **Efectos en una capa cacheada que se regenera solo al cambiar el
    tamaño:** líneas de barrido (1 px de cada 3, negro al 38 %), banda de
    barrido tenue, grano (ruido fijo), reflejo diagonal y viñeta.
  - **Brillo del texto y la curva:** se dibuja el contenido en una imagen y
    se compone con una copia desenfocada (`juce::ImageConvolutionKernel`,
    radio ~3) en modo aditivo. Se cachea mientras el contenido no cambie.

- [ ] **Step 1: Pruebas que fallan:**
  - `"screen: wraps and keeps the last lines visible"`: con 200 mensajes
    (Review Focus 2), el último es visible y desplazar hacia arriba muestra
    los anteriores.
  - `"screen: no repaint at rest"`: tras `setLines`, cien eventos del
    temporizador sin cambios piden 0 repintados (cuenta de repintados
    expuesta para pruebas, como `LevelMeterView::getRepaintRequests`).
  - `"screen: curve marks active bands only"`: con dos bandas activas hay
    dos marcadores.
  - `"screen: detail view lists active bands"`.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P3.4: phosphor screen"`.

### Task 5: Teclas, panel ASSISTANT y línea de comando

**Files:**
- Create: `iaq8/src/ui/RackKey.{h,cpp}`
- Modify: `iaq8/src/ui/Iaq8Editor.cpp`, `iaq8/src/ui/Iaq8LookAndFeel.cpp`

**Interfaces:**
- `class RackKey final : public juce::Button`, que pinta una tecla con LED:
  - `enum class Style { dark, apply, slot };`
  - `slot` deja ver la tecla del fondo y solo pinta el texto y el LED.
  - `void setLed (bool);`
- **Siete teclas rápidas** sobre `quickKeys[0..6]`: texto de las 7 primeras de
  `Session::getQuickReplies()` en dos líneas (Barlow Semi Condensed, 12,5 px).
  - Al pulsarlas llaman a `Session::say(text)`.
  - Una tecla sin texto está desactivada y no hace nada (Review Focus 3).
  - La octava (`quickKeys[7]`) es siempre **VER DETALLE**: alterna
    `PhosphorScreen::setDetailVisible` y solo está activa cuando hay
    propuesta o algo aplicado.
- **Panel inferior:**
  - Cabecera: piloto naranja + "ASSISTANT" + "Pídelo con tus palabras" + raya.
  - Línea de comando: `juce::TextEditor` sin borde, con VT323 29 px,
    `#a9ff92`, fondo de fósforo, prefijo `> `, longitud máxima 4000 y Enter
    para enviar.
  - Aviso: "El audio nunca sale de tu ordenador · solo se envía un resumen
    del análisis".
  - Casillas SOURCE, LISTENED, CHANGES y LEVEL MATCH, con estos valores:
    fuente elegida; segundos escuchados; número de bandas activas de la
    propuesta o de lo aplicado; `getCompensationDb()` con signo y "dB".
  - Teclas ESCUCHAR (`listenAgain`, LED encendido mientras escucha), ENVIAR
    (envía el texto), DESHACER (`undo`, desactivada si `!canUndo()`) y
    APLICAR (`apply`, naranja, desactivada si `!canApply()`).
- **Avisos de `Session::getNotice()`:** van como línea de sistema en la
  pantalla.

- [ ] **Step 1: Pruebas que fallan**, con un `Session` sobre un transporte
  falso, como en `SessionTests`:
  - `"ui keys: quick replies follow the session and send their text"`.
  - `"ui keys: apply and undo enabled only when allowed"` (Review Focus 3).
  - `"ui input: enter sends and clears; 4000-char cap"`.
  - `"ui values: boxes show source, seconds, changes and compensation"`.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P3.5: rack keys, assistant panel and command line"`.

### Task 6: Fila superior: SOURCE, COMPARE y STATUS

**Files:**
- Modify: `iaq8/src/ui/Iaq8Editor.cpp`, `iaq8/src/ui/Iaq8LookAndFeel.cpp`

**Interfaces:**
- **SOURCE:** un `juce::ComboBox` con el estilo del desplegable de LABQ8
  (fondo `#232421`→`#171816`, borde `#3b3d38`, triángulo). Las opciones son
  las 7 fuentes en español (Voz, Guitarra acústica, Guitarra eléctrica,
  Bajo, Batería, Mezcla, Otro). Elegir una llama a `Session::chooseSource`;
  con "Otro" se pide una nota en la línea de comando. Muestra la fuente
  actual de la sesión.
- **COMPARE:** dos caras, `mode_face_unlit` y `mode_face_lit`, rotuladas
  ANTES y DESPUÉS. Al pulsar llaman a `Session::setListen` (applied o
  preview). La cara iluminada es la que suena. Sin propuesta, DESPUÉS está
  desactivada.
- **STATUS:** tres pilotos:
  - ONLINE: verde si la última petición tuvo respuesta; apagado en
    `offline` o al empezar.
  - LISTEN: verde mientras escucha.
  - PROPOSAL: naranja si hay propuesta.

- [ ] **Step 1: Pruebas que fallan:**
  - `"ui top: source combo drives the session"`.
  - `"ui top: compare faces follow and set listen; after disabled without proposal"`.
  - `"ui top: lamps follow stage, listening and proposal"`.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P3.6: source, compare and status row"`.

### Task 7: Vínculo con la sesión y refresco

**Files:**
- Modify: `iaq8/src/ui/Iaq8Editor.{h,cpp}`

**Interfaces:**
- `Iaq8Editor` es `Session::Listener`. En `sessionChanged()` actualiza
  pantalla, teclas, casillas y pilotos a partir de `Session` y del
  procesador (`getAppliedBands`, propuesta actual y `getCompensationDb`).
- Un temporizador a 15 Hz solo mientras hay escucha o petición en curso,
  para el progreso y el cursor. Se para en reposo.
- La barra de estado del CRT muestra la versión ("IAQ8 1.0"), "FUENTE: …",
  "ESCUCHADO: N S" y "PROPUESTA n · DESPUÉS/ANTES".

- [ ] **Step 1: Pruebas que fallan:**
  - `"ui session: full flow updates screen, keys and lamps"`, con transporte
    falso: elegir fuente, escucha simulada, objetivo, propuesta, comparar y
    aplicar. Comprueba los textos clave en la pantalla y los estados de las
    teclas.
  - `"ui session: editor destroyed mid-request and reopened recovers state"`
    (Review Focus 4): la respuesta llega sin editor, y al recrearlo la
    pantalla muestra la propuesta.
  - `"ui session: timer stops at rest"`.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P3.7: session binding and refresh"`.

### Task 8: Instantáneas, retirada del editor provisional y prueba en Logic

**Files:**
- Create: `iaq8/tools/Iaq8SnapshotMain.cpp` (target `iaq8_ui_snapshot`)
- Delete: `iaq8/src/plugin/DebugEditor.{h,cpp}` y sus pruebas; la sonda de
  red de Plan 1 se conserva solo como función de prueba si alguna prueba la
  usa
- Modify: `iaq8/docs/` (`UI.md`: cómo ejecutar las instantáneas, la lista de
  diferencias con la maqueta y el resultado en Logic)

- [ ] **Step 1:** `iaq8_ui_snapshot <dir>` renderiza el editor sin pantalla
  y guarda PNG en estos estados, con `Session` y transporte falsos:
  - `1_inicio` (pregunta de la fuente);
  - `2_escuchando`;
  - `3_propuesta` (curva, explicación y teclas);
  - `4_detalle`;
  - `5_sin_conexion`;
  - `6_zoom_040` y `7_zoom_140` (Review Focus 1).
- [ ] **Step 2:** Generar las instantáneas y compararlas a ojo con la
  maqueta aprobada. Anotar las diferencias en `UI.md` y corregir las
  evidentes (posiciones o tamaños de texto).
- [ ] **Step 3:** Retirar `DebugEditor` y ejecutar la suite completa.
  Esperado: PASS, sin avisos.
- [ ] **Step 4:** Manual (autor): compilar, instalar y probar en Logic con el
  lanzador local (`Abrir IAQ8 local.command` del servidor). Recorrer el flujo
  completo a 0,6 y a 1,0, escribir con el teclado en la línea de comando
  (que Logic no se coma las teclas) y comprobar el cierre y la reapertura de
  la ventana. Anotarlo en `UI.md`.
- [ ] **Step 5: Commit** `"IAQ8 P3.8: snapshots, retire debug editor"`.

---

## Al terminar el Plan 3

- Suite de IAQ8 en verde, sin avisos.
- Instantáneas generadas y revisadas contra la maqueta.
- Comprobar con
  `git diff --stat 17c9c32 -- src tests CMakeLists.txt assets docs/ROADMAP.md`
  que LABQ8 sigue sin cambios.
- Pendiente del autor: la prueba en Logic (Tarea 8, Step 4) y la valoración
  visual final.
