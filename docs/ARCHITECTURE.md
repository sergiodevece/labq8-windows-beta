# LABQ8: arquitectura (E0)

## Capas

| # | Capa | Responsabilidad | Estado E0 | Ubicación |
|---|---|---|---|---|
| 1 | Plugin Host Adapter | JUCE `AudioProcessor`, buses, parámetros → ajustes DSP, estado | Implementado | `src/plugin/`, `src/parameters/` |
| 2 | EQ Core | Filtros paramétricos, suavizado, bypass | 1 banda Bell | `src/dsp/` |
| 3 | Dynamic Engine | Detectores, envolventes, control dinámico por banda | No iniciado | — |
| 4 | Analysis Engine | FFT, medición | No iniciado | — |
| 5 | Intention Engine | Perfiles musicales, sugerencias | No iniciado | — |
| 6 | UI Layer | Interfaz definitiva | Editor genérico de JUCE | (`GenericAudioProcessorEditor`) |

Las capas 3–6 no tienen código ni clases vacías: solo se describen aquí y en
[ROADMAP.md](ROADMAP.md).

### Reglas de dependencia

- **EQ Core no depende de JUCE.** `labq8_dsp` es una biblioteca estática en
  C++20 puro (`BiquadDesign`, `BellBand`, `BandLimits`). Se puede probar,
  reutilizar en la variante de mastering o en otro host sin arrastrar JUCE.
- **El Host Adapter no contiene DSP.** Lee los `std::atomic<float>` de los
  parámetros, construye `BellSettings` y llama a `BellBand::setTargets` /
  `process`. Nada más.
- **La UI no toca el DSP.** Solo habla con el `AudioProcessorValueTreeState`.
- Los rangos se definen una sola vez (`dsp/BandLimits.h`). La capa de
  parámetros los expone al host y el DSP vuelve a limitarlos por su cuenta,
  porque no debe fiarse de quien lo llame.

```
Host/DAW ──► LabQ8AudioProcessor (adapter) ──► APVTS (parámetros + estado)
                    │  readBand1Settings()           ▲
                    ▼                                │
              dsp::BellBand  (EQ Core, sin JUCE)   GenericAudioProcessorEditor
```

## EQ Core: decisiones

### Filtro

- Biquad IIR de fase mínima, diseño RBJ "Audio EQ Cookbook" (peaking).
  Equivale exactamente a la transformada bilineal de
  `H(s) = (s² + s·A/Q + 1) / (s² + s/(A·Q) + 1)`, con prewarping en f0 y
  `A = 10^(dB/40)`. Las pruebas comparan contra ese prototipo analógico, una
  derivación independiente del código de diseño.
- Estructura **Transposed Direct Form II**, con coeficientes y estado en
  `double` y E/S en `float`. Con un Bell a 20 Hz y 96 kHz los polos quedan muy
  cerca de |z| = 1, y en `float` el ruido de redondeo sería audible.
- A 0 dB los coeficientes cumplen b = a **bit a bit** (`b1` y `a1` salen de la
  misma expresión y `A = 1` exacto), así que el filtro es una identidad exacta
  con estado nulo. El bypass se apoya en esta propiedad.

### Suavizado de parámetros (anti-clicks)

- Rampas lineales de 20 ms: frecuencia y Q en dominio log2 (paso perceptual
  uniforme), ganancia en dB.
- Los coeficientes se recalculan cada **16 muestras** mientras una rampa se
  mueve. A 48 kHz son 60 pasos por rampa. Con todos los parámetros estables no
  se recalcula nada.
- La rejilla de actualización sigue un **contador absoluto de muestras**, no
  el inicio de cada bloque. Por eso la salida es idéntica bit a bit con
  cualquier tamaño de buffer (probado con 1, 7, 16, 64, 333, 512, 4096 y
  tamaños aleatorios).
- Los cambios de parámetro entran en vigor en el siguiente tick (≤ 16
  muestras, 0,33 ms a 48 kHz).

### Bypass de banda

- El bypass lleva la ganancia efectiva a 0 dB con la misma rampa. Cuando la
  rampa termina y el estado residual cae por debajo de 1e-9 (≈ −180 dBFS), la
  banda pasa a **idle**: deja el buffer intacto (seco bit a bit) y no consume CPU.
- Al reactivar, se arranca desde un estado nulo a 0 dB (identidad exacta) y la
  ganancia sube en rampa, sin discontinuidad.
- El bypass global del host usa el `processBlockBypassed` por defecto de JUCE
  (copia directa, sin rampa). Ver riesgos.

### Seguridad en tiempo real

- Sin asignaciones, locks, E/S, logging ni excepciones en `process()`. El
  estado tiene tamaño fijo (`std::array`, 2 canales).
- Las muestras de entrada no finitas se tratan como 0 antes de entrar en el
  filtro, así que el estado nunca se contamina. Si el estado llega a no ser
  finito por cualquier otra vía, se reinicia. Los valores menores de 1e-30 se
  ponen a cero (además de `ScopedNoDenormals` en el procesador).
- Los parámetros NaN/Inf o fuera de rango se limitan; la frecuencia queda
  además por debajo de 0,49·fs. Una fs inválida (≤ 8 kHz, > 768 kHz, NaN)
  cae a 44,1 kHz.
- Estabilidad: el diseño RBJ con Q > 0 y f0 < Nyquist siempre da polos dentro
  del círculo unidad. Se comprueba el triángulo de estabilidad en toda la
  rejilla de límites.

### Canales

- Bus principal mono→mono o estéreo→estéreo. Se rechazan mono→estéreo,
  estéreo→mono, surround y bus deshabilitado.
- Un estado independiente por canal y coeficientes compartidos (hasta que
  llegue M/S).

## Estado

`AudioProcessorValueTreeState` serializado como XML binario de JUCE. Raíz
`<LABQ8 labq8StateVersion="1">` con un hijo `PARAM id/value` por parámetro.
Un estado corrupto, vacío o de otro plugin se ignora sin cambiar nada. Detalle
en [PARAMETER_SCHEMA.md](PARAMETER_SCHEMA.md).

## Pruebas

Un solo ejecutable, `labq8_tests` (consola JUCE + harness propio, sin
dependencias externas), registrado en CTest. Se divide en:

- `dsp:` pruebas del EQ Core directamente, a 44,1, 48 y 96 kHz.
- `processor:` pruebas del adaptador sin cabeza: layout, estado, buses,
  equivalencia bit a bit con el EQ Core, automatización y creación del editor.

### Tolerancias y por qué

| Tolerancia | Uso | Justificación |
|---|---|---|
| Exacta (bit a bit) | 0 dB, bypass, mono vs estéreo, tamaños de bloque, procesador vs core, NaN ≡ 0 | Son propiedades estructurales: cualquier diferencia indica un error |
| 1e-4 dB | Respuesta de los coeficientes frente al prototipo analógico | Cálculo en double; el error observado es ~1e-12 dB, así que el margen solo cubre redondeo de `tan`/`cos` cerca de Nyquist |
| 0,01 dB | Senoides medidas con E/S float32 (ajuste por mínimos cuadrados) | El redondeo float aporta < 1e-5 dB; 0,01 dB está unas 10 veces por debajo de la menor diferencia audible de EQ (~0,1 dB) |
| 0,02 dB | Ganancia del procesador medida por RMS | El periodo parcial en la ventana RMS acota el error a ~0,01 dB |
| ≥ 20 dB de reducción | Clicks: máximo de la 4.ª diferencia frente a un control sin suavizado | La 4.ª diferencia atenúa un seno de 200 Hz ~70 dB y conserva las discontinuidades. Se exige además que el control sí produzca click (sensibilidad); comprobado desactivando el suavizado, con lo que el test falla |
| Cola < 1e-5 · pico | Decaimiento de la respuesta al impulso en los límites | Se mide en 2× el tiempo de 12 constantes del polo más lento (incluye polos sobreamortiguados) |
| Pico < 100 | Automatización aleatoria cada 32 muestras | Entrada de 0,5 de pico con +24 dB como máximo da ~8; llegar a 100 indicaría inestabilidad |

## Riesgos técnicos conocidos

1. **Sin `auval` ni validación en DAW.** `auval` solo valida componentes
   registrados en el sistema, y la regla de E0 prohíbe instalarlos. El VST3 sí
   se cargó en el build (el `juce_vst3_helper` genera `moduleinfo.json`
   cargando el binario). Hay que validar con `auval` y pluginval en una
   ubicación aislada en cuanto se autorice la instalación.
2. **Bypass del host sin rampa.** JUCE copia la entrada directamente y el
   cambio puede producir un click. Solución: exponer un parámetro de bypass
   global con rampa vía `getBypassParameter()` (propuesto para E1).
3. **Estado parcial en versiones futuras.** Al restaurar un estado sin algún
   parámetro (p. ej. una sesión E0 cargada en E1, sin las bandas 2–8), JUCE
   deja ese parámetro con su **valor actual**, no con el predeterminado. E1
   debe reiniciar a sus valores por defecto los parámetros ausentes antes de
   `replaceState`, usando `labq8StateVersion`.
4. **Cramping cerca de Nyquist.** El Bell bilineal se estrecha y deforma
   cerca de fs/2 (a 44,1 kHz, una banda en 18–20 kHz no tiene la forma de su
   equivalente analógico). Es inherente a la fase mínima con bilineal y está
   probado como "correcto según el diseño". Si se quiere una curva fiel en la
   parte alta, habrá que evaluar oversampling o diseño por ajuste de magnitud
   (tipo Vicanek) en una fase posterior.
5. **Coste de recálculo.** `designBell` usa `pow`/`sin`/`cos` cada 16
   muestras por banda mientras hay rampa. Con 8 bandas en automatización
   simultánea sigue siendo bajo, pero conviene medirlo en E1.
6. **Latencia de parámetro ≤ 16 muestras.** La automatización no tiene
   precisión de muestra. Es aceptable para EQ y habrá que revisarlo si la
   dinámica necesita más precisión.
7. **Nombre de compañía inconsistente en la marca.** En otros proyectos
   VitaPlug aparecen tanto "VitaPlug" como "VITAPLUG" con el mismo código
   `Vtpl`. LABQ8 usa "VitaPlug". Conviene unificarlo en todo el bundle.
8. **Toolchain.** Se compila con Command Line Tools y Makefiles, sin Xcode.
   La firma, el notarizado y el empaquetado necesitarán Xcode o `codesign` con
   identidad de desarrollador.
9. **`getTailLengthSeconds() = 0`.** Con Q alto y graves, la cola IIR real
   puede durar segundos. Algunos hosts cortan el procesamiento en silencio y
   eso puede truncarla. Hay que valorar si reportar una cola en función de Q y f0.
