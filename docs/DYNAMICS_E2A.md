# E2a: prototipo de Bell dinámica

Prototipo DSP de **una** banda Bell con reducción dinámica de ganancia. Sirve
para comprobar que la dinámica es viable en lo matemático, lo técnico y el
rendimiento antes de llevarla a las ocho bandas.

Alcance:

- **Sin parámetros nuevos:** no hay parámetro AU/VST3 nuevo; el plugin
  sigue con sus 40 parámetros y su ruta estática E1b.1.
- **Solo DSP:** el motor solo existe en `labq8_dsp` y en las pruebas.

Archivos:

| Archivo | Qué es |
|---|---|
| `src/dsp/DynamicDetector.{h,cpp}` | Detector de nivel (sin JUCE) |
| `src/dsp/DynamicGainLaw.h` | Ley de transferencia y balística |
| `src/dsp/DynamicBellBand.{h,cpp}` | La banda dinámica: `EqBand` + detector + ley + balística |
| `src/dsp/EqBand.{h,cpp}` | Entrada de modulación de ganancia (`setGainModulationDb`) con interpolación de coeficientes por muestra |
| `src/dsp/LinearRamp.h` | La rampa lineal de E0, extraída de `EqBand` para compartirla |
| `src/dsp/BiquadDesign.{h,cpp}` | `designBandPass` (filtro del detector) |
| `tests/DynamicTests.cpp` | 12 pruebas E2a |

## 1. Arquitectura del detector

```
entrada de la banda (antes del EQ)
   │
   ├─► [paso banda RBJ f0,Q] ─► [paso banda RBJ f0,Q] ─► y²  ─┐   por canal
   │                                                          │
   │        enlace: max (por defecto) │ energía media │ mono  ◄┘
   │                                   │
   │                 [1 polo τ] ─► [1 polo τ]   τ = 0,5/f0  (0,1…25 ms)
   │                                   │
   │                       nivel = 10·log10(2·ms)   dBFS de seno
   │
   └─► EqBand (Bell) ─► salida          ▲ ganancia − reducción (cada 16 muestras,
                                          interpolada muestra a muestra)
```

- **Señal de detección:** la entrada de la banda, leída **antes** de que
  `EqBand` la filtre en el sitio. La salida del EQ nunca llega al detector.
- **Región:**
  - dos paso banda RBJ en cascada (`designBandPass`, "constant 0 dB peak") en
    la frecuencia y Q **actuales** (ya suavizadas) de la banda;
  - es un filtro de 4.º orden, exactamente 0 dB en f0 para cualquier f0, Q y
    fs, gracias al prewarp de la bilineal.
- **Nivel:**
  - media cuadrática suavizada por dos polos en cascada, con τ = 0,5/f0
    (limitado a 0,1–25 ms), unas dos ventanas por periodo;
  - el rizado a 2·f0 de un seno queda en ±0,107 dB, y la demora es de un
    periodo aproximadamente.
- **Escala:**
  - `nivel = 10·log10(2·ms)` en **dBFS de seno**: un seno de amplitud A en f0
    lee 20·log10(A), y uno a fondo de escala lee 0 dBFS;
  - suelo de −160 dBFS en silencio.
- **Ataque y release:** no están en el detector. Actúan sobre la reducción
  (balística), de modo que el detector es un medidor puro y la constante de
  tiempo se puede medir sin ambigüedad.
- **Tiempo real:**
  - estado fijo (2 canales);
  - sin asignaciones ni bloqueos;
  - los NaN/Inf de la entrada cuentan como 0;
  - los estados no finitos se borran, y los menores de 1e-30 se ponen a 0 en
    cada tick;
  - el resultado no depende del tamaño de bloque, porque es por muestra y en
    la rejilla absoluta de ticks.

### RMS frente a pico: elección inicial RMS

| | RMS (media cuadrática) | Pico |
|---|---|---|
| Lectura de un seno en f0 | estable (±0,1 dB con τ = 0,5/f0) | rizado a 2·f0 si no hay retención; con retención, sesgo hacia los picos |
| Relación con lo que se oye | energía de la resonancia; correlaciona con la sonoridad | depende del factor de cresta y de la fase |
| Ruido y señales complejas | promedia; umbral estable | salta con cada pico aleatorio: más bombeo |
| Transitorios | reacciona con la ventana (≈1 periodo) | más rápido |
| Coste | 2 multiplicaciones-suma por muestra | similar |

- **Elección:** RMS. El objetivo de una Bell dinámica es controlar la
  **energía** de una región (resonancias, sibilancia, graves que se
  acumulan), no recortar picos.
- **Ventaja para el umbral:** un umbral sobre energía significa lo mismo para
  un seno, para ruido de banda estrecha y para una voz.
- **Más adelante:** un modo pico (o pico/RMS) puede añadirse como otra
  variante del detector sin cambiar la ley ni la balística.

### Normalización frente a frecuencia y Q

- **Componente tonal en f0:** el paso banda tiene ganancia exactamente 1 en
  f0, así que lee igual para cualquier f0, Q y fs (prueba 1: error máximo
  0,109 dB en 96 combinaciones).
  - **Ventaja:** un umbral de −20 dBFS significa siempre "la componente en f0
    supera −20 dBFS de seno".
- **Señales de banda ancha** (ruido): la lectura es proporcional al ancho de
  banda equivalente de ruido del detector, que es ∝ f0/Q.
  - Medido con ruido blanco de −10,8 dBFS RMS a 1 kHz: Q 0,5 → −19,8 dB, Q 2 →
    −25,8 dB, Q 8 → −31,8 dB, es decir, −6,0 dB por cada factor 4 de Q.
  - **Consecuencia:** con material de banda ancha, estrechar la banda baja la
    lectura y hace falta bajar el umbral. Es lo esperable, porque la banda
    contiene menos energía.
  - **Alternativa (no implementada):** un modo normalizado a densidad
    espectral (umbral en dB/Hz) que sume 10·log10(fs/2 / ENBW). La decisión
    queda para E2b, porque cambia el significado del umbral.
- **Rechazo** (prueba 2): coincide con |H_bp(f)|² teórico (±0,2 dB). A dos
  octavas de f0, con Q 1, lee −23,6 dB; con Q 2, −35,2 dB.
  - **Matiz:** por debajo de f0, la ventana sintonizada a f0 deja más rizado,
    porque la componente es de frecuencia menor que la ventana. La media sigue
    siendo correcta.
- **Retardo:** la ventana dura aproximadamente un periodo de f0. En graves la
  detección es necesariamente más lenta: a 50 Hz, unos 20 ms antes de que la
  lectura se estabilice. No se puede medir la energía de 50 Hz en menos de un
  periodo sin lookahead, y el lookahead está fuera de E2a.

## 2. Ley de transferencia

Con o = L − T, donde L es el nivel del detector y T el umbral, ambos en dBFS de seno:

| Región | Reducción |
|---|---|
| o ≤ −K/2 | 0 |
| \|o\| < K/2 | (o + K/2)² / (2K) — rodilla cuadrática |
| o ≥ K/2 | o (pendiente 1) |
| siempre | mín(…, Dynamic Range) |

- **Rodilla:** K = 6 dB, fija en E2a. Es continua y la pendiente pasa de 0 a 1.
- **Pendiente 1:** por encima de la rodilla, cada dB de exceso es un dB de
  reducción. La componente detectada en f0 se mantiene en T + ganancia
  estática hasta agotar el rango; a partir de ahí, la banda queda en
  ganancia estática − Range.
- **Una ley de ratio** se podrá añadir multiplicando por (1 − 1/ratio) sin
  cambiar nada más.
- **Ganancia aplicada:** ganancia estática − reducción × fade de enable.
  `EqBand` la sigue limitando a ±24 dB: con ganancia estática −12 dB y rango
  24 dB, la banda llega a −24 dB, no a −36 dB.
- **Balística** sobre la reducción en dB, un polo por tick (16 muestras):
  - usa el ataque si la reducción objetivo sube y el release si baja;
  - las constantes de tiempo son al 63,2 %;
  - después se limita al rango actual.
- **Reposo exacto:** con el objetivo en 0 y la reducción por debajo de 1e-4 dB
  (un paso de −98 dB relativo), la reducción pasa a 0 exacto. Desde ahí la
  banda es **bit a bit** la banda estática.
- **Rangos experimentales:**

  | Parámetro | Rango |
  |---|---|
  | Threshold | −80 … +24 dBFS |
  | Range | 0 … 24 dB |
  | Attack | 0,5 … 500 ms |
  | Release | 5 … 5000 ms |

  - Threshold, Range y el enable se suavizan con rampas de 20 ms (en el
    enable, como fade de la reducción).
  - Attack y release cambian en el acto, porque solo cambian una velocidad.

**Comprobado (prueba 3):**

- La reducción en régimen está a 0,077 dB de la ley como mucho.
- La ganancia medida en f0 está a 0,076 dB de "estática − reducción", con
  ganancia estática 0, +6 y −6 dB.
- El rango se respeta exactamente: con 30 dB de exceso y rango 6, la reducción
  es 6,000 y la salida en f0 −6,00 dB.

## 3. Modulación de la ganancia del filtro: cómo y a qué coste

**Lo que hice primero** fue aplicar la nueva ganancia en la rejilla de 16
muestras, es decir, rediseñar la Bell y cambiar los coeficientes de golpe en
cada tick.

- **Lo que se midió:** frente a un motor ideal que rediseña la Bell en cada
  muestra con la ganancia interpolada, esa escalera deja un error de
  **−22 dB** respecto a la señal durante el ataque, en el peor caso (ataque de
  0,5 ms).
- **Por qué importa:** es ruido de cremallera ("zipper"), con bandas laterales
  en múltiplos de fs/16. Era un defecto real.

**Solución aplicada** en `EqBand`:

- **Cuándo actúa:** solo cuando cambia la modulación.
- **Cómo:** en cada tick se diseña la Bell nueva (un `designBell` por tick), y
  dentro del tick los coeficientes se **interpolan linealmente, muestra a
  muestra**, desde el diseño anterior; el último sample del tick usa
  exactamente el diseño nuevo.
- **Estabilidad:** el triángulo de estabilidad (|a2| < 1, |a1| < 1 + a2) es
  convexo. Por eso cada juego intermedio de coeficientes entre dos diseños
  estables también es estable.
- **Resultados** (prueba 11; f0 de 100, 1000 y 5000 Hz; ataque de 0,5, 2 y
  10 ms):
  - **Error frente al motor ideal:** −60,9 dB en el peor caso (el criterio
    es < −55 dB). Antes era −22 dB.
  - **Clicks:** la parte de alta frecuencia del error (4.ª diferencia) queda
    al menos **41 dB por debajo** de la escalera de 16 muestras.
- **Ruta estática intacta:** sin modulación nada se interpola y los
  coeficientes cambian en el tick como en E1b.1. Lo prueban las 75 pruebas
  anteriores, la igualdad bit a bit con `EqBand` con la dinámica apagada y el
  arnés de compatibilidad estricto frente a E1b.1.

**Estabilidad variable en el tiempo** (prueba 11):

- **Prueba:** la modulación alterna entre 0 y −24 dB **en cada tick**, mucho
  más rápido que cualquier balística posible, con Q 0,1 y Q 18, f0 de 20 Hz,
  1 kHz y 20 kHz, ganancia 0 y +24 dB, y fs de 48 y 192 kHz.
- **Resultado:** la salida queda acotada (pico < 20 con entrada de 0,5, por
  debajo de la cota estática más 6 dB) y decae después de la señal.

**Coste:**

- **Por tick:** un `designBell` (pow, sin, cos), que el banco mide.
- **Por muestra, mientras se interpola:** 5 interpolaciones lineales más el
  biquad.
- **Posible optimización si E2b la necesita:** precalcular cos y α, que solo
  cambian con f y Q, y calcular por tick solo A = 10^(g/40) y dos divisiones.

### Medidas de CPU

`labq8_bench --e2a`, en Release arm64 sobre Apple M5: **una** banda en
estéreo, con una mediana de 5 pasadas de 3 s. Los valores son % de un núcleo.

| Escenario | 48 kHz / 64 | 48 kHz / 512 | 96 kHz / 64 | 96 kHz / 512 |
|---|---|---|---|---|
| Bell estática (`EqBand`) | 0,050 | 0,041 | 0,091 | 0,088 |
| Dinámica apagada | 0,048 | 0,050 | 0,097 | 0,092 |
| Dinámica en reposo (detector activo, sin reducción) | 0,104 | 0,107 | 0,204 | 0,220 |
| Dinámica activa (la ganancia cambia en cada tick) | 0,123 | 0,131 | 0,246 | 0,244 |

- **`designBell`:** 31,6 ns por llamada; a 96 kHz son 6000 llamadas por
  segundo, un 0,019 % de un núcleo.
- **Reparto del coste:** casi todo lo extra es el detector (4 biquads por
  muestra en estéreo) y la interpolación de coeficientes, no el rediseño.
- **Dinámica apagada:** cuesta lo mismo que la Bell estática.
- **Estimación para ocho bandas activas:** unas 8 × 0,25 % ≈ 2 % a 96 kHz.
- **Prueba de CPU:** tiene un presupuesto de 0,5 % por banda activa a 96 kHz;
  la medida está entre 0,10 y 0,30 % según la carga de la máquina.
- **Ruta estática** (prueba `perf:` de 8 bandas, intercalando E1b.1 y E2a tres
  veces, 96 kHz):
  - static y automating: iguales;
  - crossfade continuo de las 8 bandas: un 2–4 % más lento en las tres
    pasadas, por la comprobación por muestra de la interpolación en el bucle
    de crossfade. Es poco, pero es medible; se puede optimizar si hace falta.

## 4. Integración y automatización

- **`EqBand::setGainModulationDb(dB)`:**
  - suma un desplazamiento a la ganancia de las topologías con ganancia (Bell
    y shelves) a partir del siguiente tick;
  - las otras topologías lo ignoran;
  - con 0 (el valor por defecto, y el único que usa el plugin), `EqBand` es
    exactamente el de E1b.1.
- **`DynamicBellBand`:**
  - contiene un `EqBand` de tipo Bell, un `DynamicDetector`, la balística y
    las rampas de threshold, range y enable;
  - procesa por segmentos alineados con la rejilla de ticks de `EqBand`: tick
    de control, después el detector sobre la entrada del segmento, después
    `EqBand` en el sitio.
- **Frecuencia y Q:** se automatizan con las rampas de 20 ms de `EqBand`; el
  detector sigue los valores suavizados en cada tick.
- **Threshold y Range:** rampas de 20 ms en dB. El rango también limita la
  reducción ya suavizada, así que bajar el rango reduce la atenuación en
  20 ms.
- **Enable:**
  - **al encenderlo**, el detector empieza desde silencio y la reducción crece
    con el ataque, multiplicada por un fade de 20 ms;
  - **al apagarlo**, la reducción se desvanece en 20 ms. Después el detector
    se detiene (no gasta CPU) y la modulación es 0 exacto.
- **Comprobaciones de automatización:**
  - **cambios rápidos** (prueba 9), cada 1–100 muestras durante 1 s, de todos
    los ajustes: salida finita, sin asignaciones y convergencia al estado de
    una banda nueva con los ajustes finales (diferencia < 1e-4);
  - **bloques** (prueba 10): salida idéntica bit a bit con bloques de 1, 7,
    16, 64, 480, 4096 y aleatorios, en mono y estéreo, con dinámica activa y
    automatización.

## 5. Estéreo: propuesta para E2b/E2c

**Ya en E2a:** el detector admite 1 o 2 canales y tres enlaces, y la reducción
es **una sola** para todos los canales (la de `EqBand`).

| Enlace | Lectura de un seno a la izquierda solamente | Lectura centrado |
|---|---|---|
| `maxLinked` (por defecto) | A | A |
| `energyLinked` | A − 3,01 dB | A |
| `mono` ((L+R)/2) | A − 6,02 dB | A (en fase), −∞ (en contrafase) |

**Recomendación para E2b: `maxLinked`.**

- **Umbral independiente de la posición:** una fuente con la misma amplitud
  lee lo mismo esté centrada o panoramizada.
- **Imagen estable:** la reducción es común, así que no se desplaza. La prueba
  de estéreo lo comprueba: L y R reciben la misma ganancia con 0,01 dB de
  diferencia como máximo.
- **Inconveniente:** una resonancia en un solo canal reduce también el otro.
  Es el comportamiento habitual de un compresor enlazado.
- **Descartado como opción por defecto:**
  - `energyLinked` es más suave con fuentes laterales, pero el umbral pasa a
    depender del panorama (3 dB);
  - `mono` es ciego al material en contrafase.
- **E2c:** M/S y detección por canal no enlazada, con bancos dinámicos
  independientes. En ese caso la imagen sí puede moverse, y hará falta su
  propia evaluación.

## 6. Plan para ocho bandas (E2b)

1. **Separar los componentes:**
   - `DynamicBellBand` se divide en `DynamicSection` (detector, ley,
     balística y rampas) y el `EqBand` ya existente;
   - `EqCore` gana una `DynamicSection` opcional por banda, como
     `std::array` fijo.
2. **Detección desde la entrada del plugin**, no desde la salida de la banda
   anterior:
   - `EqCore` copia el bloque de entrada en un búfer reservado en `prepare()`;
   - cada sección lee de esa copia, y las bandas procesan en cascada como
     ahora.
   - Esto evita que una banda dinámica reaccione a lo que hizo otra.
3. **Topologías:** se aplica a Bell y shelves (las topologías con ganancia;
   `EqBand` ya lo admite). Cuts y Notch quedan excluidos o necesitarán otra
   definición.
4. **Parámetros públicos:**
   - `b{n}_dyn_on`, `b{n}_threshold`, `b{n}_range`, `b{n}_attack` y
     `b{n}_release`;
   - se añaden al final con el version hint 4, y el estado sube a v4 con
     migración (valores por defecto: dinámica apagada);
   - las sesiones v1–v3 sonarán idénticas.
5. **CPU:** ver las medidas; ocho bandas activas ≈ 8 × una banda activa.
   Optimizaciones posibles:
   - precalcular cos y α por banda (§3);
   - procesar el detector de varias bandas en un mismo bucle.
6. **Pruebas:** las de E2a, parametrizadas por banda, más interacción entre
   bandas dinámicas en cascada y estado v4.

## 7. Riesgos

1. **Clicks:**
   - con la interpolación por muestra, el error frente a un motor por muestra
     es −61 dB;
   - con ataques muy cortos (0,5 ms) sobre graves, la modulación misma
     (no la cremallera) produce distorsión de intermodulación, como en
     cualquier compresor rápido;
   - el ataque mínimo de 0,5 ms es experimental.
2. **Fase:** la Bell es de fase mínima, así que cambiar su ganancia cambia
   también su fase en la región. Durante la reducción, la fase en f0 se mueve
   con la ganancia. Es inherente a un EQ dinámico de fase mínima; solo una
   versión de fase lineal (fuera de alcance) lo evita.
3. **Estabilidad:**
   - cada juego de coeficientes es estable (convexidad);
   - la estabilidad variable en el tiempo no tiene garantía teórica general,
     pero está probada con la modulación más agresiva posible (0 / −24 dB en
     cada tick) y Q extremos.
4. **Retardo de detección:** un periodo de f0 más la balística. Sin lookahead,
   el primer milisegundo de un transitorio pasa sin reducción (prueba 6:
   0 dB en el primer ms).
5. **Umbral con material de banda ancha:** depende de Q (§1); la decisión de
   normalización queda abierta.
6. **Ganancia total limitada a ±24 dB:** el rango puede quedar parcialmente
   sin efecto si la ganancia estática ya es muy negativa.
7. **Sin limitador ni normalización:** las señales de más de 0 dBFS pasan sin
   recortar (prueba 8: un seno de +24 dBFS con 12 dB de reducción sale a
   +12,0 dBFS).

## 8. Pruebas de mutación

Se aplicaron en Debug; después se restauraron los archivos y se comprobaron
con `cmp`.

| Mutación | Pruebas que fallan |
|---|---|
| El detector lee la salida del EQ | 4: ley (36/98), balística (21/48), extremos, estéreo |
| Ataque y release intercambiados | 2: balística (30/48), transitorios |
| Sin tope de Dynamic Range | 3: ley (10/98), extremos (3/5), modulación |
| Sin interpolación de coeficientes (escalera de 16 muestras) | 1: modulación (18/42) |
| La banda a 0 dB pasa a idle aunque esté modulada | 1: reposo (comprobación de uso directo del hook) |

**Defecto que dejaban pasar las pruebas:** la última mutación no se detectaba
al principio, porque `DynamicBellBand` llama al hook en cada tick. Añadí una
comprobación de uso directo de `EqBand`: modulación fijada una sola vez,
silencio y después señal.
