# LABQ8 E4d · FFT y dinámica MID / SIDE

**Estado:** revisión técnica y revisión complementaria (§8) aprobadas;
commit local autorizado. Pendiente: revisión visual directa de las capturas
y SMOKE-9 físico en Logic.

Base: `52cf2b5` (E4c, SMOKE-8 PASS 8/8). Auditoría aprobada:
`docs/MIDSIDE_E4D_AUDIT.md` (D1–D7). Sin cambios en el DSP de E4b, en los
241 parámetros (IDs y orden), en el estado v5 ni en el aspecto de E4c.

## 1. Arquitectura

### A. Analizador MID / SIDE

- **Origen:** las mismas muestras en el tiempo PRE (antes del motor) y POST
  (después) que ya captaba el procesador. No hay capturas nuevas en el hilo
  de audio.
- **Componente:** en el editor, en `double`, antes de la FFT:
  M = (L + R) / 2, S = (L − R) / 2; después una sola FFT por traza. Nunca a
  partir de magnitudes. Bus mono: MID es el canal y SIDE es cero (no se
  dibuja nada).
- **Calibración:** la misma ventana, normalización y escala dBFS de UI-2b; un
  seno de amplitud A solo en M se lee 20·log10 A en MID (prueba F1).
- **Dominio por defecto:** el banco editado (MID o SIDE). En STEREO el
  analizador es exactamente el de UI-2b (lectura L/R sin alinear, potencia
  media de los canales).
- **Sincronía PRE / POST (desviación justificada de la auditoría):** la
  auditoría proponía alinear solo en el editor, pero los dos taps tienen
  contadores independientes y el editor puede leer entre la escritura PRE y
  la POST de un bloque, con lo que PRE iría un bloque por delante. Por eso
  el procesador pasa a ambas escrituras la misma **posición de flujo**
  (`streamPosition`, un `uint64_t` que avanza con cada bloque, también con
  bypass del host) y `SpectrumTap` la guarda (`streamEnd`). El editor lee
  POST primero y después PRE terminando en la misma posición
  (`copyAtStream`). Coste en el hilo de audio: una suma y un almacenamiento
  atómico por escritura. La prueba "e4d sync" reproduce la lectura a mitad
  de bloque: con la posición, PRE queda alineado; con la lectura normal iría
  un bloque por delante.
- **Menú en M/S:** cabecera MID o SIDE con Pre, Post, Pre + Post;
  separador; cabecera "Output" con "L/R Post (output)". El selector muestra
  "MID Post", "SIDE Pre + Post", "L/R Post", etc. El menú y los textos de
  STEREO no cambian. La elección "L/R Post" se guarda solo en la sesión del
  editor (como el resto de preferencias de vista), no en el estado.

### B. Dinámica por banco

- `DynamicsTelemetry` tiene un **banco observado** (atómico, 0..2), que el
  editor fija con el banco mostrado. El procesador ofrece la reducción del
  núcleo de ese banco (`engine.getCore (bank)`).
- Cada instantánea lleva **banco**, **generación** (id) y **disponibilidad**:
  - `live`: el banco observado es el que se oye y no hay fundido;
  - `unavailable`: banco en standby (no se oye) o fundido de 20 ms en curso;
  - `hostBypassed`.
- Una instantánea de otro banco, o no disponible, sustituye siempre a la
  guardada; el overlay solo dibuja datos `live` de su banco. Al cambiar de
  banco el overlay se vacía (sin valores del banco anterior) y vuelve con
  los del nuevo en el siguiente bloque.
- Nunca se muestran bancos en standby ni un 0 dB inventado: durante el
  fundido o en standby el overlay no dibuja nada.
- Solo bandas Bell, límite efectivo ±24 dB (sin cambios de UI-2c).
- Se elimina la nota "M/S dynamics display: E4d": DYN y ALL dibujan la
  dinámica de MID o SIDE.

### C. Interfaz

Sin cambios en PNG, geometría, botones ni aspecto de la curva. Se reutilizan
EQ / SPECTRUM / DYN / ALL; solo cambian textos del analizador y la
instrumentación.

## 2. Archivos

| Archivo | Cambio |
|---|---|
| `src/plugin/SpectrumTap.h` | posición de flujo por escritura; `copyLatestWithStream`, `copyAtStream` |
| `src/plugin/DynamicsTelemetry.h` | banco observado; banco, disponibilidad y estado en la instantánea |
| `src/plugin/PluginProcessor.{h,cpp}` | `streamPosition`; telemetría del banco observado; preferencia "L/R Post" del editor |
| `src/ui/SpectrumView.{h,cpp}` | dominio L/R / MID / SIDE; componente en el tiempo; PRE alineado con POST |
| `src/ui/DynamicsOverlay.{h,cpp}` | `setBank`; solo datos del banco y disponibles |
| `src/ui/LabQ8Editor.{h,cpp}` | sin nota E4d; dominio del analizador; menú y textos M/S |
| `tests/MidSideViewTests.cpp` (nuevo), `tests/EditorBankTests.cpp`, `tests/MidSideTests.cpp`, `CMakeLists.txt` | pruebas E4d |
| `tools/ui/UiSnapshot.cpp` | modo `--e4d`: capturas con audio sintético |
| `tests/compat/run_compat.sh` | base `e4c` (52cf2b5) |

## 3. Resultados

| Verificación | Resultado | Evidencia |
|---|---|---|
| Pruebas Release arm64 / x86_64 | 228/228 (1 omitida) en ambas | `build/e4d-report/tests_*.txt` |
| Pruebas E4d (F1–F7, D1–D5, D3 guardas / telemetría / reinicio, U1, T1/T2, instancias, sync, sync prepare, sync estrés) | 20/20 | idem |
| ThreadSanitizer, pruebas funcionales y de concurrencia (`--exclude=perf`) | 223/223 PASS (1 omitida), 0 FAIL, 0 avisos | `build/e4d-report/tsan_functional.txt` |
| ThreadSanitizer, pruebas de rendimiento (aparte, §8.1) | 1 PASS, 4 FAIL por presupuesto de CPU, 0 avisos | `build/e4d-report/tsan_perf.txt` |
| Auditoría RT (interpositor), 64/512/1024, cambios de modo, bypass, lector concurrente | 0 malloc / free / bloqueos / esperas (los `nanosleep` son del arnés) | `build/e4d-report/rt_audit.txt` |
| CPU hilo de audio con PRE+POST y telemetría | ≤ 0,06 puntos de un núcleo (1 y 8 instancias, 48 / 96 kHz) | `build/e4d-report/cpu-audio-thread_*.txt` |
| CPU editor FFT M/S frente a L/R | menor (1 FFT en lugar de 2): 48 kHz 0,28 % frente a 0,46 % (arm64) | `build/e4d-audit-fft-cost_*.txt` |
| Mutaciones dirigidas | 16/16 detectadas (§5, §8.2) | `build/e4d-report/mutations_r2.txt` |
| Compatibilidad estricta (15 bases, arm64 + x86_64) | 30/30 OK (§6); repetida contra e4c con el código final | `build/e4d-report/compat_all.txt`, `compat_e4c_final.txt` |

### Sincronía FFT

- F4: con bancos planos, MID y SIDE POST son idénticos bit a bit a PRE en la
  misma posición de flujo, con impulsos en el mismo índice, para bloques de
  32, 64, 333, 480 y 1024 muestras.
- "e4d sync": lectura entre la escritura PRE y la POST de un bloque.
- Auditoría RT con lector concurrente: > 99 % de las lecturas PRE terminan en
  la posición de POST; el resto son copias sobrescritas durante la lectura
  (seqlock), que se reintentan en el siguiente fotograma y nunca se dibujan.

### Dinámica por banco

- D1/D2: con dinámicas distintas en STEREO, MID y SIDE, el banco mostrado
  recibe exactamente la reducción de su núcleo; un tono en M solo reduce MID
  y uno en S solo SIDE.
- D: las ocho Bell de MID y de SIDE muestran su reducción real.
- D3 / D3 rápido: durante el fundido, no disponible (no 0 dB); 300 cambios
  aleatorios de banco y modo sin ningún valor de otro banco ni de uno en
  standby.
- D4/D5: reducción efectiva con límite ±24 dB; bypass, silencio y SIDE en
  pista mono no dibujan nada.

## 4. Capturas

`build/e4d-captures/` (audio sintético: bombo y bajo al centro, pad ancho,
charles con mucho lateral; MID con Bell dinámica en 70 Hz, SIDE con corte de
graves y Bell dinámica en 6 kHz): STEREO / ALL, MID / ALL, SIDE / ALL,
MID / DYN, SIDE / DYN, MID y SIDE / SPECTRUM Pre + Post, al 40, 60 y 100 %,
más recortes del selector y las pestañas a cada zoom (1x y 2x) y dos hojas
de comparación (`e4d_selector_sheet_1x.png`, `e4d_selector_sheet_2x.png`). Los medidores IN / OUT
aparecen vacíos porque la herramienta no hace avanzar su temporizador.

## 5. Mutaciones

Detalle: `build/e4d-report/mutations_r2.txt` (segunda ronda, árbol final; la primera en `mutations.txt`).

| Mutante | Resultado |
|---|---|
| D-M1 telemetría ignora el banco observado | detectado (8 pruebas) |
| D-M2 banco en standby ofrecido como live | detectado |
| D-M3 fundido ofrecido como live | detectado (D3 guardas) |
| D-M4 overlay ignora el banco de la instantánea | detectado (D3 guardas) |
| D-M5 overlay acepta datos no disponibles | detectado (D3 guardas) |
| D-M6 cambio de banco sin reiniciar `started` | detectado (D3 reinicio); no era equivalente, §8.2 |
| D-M7 telemetría conserva la instantánea del banco anterior | detectado (D3 telemetría) |
| F-M1 MID/SIDE calculados como L/R | detectado (views, F1, F2/F3, F4, F6, U1) |
| F-M2 SIDE = (L + R) / 2 | detectado |
| F-M3 SIDE sin el 1/2 (+6 dB) | detectado (F1) |
| F-M4 PRE sin alinear con POST | detectado (sync) |
| F-M5 posición de flujo sin avanzar | detectado (F4) |
| F-M6 PRE escrito con la posición del bloque siguiente | detectado (F4, sync estrés) |
| F-M7 `prepare` conserva la posición de flujo anterior | detectado (sync prepare, sync estrés) |
| U-M1 "L/R Post (output)" sin identificar en el menú | detectado (U1) |
| U-M2 texto del selector "MID" en salida L/R | detectado (U1) |

La primera ronda dejó D-M3, D-M4, D-M5 y D-M7 vivos: se añadieron
las pruebas "D3 guardas" y "D3 telemetría". F-M1 aparecía como superviviente
porque el proceso caía en el ayudante de prueba `levelAt` (vector vacío) y
se perdía la salida; el ayudante se corrigió.

## 6. Compatibilidad

`tests/compat/run_compat.sh all` con la nueva base `e4c` (52cf2b5): 15 bases
(e0 … e4b, e4c) en arm64 y x86_64, 30/30 OK. Estrictas sin diferencias,
salvo e0 (diferencias explicadas por el 0 dB nominal) y e1b (solo donde un
shelf o corte superaba su límite de Q), igual que en las fases anteriores.
STEREO es bit a bit idéntico; las sesiones v5 con bancos M/S, también.

## 7. Riesgos pendientes

- La reducción mostrada es la del instante de mayor reducción entre
  fotogramas (A2 de UI-2c), igual que en STEREO.
- Tras un cambio de banco o de modo el overlay queda vacío hasta el primer
  bloque del banco nuevo (≈ un bloque) o el final del fundido de 20 ms.
- SMOKE físico en Logic de E4d pendiente (no se instala nada en esta fase).

## 8. Revisión previa al commit

### 8.1 ThreadSanitizer: funcionales frente a rendimiento

El ejecutor de pruebas acepta ahora `--exclude=texto` (`tests/TestMain.cpp`).
No se ha tocado ningún presupuesto de CPU.

- **Funcionales y concurrencia** (`labq8_tests --exclude=perf`, compilación
  TSan `build/e4b-tsan`): 224 pruebas, **223 PASS, 0 FAIL, 1 omitida**
  (fixtures, solo con `LABQ8_WRITE_FIXTURES`), **0 avisos** de
  ThreadSanitizer. Incluye todas las E4b / E4c / E4d (telemetría, taps,
  sincronía, estrés con dos instancias, sin asignaciones).
- **Rendimiento** (`labq8_tests perf`, mismo binario TSan): 5 pruebas,
  1 PASS, **4 FAIL solo por presupuesto**, 0 avisos. La instrumentación de
  TSan multiplica el coste por 10–25; en Release las cuatro pasan con margen:

| Prueba | Presupuesto | Bajo TSan | Release arm64 | Release x86_64 |
|---|---|---|---|---|
| perf: 8 bandas en crossfade continuo (96 kHz, bloque 64) | < 5 % | 11,16 % | 0,47 % | 0,57 % |
| dyn perf: una banda dinámica activa (96 kHz) | < 0,5 % | 1,11 % | 0,067 % | 0,14 % |
| dyn8 perf: ocho bandas dinámicas (96 kHz) | < 5 % | 6,07 % | 0,58 % | 1,07 % |
| e2c perf: procesador, 8 bandas dinámicas (96 kHz) | < 5 % | 10,06 % | 0,70 % | 1,27 % |

  La quinta, "perf: 8-band EQ core CPU load", pasa también bajo TSan (sus
  límites son más holgados).

### 8.2 Mutante D-M6: no era equivalente

- **Guarda afectada:** `started = false;` en `DynamicsOverlay::setBank`
  (`src/ui/DynamicsOverlay.cpp`). Con ella, el primer `advance` tras un
  cambio de banco no dibuja nada y toma `activationOffered = s.offered`; a
  partir de ahí solo se dibujan ofertas posteriores al cambio.
- **Por qué parecía redundante:** `setBank` también vacía el overlay
  (`clear()`), y toda instantánea de otro banco se rechaza por su etiqueta
  (`s.bank == bank`, mutante D-M4). Con audio sonando, la primera oferta
  del banco nuevo es dato real de ese banco, así que mostrarla un
  fotograma antes no es incorrecto.
- **Estado que alcanza la diferencia:** un cambio de ida y vuelta
  (MID → SIDE → MID) sin ninguna oferta entre medias (transporte parado
  dentro de los 100 ms de caducidad, o un bloque grande del host). La
  instantánea guardada sigue siendo de MID y de antes del primer cambio. Sin
  la guarda, al volver a MID se redibuja enseguida esa reducción antigua;
  con la guarda no se dibuja nada hasta la siguiente oferta.
- **Conclusión:** el comportamiento observable **no** es idéntico, así que
  el mutante no es equivalente. No se ha quitado código: se ha añadido la
  prueba "e4d D3 reinicio", que lo mata, y la guarda se queda tal cual.

### 8.3 Sincronía del analizador

Revisión de `SpectrumTap::copyAtStream` y `SpectrumView::advance`:

- **Defecto encontrado y corregido:** `SpectrumTap::prepare()` conservaba
  `streamEnd` de la generación anterior. Tras un `prepareToPlay`, y hasta
  la primera escritura nueva, `copyAtStream` aceptaba alinear con una
  posición de la generación vieja. Copiaba ceros (las muestras anteriores
  al inicio de la generación siempre se ponen a cero), así que nunca se
  dibujaba audio antiguo, pero aceptaba una alineación que no pertenecía al
  flujo actual. Ahora `prepare()` pone `streamEnd` a 0 (nunca coincide con
  `write`). La prueba "e4d sync prepare" fallaba antes y pasa ahora; el
  mutante F-M7, que deshace el cambio, se detecta.
- **Lecturas que se descartan:** `copyAtStream` devuelve falso (el editor no
  analiza PRE ese fotograma y lo reintenta en el siguiente) cuando: el par
  (escrito, posición) no es coherente; la posición pedida no está escrita
  todavía en PRE; las muestras ya no están en el anillo; o el escritor las
  ha sobrescrito durante la copia (seqlock). PRE solo se analiza si POST se
  copió en el mismo fotograma, y solo cuando su final cambia (no se vuelve
  a analizar una copia vieja como nueva).
- **Traza recién mostrada:** igual que en UI-2b, empieza sin historia (ceros
  antes del momento de activarse). Durante unos 170 ms (8192 muestras a
  48 kHz), PRE y POST coinciden muestra a muestra desde donde las dos
  tienen historia; es el arranque previsto, no un desalineamiento.

| Caso | Prueba | Resultado |
|---|---|---|
| Lectura entre las escrituras PRE y POST | e4d sync | alineado; la lectura normal iría un bloque por delante |
| Cambio de tamaño de bloque (16–2048, sin prepare) | e4d sync estrés, F4 (5 tamaños) | 0 desalineados |
| Transporte parado | e4d sync estrés | 0 análisis nuevos sin audio |
| Reinicio (`prepareToPlay`) | e4d sync prepare, estrés (78 reinicios) | 0 análisis con muestras del flujo anterior; posición reiniciada, generaciones PRE = POST |
| Cambio de sample rate (44,1 / 48 / 96 kHz) | estrés, F6/F7 | se reanaliza a la nueva frecuencia; 0 muestras antiguas |
| Varias instancias | estrés (2 intercaladas), e4d instances | nada compartido |
| Cambios rápidos MID / SIDE y Pre / Post / Pre + Post | estrés (400 pasos × 2) | 294 fotogramas Pre + Post alineados, 0 desalineados |
| Contadores tras prepareToPlay | e4d sync prepare, estrés | tap activo: posición = primer bloque; inactivo: 0 |

### 8.4 Revisión visual

Capturas reales del editor (procesador y editor, audio sintético) en
`build/e4d-captures/`. Selector del analizador: en Retina (2x) se lee bien
al 40, 60 y 100 %, y "MID Pre + Post" / "SIDE Pre + Post" caben sin
recortarse. En pantallas 1x al 40 % el texto mide unos 5 px, igual que
"Analyzer: Post" de STEREO y las pestañas desde UI-2b: no es un defecto
nuevo y no se ha cambiado el diseño.
