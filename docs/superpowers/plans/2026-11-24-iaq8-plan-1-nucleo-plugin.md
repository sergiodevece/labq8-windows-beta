# IAQ8 · Plan 1: núcleo del plugin — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Plugin IAQ8 (AU + VST3) que suena con el motor de LABQ8, previsualiza
propuestas con antes/después a volumen igualado, aplica y deshace, captura y
analiza la pista, y habla con el servidor mediante un contrato fijo. La
interfaz es provisional (de depuración); la definitiva es el Plan 3.

**Architecture:** Proyecto CMake propio en `iaq8/` que compila las fuentes de
`../src/dsp` sin modificarlas y reutiliza JUCE de `../external/JUCE`. Hay
tres capas:
- **Audio** (`iaq8/src/dsp`, sin JUCE): dos `EqCore` (aplicado y
  previsualización), fundido A/B e igualación de volumen.
- **Análisis** (`iaq8/src/analysis`): captura sin bloqueos y análisis en un
  hilo de fondo que produce un resumen numérico.
- **Asistente** (`iaq8/src/assist`): contrato JSON, cliente HTTPS con
  transporte inyectable, límites de seguridad y sesión de conversación.

**Tech Stack:** C++20, JUCE 9.0.3 (`juce_audio_processors`, `juce_dsp`,
`juce_core` para URL/JSON), CMake ≥ 3.22, el arnés de pruebas de LABQ8
(`tests/TestHarness.h`, `LQ_TEST`/`LQ_CHECK`).

**Spec:** `docs/superpowers/specs/2026-11-24-iaq8-design.md`

**Serie de planes:** 1 núcleo del plugin (este) · 2 servidor, prompt y batería
de casos (consume el contrato y la herramienta `iaq8_analyze` de este plan) ·
3 interfaz según la maqueta `docs/superpowers/specs/iaq8-mockup-v2/`.

## Global Constraints

- **LABQ8 no se toca:** ningún cambio fuera de `iaq8/` y `docs/superpowers/`.
  `src/`, `tests/`, `CMakeLists.txt` raíz, `assets/` y `docs/ROADMAP.md` quedan
  intactos. Se reutilizan por inclusión o compilación, nunca por edición.
- Rama `dev/iaq8-prototype`, creada desde `17c9c32` (E4d).
- C++20; JUCE 9.0.3 de `../external/JUCE`; macOS 11.0+; AU y VST3, arm64 y
  x86_64.
- `PRODUCT_NAME "IAQ8"`, `PLUGIN_CODE Iaq8`, `PLUGIN_MANUFACTURER_CODE Vtpl`,
  `BUNDLE_ID com.vitaplug.iaq8`, `COMPANY_NAME "VitaPlug"` y versión `0.1.0`.
- **Hilo de audio:** sin asignaciones, sin bloqueos y sin red. Toda petición
  HTTPS va en un hilo de fondo.
- **Nunca se envía audio:** solo texto, el resumen del análisis y los ajustes.
- **La clave de Anthropic no existe en el plugin.** El plugin solo conoce la
  URL del servidor y el código de acceso del usuario.
- **Límites de seguridad:** como mucho 5 bandas activas, ganancia de −12 a
  +6 dB, Q de campana de 0,5 a 8 y compensación de volumen de ±6 dB.
- **Captura:** objetivo de 12 s de señal útil, mínimo 6 s para analizar y
  tope de 20 s.
- **Idioma:** la conversación, las teclas de acción y los avisos van en
  español.
- **Expuestos al host:** solo los 40 parámetros estáticos del banco STEREO de
  LABQ8, con los mismos IDs y rangos. La dinámica queda apagada y el modo es
  Stereo.

## Review Focus

1. **El host vuelve a llamar a `prepareToPlay`** (cambio de frecuencia o de
   bloque) con una propuesta activa: aplicado y previsualización conservan
   sus ajustes y la captura se reinicia. Prueba en la Tarea 5.
2. **Se guarda y se reabre la sesión con una propuesta sin aplicar:** al
   restaurar se recupera exactamente lo aplicado, y la propuesta no se aplica
   sola. Prueba en la Tarea 5.
3. **Pista mono (1 entrada, 1 salida):** procesa, captura y analiza (con
   `is_mono` verdadero). Pruebas en las Tareas 5 y 7.
4. **La respuesta del servidor llega con el editor cerrado o el plugin ya
   destruido:** no hay fallo ni acceso a memoria liberada. Prueba en la
   Tarea 9.
5. **El host automatiza un parámetro aplicado mientras se previsualiza:** la
   ruta aplicada sigue la automatización y Antes/Después compara contra lo
   aplicado en ese momento. Prueba en la Tarea 5.

---

## File Structure

```
iaq8/
  CMakeLists.txt                  proyecto propio: JUCE, labq8_dsp (fuentes de ../src/dsp), iaq8_core, IAQ8, iaq8_tests, iaq8_analyze
  src/parameters/Iaq8Parameters.{h,cpp}   layout de 40 parámetros (IDs/rangos de LABQ8)
  src/dsp/AssistEngine.{h,cpp}            aplicado + previsualización, fundido A/B, compensación
  src/dsp/LoudnessMatcher.{h,cpp}         sonoridad ponderada K de ambas rutas
  src/analysis/CaptureBuffer.h            captura sin bloqueos desde el hilo de audio
  src/analysis/TrackAnalyzer.{h,cpp}      espectro promedio, tendencia y candidatos
  src/analysis/AnalysisTuning.h           umbrales y constantes del análisis (un solo sitio)
  src/assist/Contract.{h,cpp}             tipos del contrato y su (de)serialización JSON
  src/assist/ProposalLimits.{h,cpp}       límites de seguridad por fuente
  src/assist/AssistClient.{h,cpp}         petición/respuesta con transporte inyectable
  src/assist/Session.{h,cpp}              flujo de conversación (hilo de mensajes)
  src/assist/Settings.{h,cpp}             URL del servidor y código de acceso (PropertiesFile)
  src/plugin/Iaq8Processor.{h,cpp}        AudioProcessor
  src/plugin/DebugEditor.{h,cpp}          editor provisional (se sustituye en el Plan 3)
  tools/AnalyzeMain.cpp                   iaq8_analyze <wav> <source> → JSON
  tests/*.cpp                             pruebas (TestMain y AllocationGuard de ../tests)
  tests/fixtures/contract/*.json          ejemplos del contrato (el Plan 2 los usa también)
  docs/NETWORK_SPIKE.md                   resultado de la prueba de red en hosts
```

Compilar y probar (todas las tareas):

```bash
cmake -S iaq8 -B build/iaq8-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/iaq8-debug -j8
ctest --test-dir build/iaq8-debug --output-on-failure
```

Un solo grupo de pruebas: `build/iaq8-debug/iaq8_tests_artefacts/Debug/iaq8_tests "<filtro>"`.

---

### Task 1: Esqueleto IAQ8 y prueba de red en el DAW

Es el primer riesgo de la spec (§9): comprobar que el plugin puede hacer
HTTPS desde Logic (AU, proceso aislado) y desde un host VST3.

**Files:**
- Create: `iaq8/CMakeLists.txt`, `iaq8/src/plugin/Iaq8Processor.{h,cpp}`, `iaq8/src/plugin/DebugEditor.{h,cpp}`, `iaq8/tests/SmokeTests.cpp`, `iaq8/docs/NETWORK_SPIKE.md`

**Interfaces:**
- Produces: target `labq8_dsp_for_iaq8` (biblioteca estática con las 8
  fuentes de `../src/dsp`, include `../src`) y target `iaq8_core` (biblioteca
  estática de `iaq8/src/**` sin `plugin/`, include `iaq8/src` y `../src`). Las
  tareas siguientes añaden sus fuentes a `iaq8_core`. Plugin `IAQ8`;
  `iaq8_tests` (`../tests/TestMain.cpp`, `../tests/AllocationGuard.cpp`,
  `iaq8/tests/*.cpp`, con include `../tests`). `class Iaq8AudioProcessor
  final : public juce::AudioProcessor` (de momento, paso directo).

- [ ] **Step 1:** Crear la rama `git switch -c dev/iaq8-prototype 17c9c32`.
- [ ] **Step 2: Prueba que falla.** En `SmokeTests.cpp`:
  `LQ_TEST ("iaq8 smoke: processor passes audio through")` instancia
  `Iaq8AudioProcessor`, `prepareToPlay (48000, 512)` y procesa ruido estéreo.
  Comprueba que la salida es igual a la entrada muestra a muestra.
- [ ] **Step 3:** Escribir `iaq8/CMakeLists.txt` siguiendo el raíz de LABQ8:
  in-source prohibido, `CMAKE_OSX_DEPLOYMENT_TARGET 11.0`,
  `add_subdirectory(../external/JUCE ...)`, los targets de "Produces",
  `juce_add_plugin(IAQ8 ...)` con los códigos de Global Constraints,
  `NEEDS_CURL FALSE`, `NEEDS_WEB_BROWSER FALSE`, `COPY_PLUGIN_AFTER_BUILD
  FALSE`, `add_test(NAME iaq8_tests ...)`. Ejecutar el test y ver el fallo de
  compilación o de prueba.
- [ ] **Step 4:** Implementar el procesador de paso directo (buses: mono→mono
  y estéreo→estéreo, como LABQ8). `DebugEditor`: un botón "Probar conexión"
  que lanza en un `juce::Thread` un GET a `https://api.anthropic.com/v1/models`
  sin clave (la respuesta esperada es 401: lo importante es que llegue un
  código HTTP) y muestra el código o el error en una etiqueta. La etiqueta se
  actualiza con `juce::MessageManager::callAsync` y un `SafePointer`.
- [ ] **Step 5:** Compilar y ejecutar `ctest`. Esperado: PASS.
- [ ] **Step 6: Prueba manual.**
  - `auval -v aufx Iaq8 Vtpl` → "AU VALIDATION SUCCEEDED".
  - Copiar a mano `IAQ8.component` a `~/Library/Audio/Plug-Ins/Components/`
    y probar el botón en Logic: tiene que mostrar 401.
  - Repetir con `IAQ8.vst3` en un host VST3 (por ejemplo, el AudioPluginHost
    de JUCE).
  - Apuntar host, versión y resultado en `iaq8/docs/NETWORK_SPIKE.md`. **Si
    Logic bloquea la red, parar y avisar al autor**: el resto del plan depende
    de esto.
- [ ] **Step 7: Commit:** `git add iaq8 && git commit -m "IAQ8 P1.1: skeleton project and DAW network spike"`

### Task 2: Parámetros de IAQ8 (40, idénticos a LABQ8 STEREO)

**Files:**
- Create: `iaq8/src/parameters/Iaq8Parameters.{h,cpp}`, `iaq8/tests/ParameterTests.cpp`
- Modify: `iaq8/CMakeLists.txt` (añadir a `iaq8_core`; `iaq8_tests` compila además `../src/parameters/ParameterLayout.cpp` solo para comparar)

**Interfaces:**
- Produces: `namespace iaq8::params { juce::AudioProcessorValueTreeState::ParameterLayout createLayout(); constexpr int stateVersion = 1; }`.
  IDs, por banda y en este orden: `labq8::params::bandIDs[b].{frequency, gain, q, bypass, type}`.

- [ ] **Step 1: Prueba que falla** `LQ_TEST ("iaq8 params: 40 parameters with LABQ8 STEREO ids, ranges and defaults")`.
  Construye un APVTS con `iaq8::params::createLayout()` y otro con
  `labq8::params::createParameterLayout()`. Comprueba:
  - IAQ8 tiene exactamente 40 parámetros.
  - Cada ID de IAQ8 existe en LABQ8 con el mismo nombre, rango
    (`getNormalisableRange()`: inicio, fin, intervalo, sesgo), valor por
    defecto y versión del ID.
  - IAQ8 no tiene ningún ID `*_dyn*`, `proc_mode`, `mid_*` ni `side_*`.
- [ ] **Step 2:** Ejecutar `iaq8_tests "iaq8 params"`. Esperado: FAIL (no compila).
- [ ] **Step 3:** Implementar `createLayout()`. Copia las cuatro fábricas
  (frecuencia, ganancia, Q, bypass y tipo) de
  `src/parameters/ParameterLayout.cpp`, que no se pueden reutilizar porque
  están en un espacio anónimo. Usa los mismos `labq8::params::bandIDs`,
  `defaultFrequencyHz`, `filterTypeNames`, `labq8::dsp::limits` y las mismas
  versiones de ID que LABQ8 (`versionHintE0`/`E1a`/`E1b`). Añade un comentario
  que diga que la prueba del Step 1 es la que garantiza la paridad.
- [ ] **Step 4:** Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P1.2: parameter layout mirroring LABQ8 STEREO bank"`.

### Task 3: AssistEngine — ruta aplicada, previsualización y fundido A/B

**Files:**
- Create: `iaq8/src/dsp/AssistEngine.{h,cpp}`, `iaq8/tests/AssistEngineTests.cpp`

**Interfaces:**
- Consumes: `labq8::dsp::EqCore`, `labq8::dsp::BandSettings`.
- Produces (`namespace iaq8::dsp`):
  ```cpp
  using BandSet = std::array<labq8::dsp::BandSettings, 8>;
  enum class Listen { applied, preview };
  class AssistEngine {
  public:
      static constexpr double fadeSeconds = 0.020;
      static constexpr int maxChannels = 2;
      void prepare (double sampleRate, int numChannels, int maxBlockSize); // allocates scratch
      void setApplied (const BandSet&) noexcept;   // audio thread
      void setPreview (const BandSet&) noexcept;   // audio thread
      void clearPreview() noexcept;                // audio thread; Listen::preview then plays applied
      void setListen (Listen) noexcept;            // audio thread; starts a fade if it changes
      void process (float* const* channels, int numChannels, int numSamples) noexcept;
      bool hasPreview() const noexcept;
      double getCompensationDb() const noexcept;   // Task 4; 0 until then
  };
  ```

- [ ] **Step 1: Pruebas que fallan:**
  - `"assist engine: applied path is bit-identical to a lone EqCore"`: con
    `Listen::applied` y una previsualización distinta, la salida es igual
    (`==`) a la de un `EqCore` con los mismos ajustes aplicados.
  - `"assist engine: preview path is bit-identical to a lone EqCore with the preview set"`:
    con `Listen::preview` estable, compensación forzada a 0 dB (en esta tarea
    siempre lo es).
  - `"assist engine: A/B switch fades linearly over 20 ms"`: aplicado plano y
    previsualización con −12 dB en una campana a 1 kHz, sobre un seno de
    1 kHz. Al cambiar a preview, la envolvente pasa de una a otra en
    `round(0.020*fs)` muestras ±16. Ningún salto entre muestras consecutivas
    supera 0,01.
  - `"assist engine: listen preview without a preview plays applied"`.
  - `"assist engine: process does not allocate"`: con
    `ScopedAllocationCounter`, el recuento es 0 en 100 bloques, cambiando de
    Listen cada 10.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar. Las dos rutas procesan siempre una copia de la
  entrada (búfer temporal reservado en `prepare`, nunca en `process`): así la
  ruta inactiva está caliente y la Tarea 4 puede medir las dos. El fundido es
  lineal y por muestra.
- [ ] **Step 4:** Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P1.3: applied/preview engine with 20 ms A/B fade"`.

### Task 4: Igualación de volumen

**Files:**
- Create: `iaq8/src/dsp/LoudnessMatcher.{h,cpp}`, `iaq8/tests/LoudnessTests.cpp`
- Modify: `iaq8/src/dsp/AssistEngine.{h,cpp}`

**Interfaces:**
- Produces:
  ```cpp
  class LoudnessMatcher {   // iaq8::dsp
  public:
      static constexpr double windowSeconds = 3.0, maxCompensationDb = 6.0, smoothingSeconds = 0.5;
      static constexpr double silenceDb = -70.0; // K-weighted mean square below this: hold
      void prepare (double sampleRate, int numChannels);
      void push (const float* const* applied, const float* const* preview, int numChannels, int numSamples) noexcept;
      double getCompensationDb() const noexcept; // gain for the preview path, clamped ±6
      void reset() noexcept;
  };
  ```
  `AssistEngine` aplica `getCompensationDb()` (suavizado) solo a la ruta de
  previsualización y lo expone en `AssistEngine::getCompensationDb()`.

- [ ] **Step 1: Pruebas que fallan:**
  - `"loudness: +3 dB bell at 1 kHz on noise is matched within 0.5 dB"`: tras
    5 s, la sonoridad ponderada K de la salida en preview queda a ±0,5 dB de
    la de applied.
  - `"loudness: compensation is clamped to ±6 dB"`: preview +12 dB en
    shelf ancho → −6,0.
  - `"loudness: silence holds the last compensation"`: tras ruido, 5 s de
    ceros → el valor no cambia más de 0,1 dB.
  - `"loudness: applied path is never compensated"`: con Listen::applied, la
    prueba de identidad de la Tarea 3 sigue pasando con una previsualización
    que suba el volumen.
  - Las pruebas de identidad de la Tarea 3 en preview se cambian para
    comprobar `salida == EqCore(preview) × 10^(comp/20)` con tolerancia de 1e-6.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar. La ponderación K es la de BS.1770 aproximada
  con `labq8::dsp::designHighShelf(fs, 1681.97, +3.9998, 0.7071)` seguido de
  `designLowCut(fs, 38.135, 0.5003)`, aplicados a cada ruta y canal. Media
  cuadrática deslizante de `windowSeconds` (acumulador exponencial
  equivalente). Compensación = `10·log10(msApplied/msPreview)`, recortada a
  ±6 dB y suavizada en `smoothingSeconds`.
- [ ] **Step 4:** Ejecutar. Esperado: PASS (también las de la Tarea 3).
- [ ] **Step 5: Commit** `"IAQ8 P1.4: K-weighted loudness match on the preview path"`.

### Task 5: Procesador — parámetros, propuesta, aplicar/deshacer y estado

**Files:**
- Modify: `iaq8/src/plugin/Iaq8Processor.{h,cpp}`
- Create: `iaq8/tests/ProcessorTests.cpp`

**Interfaces:**
- Consumes: `iaq8::params::createLayout`, `iaq8::dsp::AssistEngine`, `BandSet`, `Listen`.
- Produces (todo en el hilo de mensajes salvo lo indicado):
  ```cpp
  class Iaq8AudioProcessor {
  public:
      juce::AudioProcessorValueTreeState& getState();
      dsp::BandSet getAppliedBands() const;              // reads the 40 parameters
      void setProposal (const dsp::BandSet&);            // to the audio thread via a lock-free 2-slot handoff
      void clearProposal();
      bool hasProposal() const;
      void setListen (dsp::Listen);
      bool applyProposal();                              // writes the 40 params with begin/end gestures, pushes undo, clears proposal
      bool undoApply();                                  // restores previous applied set (history depth 16)
      int getUndoDepth() const;
      juce::ValueTree getAssistantState() const;         // child "ASSISTANT": source, messages, never the proposal
      void setAssistantState (const juce::ValueTree&);
  };
  ```
  En el estado, `getStateInformation` guarda el APVTS con
  `stateVersion = iaq8::params::stateVersion` y el hijo `ASSISTANT`.

- [ ] **Step 1: Pruebas que fallan:**
  - `"processor: parameters drive the applied path"`: poner `b1_gain`=−6 a
    1 kHz y comprobar la respuesta con un seno.
  - `"processor: a proposal is audible in preview and never touches parameters"`:
    un `juce::AudioProcessorParameter::Listener` en los 40 no recibe ningún
    evento durante `setProposal` ni `setListen`.
  - `"processor: apply writes all 40 parameters in one gesture each and clears the proposal"`:
    después de aplicar, `getAppliedBands()` es igual a la propuesta (dentro de
    la resolución del intervalo de cada parámetro), `hasProposal()` es falso y
    el listener recibió un begin/end por cada parámetro que cambió.
  - `"processor: undo restores the previous applied set"`: aplicar dos
    propuestas y deshacer dos veces devuelve el estado inicial.
    `undoApply()` sin historial → false.
  - `"processor: state round trip keeps applied set and conversation, drops pending proposal"`
    (Review Focus 2).
  - `"processor: prepareToPlay again keeps applied and preview"` (Review
    Focus 1): una propuesta activa, `prepareToPlay(96000, 256)`, y la salida
    en preview sigue mostrando la propuesta.
  - `"processor: automation on an applied parameter while previewing"`
    (Review Focus 5): con Listen::applied, mover `b2_gain` durante el
    procesado cambia la salida; con Listen::preview, la compensación se
    recalcula contra el nuevo aplicado.
  - `"processor: mono bus processes"` (Review Focus 3, parte de proceso).
  - `"processor: processBlock does not allocate"`.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar. `processBlock` lee los 40 atómicos y llama a
  `setApplied`. Recoge la propuesta pendiente del intercambio sin bloqueos
  (dos ranuras más un `std::atomic<int>` con el índice publicado y un número
  de generación) y llama a `setPreview`/`clearPreview`. Luego aplica
  `setListen` y `process`. El historial de deshacer vive en el hilo de
  mensajes (`std::deque<BandSet>`, máximo 16).
- [ ] **Step 4:** Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P1.5: processor with preview handoff, apply/undo and state"`.

### Task 6: Captura para el análisis

**Files:**
- Create: `iaq8/src/analysis/CaptureBuffer.h`, `iaq8/tests/CaptureTests.cpp`
- Modify: `iaq8/src/plugin/Iaq8Processor.{h,cpp}` (añade `analysis::CaptureBuffer& getCapture();`, prepara la captura en `prepareToPlay` y escribe la entrada antes de la EQ)

**Interfaces:**
- Produces (`iaq8::analysis`):
  ```cpp
  class CaptureBuffer {
  public:
      static constexpr double targetSeconds = 12.0, minSeconds = 6.0, maxSeconds = 20.0;
      static constexpr double signalFloorDbfs = -60.0;   // blocks below this RMS are not counted
      enum class State { idle, capturing, done };
      void prepare (double sampleRate, int numChannels); // allocates maxSeconds; resets to idle
      void arm() noexcept;                               // message thread: idle/done -> capturing (clears)
      void cancel() noexcept;
      void write (const float* const* ch, int numChannels, int numSamples) noexcept; // audio thread
      State getState() const noexcept;
      double getCapturedSeconds() const noexcept;        // useful signal only
      bool hasEnough() const noexcept;                   // >= minSeconds
      int copyOut (std::vector<std::vector<float>>& dest) const; // message/background thread, only when not capturing; returns samples per channel
      double getSampleRate() const noexcept;
      int getNumChannels() const noexcept;
  };
  ```

- [ ] **Step 1: Pruebas que fallan:**
  - `"capture: counts only blocks above -60 dBFS"`: 4 s de ruido, 4 s de
    silencio y 4 s de ruido dan `getCapturedSeconds()` ≈ 8 ±0,02.
  - `"capture: stops by itself at 12 s of signal"`: pasa a `done` y
    `copyOut` devuelve `12·fs` muestras ±1 bloque.
  - `"capture: write does not allocate"`.
  - `"capture: prepare while capturing resets to idle"` (Review Focus 1).
  - `"capture: mono bus captures one channel"` (Review Focus 3): el procesador
    con bus mono rellena la captura con `getNumChannels()==1`.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar. Solo escribe el hilo de audio mientras está en
  `capturing`; el estado y el contador son atómicos. Los bloques bajo el
  umbral no se guardan, para que el análisis no promedie silencio.
- [ ] **Step 4:** Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P1.6: lock-free capture of useful signal"`.

### Task 7: TrackAnalyzer → AnalysisSummary

**Files:**
- Create: `iaq8/src/analysis/TrackAnalyzer.{h,cpp}`, `iaq8/src/analysis/AnalysisTuning.h`, `iaq8/src/assist/Source.h`, `iaq8/tests/AnalyzerTests.cpp`

**Interfaces:**
- Consumes: muestras de `CaptureBuffer::copyOut`, `iaq8::assist::Source`, que esta tarea define en `assist/Source.h`: `enum class Source { voice, acousticGuitar, electricGuitar, bass, drums, mix, other };`.
- Produces (`iaq8::analysis`):
  ```cpp
  enum class CandidateKind { excess, deficit, resonance, lowRumble, sibilance };
  struct Candidate { std::string id; CandidateKind kind; double freqHz, magnitudeDb, widthOct; };
  struct AnalysisSummary {
      int version = 1; double sampleRate = 0, seconds = 0; int channels = 0;
      double rmsDbfs = -120, peakDbfs = -120, crestDb = 0;
      bool isMono = true; double sideToMidDb = -120, correlation = 1;
      static constexpr double spectrumStartHz = 20.0, spectrumStepOct = 1.0 / 6.0;
      std::vector<double> spectrumDb;        // 60 points: 20 Hz · 2^(i/6), i = 0..59
      double slopeDbPerOct = 0, fitFromHz = 0, fitToHz = 0;
      std::vector<Candidate> candidates;     // at most 8, by |magnitudeDb| desc; ids "c1".."c8"
      bool enoughSignal = false;
  };
  AnalysisSummary analyse (const std::vector<std::vector<float>>& channels, double sampleRate, assist::Source);
  ```

- [ ] **Step 1: Pruebas que fallan** (señales sintéticas de 10 s a 48 kHz):
  - `"analyzer: flat pink noise has no candidates and slope about -3 dB/oct"`
    (±0,5).
  - `"analyzer: +6 dB bell at 250 Hz Q1 on pink noise is an excess near 250 Hz"`:
    tipo `excess`, frecuencia de 250 Hz ±1/6 oct, magnitud de 3 a 7 dB.
  - `"analyzer: narrow +12 dB peak at 3150 Hz Q12 is a resonance"`:
    frecuencia ±1/12 oct, anchura < 1/6 oct.
  - `"analyzer: voice with energy below 80 Hz reports low_rumble"`: ruido
    rosa más un seno de 50 Hz a −12 dB respecto al RMS.
  - `"analyzer: voice with +8 dB 5-9 kHz reports sibilance"`.
  - `"analyzer: mono and stereo detection"` (Review Focus 3): L=R da
    `isMono`; ruidos independientes dan `correlation` < 0,2.
  - `"analyzer: silence or under 6 s is not enough signal"`:
    `enoughSignal=false` y sin candidatos.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar. Constantes en `AnalysisTuning.h`, con estos
  valores iniciales que se afinarán con material real:
  - Espectro promedio de Welch sobre la señal media `(L+R)/2`, con FFT de
    orden `fs≤50k?13:fs≤100k?14:15` (como `SpectrumAnalyzer::fftOrderFor`),
    ventana Hann y solapamiento del 50 %.
  - Suavizado a 1/6 oct para `spectrumDb`, a 1/3 oct para la tendencia y los
    excesos, y a 1/24 oct para las resonancias.
  - Tendencia: recta por mínimos cuadrados del 1/3 oct en dB frente a log2(f)
    entre los límites de la fuente: voz 100–12000, bajo 40–5000, mezcla
    30–16000 y el resto 60–14000.
  - Excesos: región contigua con 1/3 oct − tendencia > +3 dB y anchura ≥ 1/3
    oct. Déficits: < −4 dB y anchura ≥ 2/3 oct.
  - Resonancias: 1/24 oct − 1/3 oct > +6 dB y anchura < 1/6 oct.
  - `lowRumble`: energía por debajo del límite inferior de la fuente
    > −20 dB respecto al total (solo voz y guitarras).
  - `sibilance`: banda de 5–9 kHz − tendencia > +4 dB (solo voz).
  - Estéreo: `sideToMidDb` y correlación de Pearson L/R. `isMono` si
    `sideToMidDb` < −40.
- [ ] **Step 4:** Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P1.7: track analyzer with measured candidates"`.

### Task 8: JSON del análisis y herramienta `iaq8_analyze`

**Files:**
- Create: `iaq8/src/assist/Contract.{h,cpp}` (parte de análisis), `iaq8/tools/AnalyzeMain.cpp`, `iaq8/tests/AnalysisJsonTests.cpp`
- Modify: `iaq8/CMakeLists.txt` (`juce_add_console_app(iaq8_analyze ...)` con `juce_audio_formats`)

**Interfaces:**
- Produces: `juce::var toJson (const analysis::AnalysisSummary&)` con esta
  forma exacta (snake_case y valores redondeados a 0,1 dB / 1 Hz / 0,01):
  `{"version":1,"sample_rate":48000,"seconds":12.0,"channels":2,
  "level":{"rms_dbfs":..,"peak_dbfs":..,"crest_db":..},
  "stereo":{"is_mono":false,"side_to_mid_db":..,"correlation":..},
  "spectrum":{"start_hz":20,"step_oct":0.1667,"db":[60 números]},
  "trend":{"slope_db_per_oct":..,"fit_from_hz":..,"fit_to_hz":..},
  "candidates":[{"id":"c1","kind":"excess|deficit|resonance|low_rumble|sibilance","freq_hz":..,"magnitude_db":..,"width_oct":..}],
  "enough_signal":true}`.
  Herramienta: `iaq8_analyze <archivo.wav> <source>` escribe ese JSON en la
  salida estándar y termina con 0, o con 2 si el archivo o la fuente no son
  válidos. `source` admite `voice, acoustic_guitar, electric_guitar, bass,
  drums, mix, other`. Analiza el archivo entero (sin el tope de 20 s).

- [ ] **Step 1: Pruebas que fallan:**
  - `"analysis json: shape and rounding"`: comprueba las claves y tipos de
    arriba.
  - `"analysis json: compact"`: `JSON::toString(var, true)` ocupa menos de
    2500 bytes con 8 candidatos.
  - Guardar un ejemplo en `iaq8/tests/fixtures/contract/analysis_voice.json`
    generado desde la prueba del exceso a 250 Hz de la Tarea 7.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar `toJson` y `AnalyzeMain.cpp`: lee con
  `juce::AudioFormatManager` (formatos básicos) y llama a `analyse`.
- [ ] **Step 4:** Ejecutar las pruebas y, a mano,
  `build/iaq8-debug/iaq8_analyze_artefacts/Debug/iaq8_analyze tests/fixtures/<cualquier.wav> voice`
  (o un WAV generado). Esperado: JSON válido (`| python3 -m json.tool`).
- [ ] **Step 5: Commit** `"IAQ8 P1.8: analysis JSON and iaq8_analyze tool"`.

### Task 9: Contrato con el servidor, límites y cliente

**Files:**
- Modify: `iaq8/src/assist/Contract.{h,cpp}`
- Create: `iaq8/src/assist/ProposalLimits.{h,cpp}`, `iaq8/src/assist/AssistClient.{h,cpp}`, `iaq8/src/assist/Settings.{h,cpp}`, `iaq8/tests/ContractTests.cpp`, `iaq8/tests/LimitsTests.cpp`, `iaq8/tests/ClientTests.cpp`, `iaq8/tests/fixtures/contract/{request_first_turn,reply_question,reply_proposal,reply_error_401,reply_malformed}.json`

**Contrato (fijo; el Plan 2 lo implementa en el servidor):**
- `POST {server}/v1/turn`, con `Content-Type: application/json` y
  `Authorization: Bearer <código de acceso>`.
- Petición: `{"plugin_id":"iaq8","plugin_version":"0.1.0","session_id":"<uuid>",
  "source":"voice|acoustic_guitar|electric_guitar|bass|drums|mix|other","source_note":"<texto si other>",
  "messages":[{"role":"assistant|user","text":".."}],"analysis":<Task 8>|null,
  "applied":{"bands":[Band×8]},"proposal":{"bands":[Band×8]}|null}`.
- Band: `{"type":"bell|low_shelf|high_shelf|low_cut|high_cut|notch","freq_hz":..,"gain_db":..,"q":..,"bypass":bool,"candidate_id":"c1"|null,"reason":"<frase corta>"|null}`.
- Respuesta 200:
  `{"kind":"question","text":"..","quick_replies":["..",…≤8]}` o
  `{"kind":"proposal","explanation":"..","bands":[Band…≤8],"quick_replies":[…≤8]}`.
  Cada respuesta rápida tiene como mucho 18 caracteres.
- Errores: 401 → `no_access`; 429 → `limit_reached`; 422 → `invalid_request`;
  5xx, tiempo agotado (30 s) o error de red → `unavailable`; 200 con cuerpo no
  válido → `invalid_response`.

**Interfaces:**
- Produces (`iaq8::assist`):
  ```cpp
  // Source: assist/Source.h (Task 7)
  struct ProposedBand { labq8::dsp::BandSettings settings; std::string candidateId, reason; };
  struct Message { enum class Role { assistant, user } role; std::string text; };
  struct TurnRequest { std::string sessionId; Source source; std::string sourceNote;
                       std::vector<Message> messages; std::optional<analysis::AnalysisSummary> analysis;
                       dsp::BandSet applied; std::optional<std::vector<ProposedBand>> proposal; };
  struct Question { std::string text; std::vector<std::string> quickReplies; };
  struct Proposal { std::string explanation; std::vector<ProposedBand> bands; std::vector<std::string> quickReplies; };
  enum class ErrorKind { noAccess, limitReached, invalidRequest, unavailable, invalidResponse };
  struct Error { ErrorKind kind; std::string userMessage; };   // userMessage in Spanish, fixed per kind
  using Reply = std::variant<Question, Proposal, Error>;
  juce::String toJson (const TurnRequest&);
  Reply parseReply (int httpStatus, const juce::String& body);

  struct LimitReport { std::vector<std::string> adjustments; };
  dsp::BandSet sanitize (const Proposal&, Source, const analysis::AnalysisSummary*, LimitReport&); // unused slots: bypassed bell at LABQ8 defaults

  struct Transport { virtual ~Transport() = default;
      virtual void post (const juce::URL&, const juce::String& headers, const juce::String& body,
                         std::function<void (int status, juce::String body)> done) = 0; }; // done on a background thread
  std::unique_ptr<Transport> makeHttpsTransport (int timeoutMs = 30000);
  class AssistClient {   // owner-safe: replies after destruction are dropped
  public:
      AssistClient (std::unique_ptr<Transport>, std::function<juce::URL()> server, std::function<juce::String()> accessCode);
      void send (const TurnRequest&, std::function<void (Reply)> onReplyOnMessageThread);
      void cancelAll();
  };
  struct Settings { static juce::URL serverUrl(); static juce::String accessCode(); static void setAccessCode (const juce::String&); };
  ```
  Límites de `sanitize`:
  - Todas las fuentes: como mucho 5 bandas activas. Si hay más, se descartan
    las de menor |ganancia|; los cortes cuentan como activos.
  - Ganancia de −12 a +6 dB y Q de campana de 0,5 a 8.
  - Notch: Q hasta 18 solo si su `candidate_id` es una resonancia del
    análisis; si no, hasta 8.
  - Frecuencia de 20 a 20000 Hz.
  - Voz: sin realces por debajo de 80 Hz y low_cut ≤ 150 Hz.
  - Bajo: low_cut ≤ 50 Hz.
  - Mezcla: ganancia de −6 a +4 dB.
  - Cada ajuste añade una línea a `adjustments`.
- `Settings`: un `PropertiesFile` en la carpeta de usuario "VitaPlug/IAQ8".
  `serverUrl` vale por defecto la constante de CMake `IAQ8_DEFAULT_SERVER_URL`
  (el Plan 2 la fija; aquí, `https://example.invalid`) y se puede
  sobrescribir con la clave `server_url`. El código de acceso **nunca** va en
  el estado de la sesión del DAW.

- [ ] **Step 1: Pruebas que fallan:**
  - Contrato: `toJson(TurnRequest)` coincide con
    `request_first_turn.json` (comparando objetos JSON).
  - `parseReply` sobre cada fixture da el tipo esperado. 401 → noAccess,
    429 → limitReached, 503 → unavailable, malformado → invalidResponse.
    Una propuesta con 9 bandas se trunca a 8 al leerla.
  - `sanitize`: un caso por cada límite de la lista, con su línea en
    `adjustments`.
  - Cliente con un `Transport` falso: llega la respuesta en el hilo de
    mensajes. Si el `AssistClient` se destruye antes de que el transporte
    responda, el callback no se llama y no hay fallo (Review Focus 4; ejecutar
    también con TSan). La cabecera `Authorization` lleva el código de
    `accessCode()`.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar.
  - HTTPS con `juce::URL::withPOSTData` y `createInputStream`, usando
    `InputStreamOptions(ParameterHandling::inPostData)`,
    `.withExtraHeaders`, `.withConnectionTimeoutMs`, `.withStatusCode`, en un
    `juce::ThreadPool` de 1 hilo.
  - La vida útil se protege con un `std::shared_ptr<std::atomic<bool>> alive`
    que se captura en el callback.
- [ ] **Step 4:** Ejecutar. Esperado: PASS.
- [ ] **Step 5: Commit** `"IAQ8 P1.9: server contract, safety limits and HTTPS client"`.

### Task 10: Session (flujo de conversación) y editor de depuración

**Files:**
- Create: `iaq8/src/assist/Session.{h,cpp}`, `iaq8/tests/SessionTests.cpp`
- Modify: `iaq8/src/plugin/DebugEditor.{h,cpp}`, `iaq8/src/plugin/Iaq8Processor.{h,cpp}` (es dueño de `Session` y `AssistClient`)

**Interfaces:**
- Consumes: `Iaq8AudioProcessor` (setProposal, applyProposal, undoApply,
  setListen, getCapture, get/setAssistantState), `AssistClient`, `analyse`,
  `sanitize`.
- Produces (`iaq8::assist`, hilo de mensajes):
  ```cpp
  class Session {
  public:
      enum class Stage { askSource, listening, askGoal, waiting, proposal, applied, offline };
      struct Listener { virtual ~Listener() = default; virtual void sessionChanged() = 0; };
      void addListener (Listener*); void removeListener (Listener*);
      Stage getStage() const;
      const std::vector<Message>& getMessages() const;
      std::vector<std::string> getQuickReplies() const;  // ≤8, shown on the 8 keys
      double getListenProgress() const;                  // 0..1 of CaptureBuffer::targetSeconds
      void chooseSource (Source, const std::string& note = {});
      void say (const std::string& userText);            // free text or a quick reply
      void listenAgain();
      void apply(); void undo(); void setListen (dsp::Listen);
      void startOver();
  };
  ```
  Flujo de la spec §5.1. Las dos primeras preguntas son **locales**, sin
  llamar al servidor:
  - "¿Qué tienes en esta pista?", con las 7 fuentes como respuestas rápidas.
  - Tras la escucha: "¿Qué te gustaría mejorar?", con respuestas rápidas
    según la fuente. Voz: "Que se entienda", "Menos apagada", "Menos
    chillona", "Menos caja". Bajo: "Más definición", "Más fondo", "Menos
    retumbe". Mezcla: "Más brillo", "Más claridad", "Menos barro". El resto:
    "Más claridad", "Menos dureza", "Más cuerpo".

  La primera llamada al servidor se hace cuando el usuario dice qué quiere.
  Lleva el análisis solo en la primera petición después de cada escucha. Cada
  respuesta `Proposal` pasa por `sanitize`, va a
  `processor.setProposal` y pone Listen en `preview`. Un `Error`
  `unavailable` lleva a `offline`, con el mensaje "Sin conexión: tu
  ecualización sigue activa", y se puede reintentar con `say`. Los mensajes
  se guardan en `ASSISTANT` mediante `setAssistantState`.

- [ ] **Step 1: Pruebas que fallan**, con transporte falso y la captura
  rellenada por la prueba:
  - Recorrido completo: preguntas locales, escucha, petición con `analysis`
    y `messages` correctos, propuesta, preview con la propuesta y `apply` que
    deja `Stage::applied`.
  - Una corrección ("menos agudos") envía `proposal` con la propuesta actual
    y sin `analysis`.
  - Un error 503 lleva a `offline` sin tocar lo aplicado.
  - `startOver` borra la conversación pero no lo aplicado.
  - Al restaurar el estado, los mensajes vuelven a aparecer.
- [ ] **Step 2:** Ejecutar. Esperado: FAIL.
- [ ] **Step 3:** Implementar `Session`. `DebugEditor` (provisional, con
  componentes JUCE básicos):
  - Un registro de texto con la conversación.
  - Las respuestas rápidas como botones.
  - Un campo de texto con "Enviar".
  - Los botones Escuchar, Antes/Después, Aplicar y Deshacer.
  - La compensación y los segundos escuchados como texto.
  - Un campo para el código de acceso (`Settings::setAccessCode`).
- [ ] **Step 4:** Ejecutar las pruebas. Esperado: PASS. Prueba manual en
  Logic con `IAQ8_DEFAULT_SERVER_URL` sin servidor: el flujo llega a
  `offline` con el aviso y la EQ sigue sonando. Repetir `auval`.
- [ ] **Step 5: Commit** `"IAQ8 P1.10: conversation session and debug editor"`.

---

## Al terminar el Plan 1

- Ejecutar todas las pruebas en Release y en Debug, y las funcionales con TSan
  (`-DCMAKE_CXX_FLAGS=-fsanitize=thread`, con `--exclude=perf`).
- Ejecutar `auval -v aufx Iaq8 Vtpl`.
- Comprobar con `git diff --stat 17c9c32 -- src tests CMakeLists.txt assets docs/ROADMAP.md`
  que la salida está **vacía**.
