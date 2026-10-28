# LABQ8: arquitectura (E1a)

## Capas

| # | Capa | Responsabilidad | Estado E1a | Ubicación |
|---|---|---|---|---|
| 1 | Plugin Host Adapter | JUCE `AudioProcessor`, buses, parámetros → ajustes DSP, estado y migración | Implementado | `src/plugin/`, `src/parameters/` |
| 2 | EQ Core | Filtros paramétricos, suavizado, bypass, cascada | 8 bandas Bell en serie | `src/dsp/` |
| 3 | Dynamic Engine | Detectores, envolventes, control dinámico por banda | No iniciado | — |
| 4 | Analysis Engine | FFT, medición | No iniciado | — |
| 5 | Intention Engine | Perfiles musicales, sugerencias | No iniciado | — |
| 6 | UI Layer | Interfaz definitiva | Editor genérico de JUCE | (`GenericAudioProcessorEditor`) |

Las capas 3–6 no tienen código ni clases vacías.

### Reglas de dependencia

- **EQ Core no depende de JUCE.** `labq8_dsp` (C++20 puro) contiene
  `BandLimits`, `BiquadDesign`, `BellBand` y `EqCore`.
- **El Host Adapter no contiene DSP.** Lee los `std::atomic<float>` de los 32
  parámetros, construye un `BellSettings` por banda y llama a
  `EqCore::setBandTargets` / `process`.
- **La UI solo habla con el `AudioProcessorValueTreeState`.**
- Los rangos se definen una vez (`dsp/BandLimits.h`). El DSP vuelve a
  limitarlos porque no se fía de quien lo llama.
- Los IDs y las frecuencias por defecto están en `parameters/ParameterIDs.h`.
  La migración de estado está en `parameters/StateMigration.*`.

```
Host/DAW ──► LabQ8AudioProcessor ──► APVTS (32 parámetros + estado v2)
                │  pushParametersToCore()       ▲      ▲
                ▼                               │      └─ migrateState() al restaurar
          dsp::EqCore                  GenericAudioProcessorEditor
            ├─ BellBand 1 ─► BellBand 2 ─► … ─► BellBand 8   (en serie, in-place)
```

## EQ Core

### `BellBand` (sin cambios desde E0)

`src/dsp/BellBand.*`, `BiquadDesign.*` y `BandLimits.h` son idénticos a
`dfcf25b`. Todo lo validado en E0 se mantiene:

- **Diseño:** RBJ peaking en TDF-II, coeficientes y estado en double, E/S en float.
- **Suavizado:** rampas de 20 ms (log2 para frecuencia y Q, dB para la
  ganancia). Los coeficientes se recalculan cada 16 muestras sobre un
  contador absoluto.
- **Idle:** una banda en bypass, o a 0 dB con el estado ya extinguido, deja
  el buffer intacto (bit a bit) y no consume CPU.
- **Protección:** las muestras no finitas entran como 0; el estado no finito
  se reinicia; los valores menores de 1e-30 se ponen a cero; Nyquist a 0,49·fs.

### `EqCore` (nuevo en E1a)

- `std::array<BellBand, 8>`, de tamaño fijo y sin asignaciones (comprobado
  con un contador de `operator new`, ver Pruebas).
- **Cascada banda a banda sobre el bloque completo:** la banda 1 procesa todo
  el bloque in-place, luego la 2, y así hasta la 8. Como cada banda solo ve
  su propia secuencia de entrada, el resultado es bit a bit igual a una
  cascada muestra a muestra, y conserva la independencia del tamaño de
  bloque de cada banda (probado).
- **Independencia de estado:** cada banda tiene su propio estado por canal,
  sus rampas y su contador. Probado: la cascada de `EqCore` es idéntica bit
  a bit a ocho `BellBand` independientes encadenadas a mano, y el estéreo es
  idéntico a dos monos.
- Entre bandas, la señal vuelve a float (E/S de cada `BellBand`). Esto
  añade como mucho 8 redondeos de float32 (~−150 dBFS), muy por debajo de la
  tolerancia de medida. Pasar la cascada completa a double es una optimización
  posible, no una necesidad.
- `magnitudeAt(f)` da la curva compuesta (producto de las 8 bandas) con los
  coeficientes actuales. Servirá a la futura curva de la UI y ya se usa en
  las pruebas.

### Host Adapter: ganancia neutra exacta (corrección de un defecto de E0)

El parámetro de ganancia del host ajusta su valor a una rejilla de 0,01 dB
en float. En **arm64** esa operación (`-24 + k·0.01f`) se compila como FMA, y
"0.00 dB" llega al DSP como **−5,364e-7 dB**. La consecuencia en E0 era que
la banda nunca pasaba a idle con ganancia 0: no había transparencia bit a
bit en el plugin real y se gastaba CPU. Las pruebas DSP de E0 no lo
detectaron porque usaban 0.0 exacto. Lo detectan los fixtures reales de E0
(`e0_default.bin`). En x86_64, sin FMA, el valor sí es 0 exacto.

Corrección: en `readBandSettings`, |ganancia| < 0,005 dB (media rejilla, es
decir, todo lo que el host muestra como "0.00 dB") se trata como 0 exacto. La
desviación de E0 frente a la identidad era de 2,98e-8 como máximo
(inaudible). La compatibilidad de audio con sesiones E0, cuantificada caso
por caso en las dos arquitecturas, está en
[COMPATIBILITY_E0_E1A.md](COMPATIBILITY_E0_E1A.md). La prueba `processor: default and all-0 dB states are bit-exact
transparent` falla si se quita la corrección (verificado por mutación).

### Bit-exactitud entre arquitecturas

Las garantías bit a bit (bloques, canales, bypass, procesador frente a
núcleo) valen **dentro de una misma arquitectura**. Entre arm64 y x86_64 hay
diferencias del orden de un ulp, porque clang contrae `a*b + c` en FMA en
arm64. Esto afecta tanto al DSP (en double, invisible tras convertir a float
salvo casos raros) como al ajuste a rejilla de JUCE (en float, visible en el
estado guardado). Las pruebas se ejecutan en las dos arquitecturas, y donde
procede la tolerancia está justificada (1 ulp de 24 dB, ver
`StateMigrationTests.cpp`). Si algún día hiciera falta reproducibilidad entre
arquitecturas, habría que compilar el DSP con `-ffp-contract=off`, con su
coste en rendimiento, no medido aún.

## Estado y migración

Formato: XML binario de JUCE con raíz `<LABQ8 labq8StateVersion="2">` y un
`PARAM id/value` por parámetro.

| Versión | Fase | Contenido |
|---|---|---|
| 1 | E0 | `b1_*` (4 parámetros) |
| 2 | E1a | `b1_*` … `b8_*` (32 parámetros) |

`params::migrateState()` se ejecuta antes de `replaceState()`:

1. Lee la versión del árbol (0 si falta).
2. Aquí irán las transformaciones propias de cada versión. De la 1 a la 2
   solo se añaden parámetros.
3. Para cada parámetro del procesador **ausente** en el árbol, añade un
   `PARAM` con su **valor por defecto explícito**.
4. Escribe `labq8StateVersion = 2`.

**Por qué hace falta (corrige lo que documenté en E0):** sin migración, JUCE
9.0.3 sí acaba asignando el valor por defecto a un parámetro ausente (vía
`valueTreeChildAdded`). Pero según la historia de la instancia, el estado que
se vuelve a guardar contiene `<PARAM id="b2_freq"/>` **sin valor** o con
valor explícito. Así, la misma sesión E0 produce estados distintos según la
instancia que la cargue. En E0 escribí que JUCE "deja el valor actual"; la
prueba empírica muestra que no es así en este caso, pero el estado sí era no
determinista. Con la migración, cualquier instancia que cargue la misma
sesión E0 produce bytes idénticos.

- Un estado corrupto, vacío o con otra raíz se ignora.
- Un `PARAM` con un ID desconocido (por ejemplo de una versión futura) se
  **conserva** en el árbol, pero no crea parámetros. No es destructivo.
- Un estado sin propiedad de versión se migra como si fuera v1.

## Rendimiento

Banco `labq8_bench` (Release, sin JUCE): 8 bandas en estéreo, ruido blanco
con semillas fijas, 10 s de audio por pasada, mediana de 7 pasadas tras 1 de
calentamiento, un solo hilo. Equipo: Apple M5 (Mac17,4), macOS 26.5.

| Escenario | 44,1 kHz | 48 kHz | 96 kHz | ns/frame estéreo |
|---|---|---|---|---|
| idle (8 bandas a 0 dB) | 0,01–0,02 % | 0,01 % | 0,02 % | ~2–5 |
| estático (8 bandas activas) | 0,11–0,16 % | 0,11–0,17 % | 0,25–0,35 % | ~24–35 |
| automatización (8 bandas en rampa continua) | 0,16–0,18 % | 0,19–0,21 % | 0,37–0,41 % | ~38–44 |

Porcentaje de un núcleo con bloques de 32, 64 y 512 muestras. Entre dos
ejecuciones completas la variación fue de hasta ±20 %, porque la máquina
estaba compartida con otras compilaciones; las cifras sirven como orden de
magnitud. El slice x86_64 bajo Rosetta da como peor caso un 0,57 % a 96 kHz,
solo como indicación (Rosetta no representa un Intel nativo).

La prueba `perf:` (solo en Release) exige menos del 5 % de un núcleo en el
peor caso (96 kHz, bloques de 64, 8 bandas en rampa). Hay unas 12 veces de
margen sobre lo medido: está pensada para detectar regresiones graves, no
variaciones finas.

## Compatibilidad macOS

| | arm64 | x86_64 |
|---|---|---|
| Mínimo que soporta JUCE 9.0.3 (README de JUCE) | 11.0 (no hay macOS arm64 anterior) | 10.11 |
| Objetivo actual de LABQ8 (`CMAKE_OSX_DEPLOYMENT_TARGET`) | 11.0 | 11.0 |
| `minos` verificado con `vtool` en AU, VST3, tests y bench (E0 y E1a) | 11.0 | 11.0 |
| SDK usado | macOS 26.5 | macOS 26.5 |

**Decisión: se mantiene 11.0 para los dos slices.** El código no usa nada de
la biblioteca estándar que exija un macOS más moderno, y JUCE permitiría
bajar el slice Intel hasta 10.11. No lo bajo porque:

1. arm64 no puede bajar de 11.0, y un objetivo único simplifica el binario
   universal (CMake aplica un único `CMAKE_OSX_DEPLOYMENT_TARGET`; separarlos
   requiere flags por arquitectura).
2. No hay forma de probar aquí en macOS 10.x, y no voy a declarar un soporte
   que no se ha probado.
3. Logic Pro actual exige un macOS muy posterior a 11, así que el público
   objetivo no gana nada bajándolo.

Si en el futuro se quiere dar soporte a Intel con macOS 10.13–10.15, será una
decisión de producto con su propia ronda de pruebas en hardware real.

## Pruebas

Un solo ejecutable, `labq8_tests` (consola JUCE + harness propio), registrado
en CTest, con 40 pruebas:

| Prefijo | Nº | Qué cubre |
|---|---|---|
| `dsp:` | 11 | E0, `BellBand` (archivo sin cambios) |
| `processor:` (E0) | 7 | E0, adaptador (un único cambio: nº de parámetros de 4 a 32) |
| `eq:` | 10 | E1a: respuesta individual, compuesta, transparencia, bypass individual, automatización simultánea y aleatoria, independencia, bloques, NaN, extremos apilados |
| `processor:` (E1a) | 5 | Layout de 8 bandas, transparencia por defecto, procesador = núcleo, bloques del host, automatización de varias bandas |
| `state:` | 4 | Migración con estados E0 reales, sonido igual a E0, round-trip v2, parciales/desconocidos |
| `rt:` | 2 | Contador de asignaciones (autotest) y cero asignaciones en `process`/`processBlock` |
| `perf:` | 1 | Presupuesto de CPU (solo Release; en Debug se informa como SKIP, nunca como PASS) |

### Tolerancias y por qué

| Tolerancia | Uso | Justificación |
|---|---|---|
| Exacta (bit a bit) | 0 dB, bypass, mono/estéreo, bloques, procesador = núcleo, cascada = 8 bandas sueltas, NaN ≡ 0, migración determinista | Propiedades estructurales dentro de una arquitectura |
| 1e-4 dB | Respuesta de coeficientes frente al prototipo analógico (también la suma de 8 bandas) | Cálculo en double; el error observado es ~1e-12 dB |
| 0,01 dB | Senoides medidas con E/S float32 (1 a 8 bandas) | Redondeo float < 1e-5 dB por etapa, 10 veces por debajo de lo audible |
| 0,02 dB | Ganancia del procesador por RMS | Periodo parcial en la ventana |
| ≥ 20 dB de reducción | Clicks (4.ª diferencia) frente a un control sin suavizado | Validado por mutación en E0 |
| Cola < 1e-5·pico | Respuesta al impulso. 1 banda: 2× el tiempo de 12τ; 8 bandas iguales apiladas: 4×, porque 8 polos repetidos decaen como t⁷e^(−t/τ) | |
| 5e-7 × máx(1, \|v\|, 24) | Valores b1 restaurados desde fixtures (escritos en arm64) | Unos pocos ulp: absorbe la diferencia FMA arm64/x86_64; para la ganancia la escala es ulp(24) ≈ 1,9e-6 |
| < 5 % de un núcleo | Peor caso de CPU | Unas 12 veces por encima de lo medido |

### Verificación de sensibilidad (mutaciones)

| Mutación | Resultado |
|---|---|
| Suavizado desactivado (E0) | Falla el test de clicks (9 comprobaciones) |
| Sin `migrateState()` | Falla `state: genuine E0 states…` en 6 comprobaciones (el estado depende de la historia y hay PARAM sin valor) |
| Sin la corrección de 0 dB | Falla `processor: default and all-0 dB states…` en arm64 (6 comprobaciones) |

En todos los casos se restauró después el archivo original (comprobado con `cmp`).

## Riesgos técnicos conocidos

1. **Sin `auval` ni validación en DAW.** `auval` solo valida componentes
   instalados, y la instalación sigue prohibida. El VST3 se carga en el build
   (lo hace `juce_vst3_helper`).
2. **Bypass del host sin rampa.** JUCE copia la entrada directamente. Está
   previsto en E1 (`master_bypass` con `getBypassParameter()`), fuera de E1a.
3. **Bit-exactitud solo dentro de cada arquitectura** (FMA). Ver arriba.
4. **Cramping cerca de Nyquist** (inherente al Bell bilineal de fase mínima).
5. **Latencia de parámetro ≤ 16 muestras.**
6. **`getTailLengthSeconds() = 0`.** Con 8 bandas de Q alto apiladas, la cola
   real puede durar segundos.
7. **Compañía "VitaPlug" frente a "VITAPLUG"** en otros proyectos del bundle.
8. **Toolchain sin Xcode** (firma y notarizado pendientes).
9. **El contador de asignaciones solo ve `operator new`**, no `malloc`
   directo ni asignaciones del sistema. El código propio no usa `malloc`.
   Además, en Release clang puede eliminar pares `new`/`delete` no
   observables; por eso el autotest fuerza que el puntero escape.
10. **Variabilidad de las medidas de CPU** (±20 %) en una máquina compartida.
    Para comparar entre fases hacen falta condiciones controladas (máquina
    en reposo, misma alimentación).
