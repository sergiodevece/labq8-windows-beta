# E2b: motor dinámico de ocho bandas

E2b integra en `EqCore` ocho secciones dinámicas independientes, una por
banda. Reutiliza el detector, la ley y la balística validados en E2a
([DYNAMICS_E2A.md](DYNAMICS_E2A.md)).

- **Solo DSP y pruebas:** el plugin sigue con sus 40 parámetros, su estado v3
  y su sonido. Los controles dinámicos son internos (`EqCore::setBandDynamics`).
- **Exposición al host:** queda para E2c.

## 1. Arquitectura

```
                 ┌──────────────── EqCore ────────────────┐
entrada ─┬─► sección 1 … sección 8   (DynamicSection: detector + ley + balística
         │       │ modulación (dB)    + rampas de umbral, rango y enable)
         │       ▼
         └─► EqBand 1 ─► EqBand 2 ─► … ─► EqBand 8 ─► salida   (en el sitio)
```

| Pieza | Papel |
|---|---|
| `DynamicSection` (nuevo, extraído de `DynamicBellBand`) | El estado dinámico de una banda: detector propio, threshold, range, attack, release, enable (fade de 20 ms), envolvente. No conoce el filtro: recibe f y Q actuales y devuelve una modulación en dB. |
| `EqCore` | `std::array<DynamicSection, 8>` junto a las 8 `EqBand`. API interna: `setBandDynamics(b, DynamicSettings)`, `setDetectorLink`, `getDynamics(b)`. |
| `EqBand` | Sin reescritura. La entrada de modulación de E2a ahora **solo afecta a la ruta Bell**: shelves, cortes y notch la ignoran. |
| `DynamicBellBand` | El prototipo E2a, reescrito como `EqBand` + `DynamicSection`. Es el mismo código que usa `EqCore`, y se mantiene para las pruebas de una banda. |

**Cuándo actúa una sección** (estado "engaged"):

- **Condición:** la banda es Bell **y** no está en bypass. `EqCore` lo
  recalcula en cada `setBandTargets`.
- **Al dejar de cumplirse** (cambio de tipo o bypass): la reducción se
  desvanece en 20 ms, igual que al apagar la dinámica, y después la modulación
  es 0 exacto.
- **Duración del fade:** 20 ms, la misma que el crossfade de tipos, así que la
  ruta Bell saliente se apaga mientras se desvanece su reducción.

## 2. Señal original común

**El requisito** es que los ocho detectores lean la entrada de `EqCore`, no la
salida de las bandas anteriores, con procesamiento en el sitio.

**Estrategia:** segmentos alineados con la rejilla de control.

Mientras alguna sección está activa, `process()` recorre el bloque en
segmentos de **16 muestras como máximo**, alineados con la rejilla absoluta de
ticks de las bandas (la misma de E0 a E2a). En cada segmento:

1. **Tick** (si toca): cada sección calcula su modulación con el nivel medido
   hasta la muestra anterior y se la entrega a su banda. La banda hace su
   propio tick justo después, en el paso 3.
2. **Detección:** los detectores activos leen el segmento, que **todavía no ha
   sido tocado** por ninguna banda.
3. **Cascada:** las bandas 1…8 filtran el segmento en el sitio.

| Requisito | Cómo se cumple |
|---|---|
| Sin asignaciones | No hay búfer de copia: el propio segmento, todavía intacto, es la "señal original". |
| Independiente del tamaño de búfer | La detección es por muestra y los ticks siguen una rejilla absoluta. Prueba 12: salida idéntica bit a bit con bloques de 1, 7, 16, 64, 480, 4096 y aleatorios, en mono y estéreo, con automatización de las 8 bandas y secciones. |
| Sin latencia | Cada segmento se detecta y se procesa en la misma llamada. La reducción de un tick usa la entrada hasta su primera muestra (causal, sin lookahead), igual que en E2a. |
| Sincronización | Todos los detectores leen las mismas muestras originales. La prueba 2 compara la traza de reducción de la banda 4 con la de otras configuraciones: es idéntica bit a bit cuando otras bandas realzan +24 dB la misma frecuencia (antes y después de ella en la cascada), cuando la recortan y cuando son dinámicas. También es idéntica a la de `DynamicBellBand` de E2a sola. |

- **Ruta estática intacta:** sin ninguna sección activa, `process()` sigue la
  ruta de E2a (banda a banda sobre el bloque entero) y solo avanza el contador
  de la rejilla, para que una activación posterior quede alineada. El arnés de
  compatibilidad lo verifica en modo estricto frente a E2a.

**Alternativas descartadas:**

- **Copiar la entrada a un búfer reservado en `prepare()`:** obliga a fijar un
  tamaño máximo de bloque. Si el host lo supera hay que trocear igualmente, y
  además añade una copia de memoria.
- **Detectar por muestra dentro de cada banda:** no evita leer la salida de
  la banda anterior.

**Coste:** los segmentos cortos añaden algo de sobrecarga de bucle; ver §6.

## 3. Estéreo enlazado

**Política:** el RMS de la región se mide **en cada canal**, cada uno con su
filtro y su ventana. El nivel de control es el **máximo** de los dos, y se
aplica **la misma** reducción a L y R. El filtrado de audio sigue siendo
independiente por canal, como antes.

En E2b, la opción por defecto de `DetectorLink::maxLinked` pasa a ser "máximo
de los niveles RMS por canal". En E2a era el máximo de la potencia instantánea
antes de promediar, que con material distinto en L y R sobrestima el nivel.

**Medido** (prueba 8/9; 1 kHz, Q 2):

- **Fuente en un canal o en ambos** con la misma amplitud: la misma reducción
  (±0,02 dB).
- **Panorámica de potencia constante** (L = A·cos θ, R = A·sin θ):
  - con `maxLinked`, la reducción se desplaza **3,01 dB** en el centro,
    porque cada canal lleva A/√2; coincide con la teoría;
  - con `energyLinked`, la variación es 0,0000002 dB: es invariante para esa
    ley de panorama.
- **Imagen:**
  - el mismo contenido a distinto nivel en L y R conserva su relación L/R
    (±0,01 dB): la ganancia es común, así que la imagen no se mueve;
  - con **contenido distinto** (un tono fuerte solo en L, ruido suave en R),
    R recibe la reducción de L: 12,0 dB en L hacen bajar la región de
    1 kHz de R 7,9 dB. Es el precio de enlazar.

**Interpretación:**

- **`maxLinked`:** "una componente de amplitud A en cualquier canal lee A".
  El umbral es estable frente a dónde está la fuente, pero no frente a la ley
  de panorama del mezclador (hasta 3 dB).
- **`energyLinked`:** no depende de la panorámica de potencia constante, pero
  lee 3 dB menos cualquier fuente lateral.
- **Decisión:** se mantiene `maxLinked` como se pidió. La alternativa queda
  disponible para la decisión de E2c.

## 4. Tipos de filtro

**Solo la Bell es dinámica.**

- **`EqBand`:** aplica la modulación únicamente a rutas Bell.
- **`EqCore`:** desactiva la sección (fade de 20 ms) cuando la banda no es
  Bell o está en bypass.

**Al cambiar de Bell dinámica a otro tipo** (prueba 10):

- **Fade:** la reducción se desvanece en 20 ms + 1 tick. A partir de ahí la
  modulación es **0 exacto** y la sección queda inactiva.
- **Topología nueva:** nunca recibe modulación. Cuando termina el crossfade de
  tipo, la salida es **idéntica bit a bit** a la de una banda estática que hace
  el mismo cambio.
- **Continuidad:** sin discontinuidades durante el cambio (cuarta diferencia
  dentro de la cota de los crossfades de E1b).
- **Cambios rápidos** entre los 6 tipos, cada 1–60 ticks, con dinámica activa:
  la modulación es 0 siempre que la banda lleve más de 20 ms + 1 tick sin ser
  Bell.

**Al volver a Bell:** el detector arranca desde silencio y la reducción crece
con el ataque, dentro del fade de 20 ms.

**Bypass de una Bell dinámica** (prueba 11): la reducción se desvanece, la
ganancia vuelve a 0 dB y la banda queda en idle. Desde ahí la salida es la
entrada **bit a bit**.

### Propuesta para Shelves dinámicos (no implementada)

Aplicarles el detector de la Bell no tendría sentido:

1. **Región de control:** un shelf no tiene "centro". Su región es la meseta:
   por debajo de f0 en Low Shelf y por encima en High Shelf.
   - **Propuesta:** detector de paso bajo (o alto) de 4.º orden en f0, que mide
     la energía de la meseta.
   - **Normalización:** que un seno en plena meseta lea su nivel.
2. **Significado del umbral:** la meseta es ancha, así que el material de
   banda ancha leerá mucho más que con una Bell estrecha. Habría que decidir
   entre un umbral de "nivel de la meseta" (sencillo) y uno de densidad
   espectral (estable con ruido).
3. **Tiempos:** un Low Shelf a 80 Hz necesita una ventana de varios periodos
   de 80 Hz. La detección será lenta, igual que en las Bells graves (§7).
4. **Qué se modula:** la ganancia del shelf. La pendiente (Q efectivo ≤ 1,
   E1b.1) se mantiene, así que la sobreoscilación sigue acotada a 1,12 dB.
   `EqBand` ya tiene la entrada de modulación; solo habría que permitirla en
   la ruta shelf.
5. **Pruebas necesarias:**
   - lectura en la meseta frente a la transición;
   - rechazo de la meseta contraria;
   - interacción con Bells dinámicas solapadas.

**Recomendación:** decidir primero el punto 2 (definición del umbral), con
medidas sobre material real. Hasta entonces, los shelves siguen estáticos.

## 5. Pruebas E2b

Archivo `tests/EqCoreDynamicTests.cpp`: 11 pruebas. Los números son los del
encargo.

| Prueba | Cubre |
|---|---|
| Ocho Bell dinámicas a la vez, cada una con su umbral y rango | 1, 6 |
| Detectores sobre la entrada común: independientes, sin realimentación | 2, 3 |
| Una banda dinámica entre siete estáticas = referencia por segmentos construida aparte (bit a bit) | 4 |
| Automatización de las 8 bandas y secciones: independiente del bloque, finita, sin asignaciones | 5, 12, 14 |
| Attack y release independientes por banda | 7 |
| Estéreo enlazado por máximo, panorámica, diferencias L/R | 8, 9 |
| Cambios de tipo desde una Bell dinámica | 10 |
| Activar, desactivar y bypass durante la reproducción | 11 |
| Frecuencias y Q extremos, > 0 dBFS, NaN/Inf | 13, 14, 15 |
| Varias bandas modulando a la vez frente a una referencia por muestra | 16 |
| CPU de ocho bandas dinámicas (Release) | 7 (rendimiento) |

**Resultados:**

| | Debug arm64 | Release arm64 | Release x86_64 |
|---|---|---|---|
| Pruebas | 94 + 4 SKIP (CPU) | **98/98** | **98/98** |

- **Las 87 pruebas de E2a:** todas pasan, sin cambios.
- **Medidas destacadas:**
  - con ocho bandas dinámicas a la vez, la reducción queda a ≤ 0,15 dB de la
    ley de cada banda;
  - la salida en cada tono coincide con la cascada prevista (≤ 0,15 dB);
  - con ocho bandas modulando a la vez, el residuo frente a la referencia por
    muestra es de −113 dB.

**Mutaciones** (Debug, archivos restaurados y comprobados con `cmp`):

| Mutación | Pruebas E2b que fallan |
|---|---|
| Detectores después de la cascada (leen la salida del EQ) | 8 de 11 (por ejemplo, independencia: 3/4; banda única: 6/6) |
| Todas las secciones con los ajustes de la banda 1 | 9 de 11 (attack/release: 15/16) |
| Modulación también en shelves | cambios de tipo (2/21) |
| "Engaged" ignora el bypass | enable/bypass (1/5) |
| Enlace por suma en vez de máximo | estéreo E2a y E2b |
| La ruta estática no avanza la rejilla de ticks | Activación de la dinámica a mitad de flujo (añadida). Antes de añadir esa comprobación, ninguna prueba lo detectaba. |

## 6. Rendimiento

**Condiciones de la medida:**

- `labq8_bench --e2b`, Release arm64, Apple M5, macOS 26.5.
- **Ocho** bandas Bell en estéreo (60 Hz … 15 kHz, Q 3, +2 dB), entrada de
  ocho tonos modulados en amplitud que cruzan los umbrales, más ruido.
- Umbral −24 dBFS, rango 15 dB, ataque 2 ms, release 30 ms.
- Mediana de 5 pasadas de 3 s, después de una de calentamiento, en un solo
  hilo. Los valores son % de un núcleo, en una máquina compartida (±20 %).

| Escenario | 44,1 kHz / 64 | 44,1 / 512 | 48 / 64 | 48 / 512 | 96 / 64 | 96 / 512 |
|---|---|---|---|---|---|---|
| 8 bandas estáticas | 0,17 | 0,18 | 0,17 | 0,20 | 0,35 | 0,40 |
| 8 bandas, dinámica configurada pero apagada | 0,16 | 0,18 | 0,18 | 0,22 | 0,36 | 0,40 |
| 1 banda dinámica activa | 0,22 | 0,22 | 0,24 | 0,24 | 0,54 | 0,50 |
| 4 bandas dinámicas activas | 0,33 | 0,35 | 0,38 | 0,37 | 0,68 | 0,65 |
| **8 bandas dinámicas activas** | **0,43** | **0,42** | **0,52** | **0,50** | **0,90** | **0,96** |
| 8 detectores sin reducción (solo detección) | 0,34 | 0,41 | 0,59 | 0,52 | 0,88 | 1,07 |
| 8 dinámicas + automatización intensa (todo en cada bloque) | 0,56 | 0,66 | 0,75 | 0,76 | 1,23 | 1,09 |

**Lectura:**

- **Dinámica apagada = estática**, porque se usa la ruta de E2a.
- **Ocho bandas activas a 96 kHz:** 0,90–0,96 % de un núcleo, menos de la
  mitad de la estimación del 2 % de E2a. La ruta estática de 8 bandas cuesta
  0,35–0,40 %, así que la dinámica añade unos 0,55 puntos.
- **Detección frente a modulación:**
  - "solo detección" (0,88–1,07 % a 96 kHz) cuesta prácticamente lo mismo que
    "8 activas" (0,90–0,96 %);
  - es decir, **casi todo el coste es la detección**: 4 biquads por muestra y
    por canal y banda, más la ventana. La modulación de coeficientes (un
    `designBell` por tick, 13–16 ns, más la interpolación) queda dentro del
    ruido de la medida.
  - Algunas filas de "solo detección" salen por encima de "8 activas"; es la
    variación de la máquina.
- **Automatización intensa:** añade 0,2–0,3 puntos, por el rediseño en cada
  tick de las rampas de f y Q de las 8 bandas.
- **Una banda dinámica:** cuesta más que 1/8 de ocho, porque al activarla las
  8 bandas pasan a procesarse en segmentos de 16 muestras.
- **Prueba de CPU** (`dyn8 perf`, 96 kHz, 8 activas): presupuesto del 5 %.
  Medido: 1,6 % en arm64 y 2,8 % en x86_64 bajo Rosetta, en una pasada con
  la máquina cargada.

**Comparación con E2a** (ejecuciones intercaladas E2a, E2b, E2a, E2b):

| Medida | E2a | E2b |
|---|---|---|
| Una banda dinámica activa, 96 kHz / 64 (`--e2a`) | 0,134–0,138 | 0,116–0,124 |
| Una banda en reposo, 96 kHz / 64 | 0,098–0,128 | 0,070–0,080 |
| 8 bandas estáticas, prueba `perf:` 96 kHz / 64 (tres pasadas) | 0,34–0,41 | 0,36–0,41 |
| 8 bandas en crossfade continuo, 96 kHz / 64 | 0,83–1,01 | 0,83–0,92 |

- **Resultado:** sin regresión. La ruta estática está a la par y la banda
  dinámica única cuesta lo mismo o algo menos. Probablemente se debe al nuevo
  bucle por canal del detector, pero no lo he aislado.

## 7. Riesgos DSP conocidos

1. **Retardo de detección en graves con Q alto:**
   - cada sección paso banda se asienta con τ = Q/(π·f0), y son dos;
   - a 60 Hz con Q 4, la reducción llega unos 50 ms más tarde que el ataque
     nominal (medido en la prueba 7);
   - a 20 Hz con Q 18, el detector necesita del orden de medio segundo.
   - Propuesta para E2c: limitar el Q del **detector** (no el del filtro)
     a 2–4, o detectar con un filtro de menor orden en graves.
2. **Enlace estéreo:** el contenido de un canal reduce el otro (7,9 dB en la
   prueba), y la ley de panorama mueve el umbral hasta 3 dB.
3. **Bandas solapadas:**
   - cada detector lee la señal original, así que dos Bells dinámicas en la
     misma región reaccionan **las dos** a la misma energía, y sus
     reducciones se suman en la salida;
   - es coherente (no hay realimentación) pero puede sorprender. Se
     documentará en la UI de E2c.
4. **Fase:** igual que en E2a. La fase de la Bell se mueve con su ganancia.
5. **Ganancia total** limitada a ±24 dB por banda (diseñador).
6. **Coste de la ruta dinámica:** cuando cualquier sección está activa, las 8
   bandas se procesan en segmentos de 16 muestras, aunque las otras siete
   sean estáticas. Ver §6.

## 8. Propuesta para E2c

1. **Parámetros públicos** al final, con el version hint 4 y estado v4 con
   migración v3 → v4 (dinámica apagada por defecto):
   `b{n}_dyn_on`, `b{n}_threshold`, `b{n}_range`, `b{n}_attack` y
   `b{n}_release` (40 parámetros nuevos). Las sesiones v1–v3 sonarán
   idénticas; lo verificará el arnés estricto.
2. **Enlace estéreo:** decidir entre `maxLinked` y `energyLinked` (§3), o
   exponerlo como opción global.
3. **Q del detector** limitado en graves (riesgo 1).
4. **Shelves dinámicos:** solo después de fijar la definición del umbral de
   meseta (§4).
5. **Detección no enlazada / M/S:** con bancos independientes por canal, la
   imagen sí puede moverse; necesita pruebas propias de imagen.
6. **Medidor de reducción** para la futura UI: `DynamicSection::getGainReductionDb()`
   ya existe y es seguro de leer al final de cada bloque.
