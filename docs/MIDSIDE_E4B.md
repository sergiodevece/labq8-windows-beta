# LABQ8 E4b — Motor DSP MID/SIDE

**Base:** `c9ea214` (E4a, SMOKE-7 PASS 10/10). **Estado:** cerrada.
Revisión técnica y revisión complementaria aprobadas el 11-11-2026; commit
local. Sin instalación: el AU de SMOKE-7 sigue instalado. El smoke físico
del motor M/S queda pendiente de autorización.

**Alcance:** motor DSP y pruebas. **Fuera de alcance (no implementado):**
botones MID/SIDE, cambios visuales, curvas dobles, telemetría MID/SIDE en
FFT y DYN, INTENTION, PHASE BEHAVIOR y SIDE artificial para fuentes mono.
Los 241 parámetros y el estado v5 no cambian (contrato congelado de E4a,
verificado por sus pruebas).

---

## 1. Arquitectura implementada

```
                 ┌─ ruta STEREO: core STEREO (estéreo, en sitio) ──────────────┐
 entrada L/R ────┤                                                              ├─ ruta escuchada → salida
 (in place)      └─ ruta MID/SIDE: M=(L+R)/2 → core MID (mono) ─┐               │   (la otra procesa una
                                  S=(L−R)/2 → core SIDE (mono) ─┴→ L=M+S, R=M−S ┘    copia: reserva)
                 Cambio de modo: fundido lineal de 20 ms entre las dos rutas
```

- **`dsp::StereoModeEngine`** (`src/dsp/StereoModeEngine.{h,cpp}`, sin JUCE):
  - **Tres `EqCore` independientes:** STEREO preparado con los canales del
    bus; MID y SIDE preparados en mono. Cada uno tiene sus propias bandas,
    rampas, crossfades de tipo, estados de filtro, detectores y balística. No
    se comparte nada.
  - **Búferes:** todos de tamaño fijo, de 256 muestras.
  - **Tiempo real:** sin asignaciones, bloqueos ni esperas.
- **Ruta STEREO:** el core STEREO en sitio. En Stereo, sin transición, es
  exactamente la llamada que hacía el procesador hasta E4a.
- **Ruta MID/SIDE** (`src/dsp/MidSide.h`):
  - Codifica `M = (L+R)/2`, `S = (L−R)/2` y decodifica `L = M+S`, `R = M−S`,
    con las sumas y restas en double y un único redondeo a float.
  - Cada detector lee la entrada de su propio banco (M o S, antes de sus
    bandas).
  - **Bus mono:** M es la señal y S = 0. MID procesa el canal en sitio; SIDE
    no tiene señal ni efecto (decisión aprobada).
- **Ruta plana (transparencia bit a bit):** si MID y SIDE están ociosos (todas
  las bandas son una identidad exacta y no hay dinámica en marcha), no se
  codifica y el búfer queda intacto.
  - Solo se **entra** en la ruta plana en una rejilla absoluta de 64 muestras
    contada desde `prepare()`, según el estado de los cores en ese instante.
    Así el resultado no depende de cómo trocea el host sus bloques.
  - Se **sale** de ella en cuanto un core deja de estar ocioso, lo que solo
    ocurre al cambiar los objetivos al inicio de un bloque.
- **Reserva:** la ruta que no se escucha sigue procesando una copia de la
  entrada, así que las dos rutas están siempre en régimen permanente
  (justificación medida en §6).
- **Cambio de modo:**
  - Fundido lineal de 20 ms, `out = a + w·(b − a)` en double, con
    `w = k/N`: una combinación convexa, como el cambio de tipo de `EqBand`.
  - Una petición de vuelta durante el fundido lo invierte en el sitio
    (`w → 1 − w`), sin salto.
  - El modo se lee en cada bloque del host, igual que el resto de parámetros.
- **Procesador:**
  - Lee los tres bancos con `params::bankOf`, en el orden STEREO, MID, SIDE.
    El banco STEREO recibe exactamente los mismos valores y en el mismo orden
    que antes.
  - Pasa `proc_mode` al motor.
  - Medidores IN/OUT y tomas PRE/POST: sin cambios; siempre miden la señal
    L/R final.
  - **Telemetría:** el editor sigue mostrando solo el banco STEREO, porque la
    telemetría M/S es E4d. Su dinámica solo se ofrece mientras se escucha la
    ruta STEREO sin transición. En M/S se indica "nada que mostrar", como en
    el bypass del host, para no dibujar reducciones STEREO sobre un sonido M/S.
- **Latencia:** cero. La cola no cambia.

### Precisión y normalización

- **Mono (L = R):** M = L exacto y S = 0 exacto. El banco MID trabaja al mismo
  nivel que el STEREO; con el mismo ajuste, el M/S da **los mismos bits** que
  STEREO sobre una señal mono (prueba D).
- **Contrafase (R = −L):** M = 0 exacto y S = L exacto.
- **Ida y vuelta codificada sin proceso:** error máximo de 1 ulp del canal
  mayor (−138,5 dB). Es exacta para pares mono, en contrafase y con un canal
  a cero (prueba F, 200 000 pares).
- **Niveles:** L sola, R sola, mono y contrafase a través de MID +6 dB y
  SIDE −6 dB dan las amplitudes analíticas `(g_M ± g_S)/2` con un error menor
  de 0,01 dB (prueba F).

---

## 2. Archivos modificados

| Archivo | Cambio |
|---|---|
| `src/dsp/StereoModeEngine.{h,cpp}` | **Nuevo:** motor de tres bancos, ruta plana, reserva y fundido |
| `src/dsp/MidSide.h` | **Nuevo:** codificador M/S |
| `src/plugin/PluginProcessor.{h,cpp}` | Usa el motor en vez de un `EqCore`: lee los tres bancos y el modo; telemetría solo con STEREO escuchado |
| `CMakeLists.txt` | `StereoModeEngine.cpp` en `labq8_dsp`; `tests/MidSideTests.cpp`; nuevo `labq8_ms_bench` |
| `tests/MidSideTests.cpp` | **Nuevo:** 17 pruebas de E4b (A–I y las dos de la revisión, §9) |
| `tests/E4aParameterTests.cpp` | Retirada la prueba solo de E4a "proc_mode se guarda pero no se procesa" |
| `tests/bench/MidSideBench.cpp` | **Nuevo:** estudio de transiciones (`--transitions`), análisis del nivel durante el fundido (`--crossfade-analysis`) y CPU (`--cpu`) |
| `tests/compat/CompatRender.cpp`, `run_compat.sh` | Base `e4a` (`c9ea214`); casos v5 en Stereo con MID/SIDE configurados y automatizados |
| `docs/MIDSIDE_E4B.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/MIDSIDE_E4A.md` | Documentación; cierre de E4a (SMOKE-7 PASS 10/10) |

**Sin cambios:** `src/dsp/EqCore`, `EqBand`, `BellBand`, `Dynamic*`,
`BiquadDesign`, `src/parameters/*` (IDs, orden, nombres, tipos, rangos, estado
v5), `src/ui/*`, `assets/`, `tools/` y el PNG maestro.

---

## 3. Resultados de las pruebas

**Suite completa (Release universal, `build/e4b-release-universal`):**
192/192 en arm64 y 192/192 en x86_64 (Rosetta), con las dos pruebas de la
revisión (§9). Hay 193 pruebas; la omitida es el generador de fixtures, que
solo corre con `LABQ8_WRITE_FIXTURES`. En el informe anterior, sin esas dos,
eran 190/190. Salidas: `build/e4b-report/tests-review_{arm64,x86_64}.txt`.

| Prueba | Qué demuestra | Resultado |
|---|---|---|
| **A** Identidad STEREO | En Stereo, el motor da los mismos bits que el core STEREO solo, con MID/SIDE aleatorios en reserva; mono y estéreo, bloques de 1, 37, 256 y 1000 | PASS |
| A (E4a) | La salida del procesador en Stereo es idéntica bit a bit tenga lo que tenga MID/SIDE: mono/estéreo, 3 frecuencias de muestreo, 2 tamaños de bloque | PASS |
| **B** Transparencia | M/S con bancos planos es exacto bit a bit: estéreo y mono, 44,1/48/96 kHz, 4 tamaños de bloque, con el banco STEREO activo o plano, y con todas las bandas en bypass (los seis tipos) | PASS |
| B Ruta plana | Al volver una banda a 0 dB, la salida vuelve a ser exactamente la entrada, en la misma muestra con cualquier tamaño de bloque | PASS |
| **C** Referencia | La salida M/S coincide bit a bit con una implementación de referencia (codificar, dos `EqCore` separados, decodificar), con dinámica, 3 frecuencias, 4 bloques | PASS |
| C Independencia | Cambiar todos los ajustes MID deja **idéntica bit a bit** la trayectoria del core SIDE (ganancias, reducciones, nivel del detector, coeficientes), y al revés. Un tono solo en M reduce solo MID; solo en S, solo SIDE | PASS |
| **D** Mono | Con L = R: L' = R' bit a bit con cualquier SIDE (incluido +24 dB, Q 18), el detector SIDE en silencio, y la salida es igual a la de STEREO con el mismo banco. En un bus mono, MID procesa y SIDE no influye | PASS |
| **E** Contrafase | Con R = −L: L' = −R' bit a bit, el detector MID en silencio y los ajustes MID no influyen | PASS |
| **F** Niveles | Amplitudes analíticas (< 0,01 dB); ida y vuelta del codificador ≤ 1 ulp, exacta en los casos especiales | PASS |
| **G** Cambio de modo | Procesador: idéntico a Stereo antes del cambio, la fórmula del fundido exacta y convexa durante 20 ms, exactamente M/S después; la inversión vuelve exactamente a Stereo; bloques de 64, 256, 333 y 1000 | PASS |
| G Bypass | Bypass del host: búfer intacto; al volver procesa de nuevo. Bypass de banda MID: vuelve a ser transparente. Telemetría STEREO solo con STEREO escuchado | PASS |
| **H** Estado v5 | Tres bancos distintos en Stereo y en M/S: valores restaurados (frecuencia y Q con unos pocos ulp en la primera ida y vuelta, como en v4) y sonido restaurado como punto fijo bit a bit; el fixture `e4a_custom_v5` suena como la referencia de sus bancos MID/SIDE | PASS |
| Robustez | El silencio sigue siendo cero exacto; NaN e Inf quedan contenidos; bloques de 1 muestra; cambios de frecuencia de muestreo (44,1 → 96 → 48 → 192 kHz) con fundido de 20 ms a cada una | PASS |
| **I** Estrés | 600 bloques de 1 a 2048 muestras, automatización aleatoria de los 241 parámetros y 200 escrituras de modo: salida finita y **0 asignaciones** | PASS |
| I Concurrencia | Parámetros (modo incluido) escritos desde otro hilo durante 3000 bloques de audio | PASS |

**Mutaciones:** 12 de 12 detectadas (`build/e4b-report/mutation.txt`), cada
una por al menos una prueba:

- bancos intercambiados;
- codificación sin el 1/2;
- signo de la decodificación;
- ruta plana con un solo core ocioso;
- sin reserva;
- peso del fundido desplazado;
- inversión ignorada;
- SIDE en el bus mono;
- parámetros SIDE enviados a MID;
- telemetría STEREO en M/S;
- decisión plana fuera de la rejilla;
- ruta STEREO alterada.

---

## 4. Independencia de los bancos: evidencias

1. **Objetos separados:** tres `EqCore`, cada uno con sus `EqBand` y
   `DynamicSection`. El motor no tiene estado compartido entre bancos, salvo
   los búferes de trabajo, que se sobrescriben en cada segmento.
2. **Trayectoria bit a bit:** prueba C, descrita en la tabla anterior.
3. **Referencia independiente:** prueba C. La salida M/S coincide con dos
   `EqCore` creados por separado, sin reserva ni banco STEREO.
4. **Señales sin componente:** mono, con SIDE inaudible (D), y contrafase,
   con MID inaudible (E), bit a bit.
5. **Parámetros:** cada core recibe solo los IDs de su banco (`bankOf`). La
   prueba de E4a "escribir cualquiera de los 241 cambia solo ese" sigue
   pasando, y la mutación M9 (SIDE enviado a MID) queda detectada.

---

## 5. CPU

`labq8_ms_bench --cpu` (`build/e4b-report/cpu_{arm64,x86_64}.txt`):

- **Condiciones:** carga = segundos de CPU / segundos de audio, en un núcleo
  y por instancia; mediana de 7 ejecuciones intercaladas de 2 s; ruido
  estéreo; 8 Bells dinámicas por banco activo.
- **Instancias:** "8 instancias" son 8 motores procesados en serie en el mismo
  hilo; la cifra es por instancia.
- **x86_64:** se ejecuta con Rosetta en esta máquina Apple Silicon.

| Caso (por instancia) | arm64 44,1 / 48 / 96 kHz | relativo | x86_64 44,1 / 48 / 96 kHz | relativo |
|---|---|---|---|---|
| Antes de E4b: core STEREO solo (referencia) | 0,233 / 0,256 / 0,512 % | ×1,00 | 0,435 / 0,474 / 0,978 % | ×1,00 |
| Stereo, MID/SIDE planos (**caso típico**) | 0,255 / 0,280 / 0,561 % | ×1,09–1,10 | 0,463 / 0,503 / 1,036 % | ×1,06 |
| M/S, STEREO plano | 0,399 / 0,441 / 0,827 % | ×1,61–1,72 | 0,586 / 0,628 / 1,224 % | ×1,25–1,35 |
| Stereo o M/S con los **tres bancos activos** (peor caso; reserva) | 0,62 / 0,68 / 1,31 % | ×2,56–2,67 | 1,01 / 1,09 / 2,18 % | ×2,23–2,32 |
| Cambios de modo cada 100 ms, todo activo | 0,62 / 0,69 / 1,32 % | ×2,57–2,67 | 1,01 / 1,09 / 2,18 % | ×2,23–2,33 |
| Todo plano | 0,035 / 0,037 / 0,075 % | ×0,15 | 0,040 / 0,042 / 0,085 % | ×0,09 |

Con 8 instancias el coste por instancia es el mismo (±1 %). **Peor caso medido:
2,18 % de un núcleo a 96 kHz en x86_64 (Rosetta)**, por debajo del presupuesto
del 5 % de las pruebas de rendimiento existentes.

El coste de la reserva es proporcional a lo que esté configurado en el banco
que no se escucha. Una sesión STEREO con MID/SIDE planos paga un 6–10 %: la
copia y los ticks de control de los cores ociosos.

---

## 6. Transiciones: estudio y elección

`labq8_ms_bench --transitions` (`build/e4b-report/transitions_arm64.txt`).

- **Referencia:** el cambio **ideal**, es decir, las dos rutas en régimen
  permanente desde t = 0 y la salida fundida de una a otra.
- **Cambio:** STEREO → M/S a 1,5 s, a 48 kHz.
- **"settle":** tiempo, tras el final del fundido, durante el que el error
  sigue por encima de −80 dBFS.

| Escenario | Política | Error de pico (dB rel.) | Peor desviación RMS en 5 ms | settle |
|---|---|---|---|---|
| Grave sostenido 40 Hz, campana +18 dB Q 10, bancos copiados | frío | −2,3 | **13,9 dB** | 2113 ms |
| | precalentamiento 20 ms | −2,9 | 11,6 dB | 2093 ms |
| | precalentamiento 80 ms | −5,3 | 7,5 dB | 2033 ms |
| | precalentamiento 320 ms | −14,5 | 1,8 dB | 1793 ms |
| | **reserva (motor)** | **exacto (0)** | **0** | **0** |
| EQ extrema (20 Hz +24 Q 18, cortes, shelf +24, notch), MID ≠ SIDE | frío / 20 / 80 / 320 ms | −27,8 / −29,4 / −30,1 / −32,2 | 0,83 / 0,69 / 0,61 / 0,48 dB | 2480 / 2460 / 2400 / 2160 ms |
| | **reserva** | **exacto** | **0** | **0** |
| Transitorios (bombos y clics), tres bancos distintos | frío / 20 / 80 / 320 ms | −47,7 / −51,5 / −63,5 / −113,1 | ≤ 0,11 dB | 157 / 137 / 77 / 0 ms |
| | **reserva** | **exacto** | **0** | **0** |
| Dinámica 100 Hz (ataque 100 ms, release 1 s), bancos copiados | frío / 20 / 80 / 320 ms | −2,0 / −4,4 / −11,4 / −33,8 | **11,0** / 9,1 / 4,9 / 0,4 dB | 628 / 608 / 548 / 308 ms |
| | **reserva** | **exacto** | **0** | **0** |

**Conclusiones:**

- **El precalentamiento no basta**, ni siquiera de 320 ms. Un IIR grave y
  estrecho (Q/(π·f) ≈ 0,3 s de constante de tiempo) y la balística de la
  dinámica (release de hasta 5 s) necesitan segundos para asentarse.
- **Por eso no hay precalentamiento:** la ruta no escuchada procesa siempre
  en reserva. La transición es exactamente la ideal en los cuatro escenarios.
- **Duración del fundido:** medida sobre la propia transición ideal, con la
  4ª diferencia como indicador de clic frente a la de la señal estable.

| Fundido | Grave copiado | EQ extrema | Transitorios | Dinámica copiada |
|---|---|---|---|---|
| instantáneo | −27,6 dB | −5,5 dB | −48,3 dB | **+55,1 dB** |
| 5 ms | −1,6 | −3,5 | −50,3 | +7,5 |
| 10 ms | −1,6 | −3,1 | −50,3 | +1,6 |
| **20 ms** | −1,6 | −3,0 | −50,3 | **−4,3** |
| 40 ms | −0,8 | −2,6 | −50,3 | −7,5 |

- **Se fija en 20 ms:** es el más corto de los probados que queda por debajo
  del clic propio de la señal en todos los escenarios. La desviación RMS
  durante el fundido es como máximo de 0,76 dB, y solo con bancos muy
  distintos.
- **Automatización rápida** (cambio cada 30 ms y cada 5 ms, más rápido que el
  fundido; bloques de 1, 64, 256 y 1000):
  - la salida es siempre finita;
  - **cada muestra queda entre las dos rutas** (desviación 0);
  - tras el último fundido, la salida es **exactamente** la ruta M/S;
  - **no hay cambio permanente de ganancia ni latencia.**
- **Artefacto inherente:** el fundido mezcla dos EQ distintas durante 20 ms;
  con bancos muy distintos hay una variación de nivel de hasta 0,76 dB en ese
  intervalo. Con bancos copiados no hay desviación de nivel (0,00–0,03 dB).

---

## 7. Compatibilidad (K)

`tests/compat/run_compat.sh all`: 13 bases (E0 … UI-2c y E4a `c9ea214`) × 2
arquitecturas. Salida: `build/e4b-report/compat-all.txt`.

**Las 13 bases dan OK en arm64 y en x86_64:**

- **E0 y E1b:** solo las diferencias ya documentadas (0 dB nominal en E0 y
  límite de Q en E1b).
- **E1a, E1b.1, E2a, E2b, E2c.1, UI-1, UI-1.1, UI-2a, UI-2b, UI-2c y E4a:** sin
  ninguna diferencia.
- **Casos de dinámica:** idénticos bit a bit en las 7 bases que la tienen
  (56 de 56).
- **Casos v5 en Stereo con MID/SIDE configurados y automatizados contra
  `c9ea214`:** idénticos bit a bit (4 de 4).

Casos nuevos (desde base E4a): sesión v5 con todos los parámetros MID/SIDE
fuera de su valor por defecto y `proc_mode` en Stereo, sin automatizar y con
MID/SIDE automatizados (ganancia, frecuencia, tipo, dinámica) durante la
reproducción. Deben ser idénticos bit a bit: la ruta M/S en reserva nunca
toca el sonido STEREO.

---

## 8. Riesgos pendientes

1. **CPU de la reserva:** con los tres bancos activos el coste es unas 2,2–2,7
   veces el del motor anterior (peor caso medido 2,18 % de un núcleo). Si se
   quisiera reducir, la alternativa sería no mantener en reserva el banco
   STEREO mientras se escucha M/S, o al revés, a costa de transiciones con
   arranque en frío, que §6 muestra inaceptables. Se mantiene la reserva.
2. **La interfaz aún no sabe de M/S** (E4c). El editor sigue editando el banco
   STEREO y no tiene control de modo. En M/S, la curva de pantalla es la del
   banco STEREO, que no se escucha, y no se dibuja ninguna dinámica. El modo
   solo puede cambiarse por automatización o con el editor genérico del host.
   **Un smoke físico de E4b debe tenerlo en cuenta.**
3. **Tamaño de bloque y ruta plana:** el resultado es independiente del
   troceo del host con los parámetros constantes. Un cambio de parámetro se
   aplica al inicio del bloque del host, como siempre.
4. **Subnormales:** con `ScopedNoDenormals` (FTZ/DAZ), una entrada subnormal
   en la ruta codificada puede salir como cero. Es inaudible (< 1,2·10⁻³⁸) y
   la ruta plana no codifica.
5. **Fundido de bancos muy distintos:** la variación de nivel de hasta 0,76 dB
   durante 20 ms es inherente a cualquier fundido lineal entre dos EQ
   diferentes. Un fundido de potencia constante sería peor con señales
   correlacionadas, que es el caso aquí.
6. **auval y hosts:** no se ha instalado ni validado en Logic (no autorizado
   en E4b). Bypass del wrapper VST3 en el índice 241: sin cambios respecto a
   E4a.

---

## 9. Revisión final (complemento)

Ninguna comprobación ha requerido cambiar el código del producto. Los únicos
cambios desde el informe son dos pruebas nuevas, un modo de análisis en el
banco y este documento. El código de `src/` y `assets/` no ha cambiado desde
la ejecución de compatibilidad, así que esa evidencia sigue siendo válida y
no se ha repetido.

### 9.1 ThreadSanitizer

- **Build:** `build/e4b-tsan` (RelWithDebInfo, `-fsanitize=thread`, arm64).
  El binario enlaza `libclang_rt.tsan_osx_dynamic.dylib`, y un programa con
  una carrera deliberada genera su aviso, lo que confirma que el detector
  está activo.
- **Pruebas ejecutadas:**
  - las 17 de E4b, incluida la de concurrencia, en la que parámetros y modo
    se escriben desde otro hilo durante 3000 bloques de audio;
  - las 7 de telemetría dinámica, la del medidor y las 6 de las tomas del
    analizador.
- **Resultado:** 31/31 pasan, **0 avisos**. Salidas en
  `build/e4b-report/tsan_*.txt`.

### 9.2 Sin asignaciones, bloqueos ni esperas en el hilo de audio

- **En ejecución:** un arnés aislado carga el procesador real con una
  biblioteca de interposición (`DYLD_INSERT_LIBRARIES`). Cuenta, **solo en el
  hilo de audio y solo dentro de `processBlock` / `processBlockBypassed`**,
  las llamadas a:
  - `malloc`, `calloc`, `realloc`, `free` y `posix_memalign` (incluidos
    `new` y `delete`);
  - `pthread_mutex_lock` / `trylock`, `pthread_rwlock_*`, `pthread_cond_wait`
    / `timedwait` y `os_unfair_lock_lock`;
  - `dispatch_semaphore_wait`, `semaphore_wait`, `nanosleep`, `usleep` y
    `sched_yield`.

  La autoprueba confirma que detecta un `malloc` y un mutex. Condiciones:

  - 4000 bloques de 1 a 2048 muestras;
  - los tres bancos activos;
  - 572 escrituras de modo, con fundidos e inversiones;
  - un 6 % de bloques en bypass del host;
  - cambio de 48 a 96 kHz;
  - telemetría y tomas del analizador activas.

  **Resultado en arm64 y x86_64: 0 llamadas de todos los tipos**
  (`build/e4b-report/rt-audit_*.txt`).
- **Estático:** el objeto `StereoModeEngine.cpp.o` solo referencia métodos de
  `EqCore`, `memcpy`, `lround` y la protección de pila. Los búferes son
  `std::array` de tamaño fijo, y `MidSide.h` es aritmética pura.
- **Prueba permanente:** "e4b I: stress" cuenta las asignaciones de C++ con
  `AllocationGuard`, con el mismo resultado: 0.

### 9.3 La variación de 0,76 dB durante el fundido

`labq8_ms_bench --crossfade-analysis`
(`build/e4b-report/crossfade-analysis_arm64.txt`).

- **Escenario:** "EQ extrema", el único donde aparece. El banco SIDE es muy
  distinto del STEREO: −24 dB a 250 Hz con Q 18 y +24 dB a 16 kHz con Q 18,
  frente a un shelf de +24 dB a 12 kHz, un corte, un notch y +24 dB a 20 Hz.
  Las dos rutas solo se parecen parcialmente: **correlación ρ = 0,74** dentro
  del fundido, y la ruta B suena 2,7 dB más baja que la A.
- **Método:**
  - nivel RMS de la salida en ventanas deslizantes de 5, 20 y 50 ms dentro
    del fundido;
  - comparado con el nivel interpolado entre los de las dos rutas en esa
    ventana;
  - más el ruido propio de la medida (la misma ventana desplazada un paso
    sobre una sola ruta).
- **Por qué ocurre:** un fundido lineal de dos señales con correlación ρ
  tiene en w = ½ una potencia de (Pa + Pb + 2ρ√(PaPb))/4. Con ρ = 0,74 la
  teoría predice **−0,60 dB**. Se mide **−0,78 dB** con ventanas de 5 ms, a
  10,6 ms del inicio, es decir, en el punto medio, con un ruido de medida de
  ±0,28 dB, y −0,47 dB con ventanas de 20 ms. El −0,76 dB con 50 ms es un
  artefacto de la métrica: la ventana, centrada en el inicio del fundido,
  incluye la ruta B, que es más baja.
- **Otros escenarios:**

  | Escenario | ρ | Previsto |
  |---|---|---|
  | Bancos copiados | 0,99–1,00 | 0,00–0,02 dB (medido ≤ 0,06) |
  | Transitorios | 0,92 | −0,17 dB |

- **Significado perceptivo:** es una caída única de algo menos de 1 dB,
  centrada en el punto medio y de unos 10 ms. Está por debajo de las
  diferencias de nivel apreciables en sonidos sostenidos (del orden de
  0,5–1 dB, comparando estímulos de cientos de ms). Solo aparece al cambiar
  de modo entre bancos muy distintos, cuando el usuario ya está cambiando el
  sonido a propósito.
- **Es la misma ley** que el cambio de tipo de filtro aprobado en E1b. La
  alternativa de potencia constante subiría unos +3 dB con bancos copiados
  (ρ = 1), que es el caso más habitual, así que se mantiene el fundido lineal.
- **Pendiente de confirmar a oído** en el smoke físico.

### 9.4 La reserva bajo automatización rápida, transporte y frecuencia de muestreo

**Prueba nueva:** "e4b standby", en las dos direcciones, Stereo → M/S y
M/S → Stereo.

- **Montaje:** dos procesadores reciben la misma entrada y los mismos
  eventos. El procesador X escucha un modo y cambia al otro al final; el
  procesador Y escucha ese otro modo desde el principio.
- **Eventos durante 520 bloques:**
  - automatización de 6 parámetros de los tres bancos en cada bloque;
  - 10 bloques en bypass del host;
  - 20 bloques sin ninguna llamada (transporte parado);
  - `prepareToPlay` de 48 a 96 kHz a mitad de la secuencia.
- **Resultado:** tras el fundido, la salida de X es **idéntica bit a bit** a la
  de Y (más de 30 bloques comparados). Es decir, la ruta en reserva vio
  exactamente lo mismo que la escuchada. PASS en arm64 y x86_64, y sin avisos
  de TSan.
- **Interpretación:**
  - el bypass del host y las pausas detienen las dos rutas a la vez;
  - un cambio de frecuencia de muestreo las reinicia a la vez, con fundido
    recalculado a 20 ms (prueba de robustez);
  - las pruebas G ya cubrían los cambios de modo cada 5 y 30 ms con bloques
    de 1 a 1000.

### 9.5 Las rutas rápidas no omiten trabajo pendiente

- **Cuándo un core está "ocioso":**
  - todas sus bandas son una identidad: ganancia 0 dB **ya alcanzada** (la
    rampa ha terminado), sin modulación, sin fundido de tipo ni petición
    pendiente, y con un estado residual < 10⁻⁹;
  - **y** ningún detector está activo (`EqCore::isIdle`).
- **Cuándo cambia ese estado** (verificado en el código):
  - una banda deja de estar ociosa en `setTargets`, al inicio del bloque;
  - un fundido de tipo empieza en `setTargets`;
  - la dinámica se activa en el acto en `setTargets` (`detectorActive = true`
    en `updateEnable`) y solo se desactiva en un tick, al acabar su fundido
    de 20 ms.

  En ningún caso un core pasa a estar activo dentro de un segmento plano.
- **En la ruta plana los cores se siguen procesando:** avanzan sus ticks,
  rampas y rejilla, y solo se omite la codificación, que no tiene ningún
  efecto sobre bancos ociosos.
- **Prueba nueva:** "e4b flat path". Con bancos planos, en una muestra fuera
  de la rejilla de 64, se activa dinámica en una Bell a 0 dB, ganancia en
  SIDE, un cambio a Low Cut o se quita el bypass de un Notch. En los cuatro
  casos:
  - el motor sale de la ruta plana **en ese mismo bloque**;
  - desde ese momento la salida es **idéntica bit a bit** a una referencia
    que codifica todas las muestras;
  - antes del cambio difiere de ella como máximo en 1 ulp, el error del
    codificador.

  La mutación "ruta plana con un solo core ocioso" queda detectada.
- **Bus mono:** no hay ruta plana aparte. El core MID se procesa siempre en
  sitio, y un core ocioso deja el canal intacto por sí mismo.

### 9.6 y 9.7 Estado de Git y AU instalado

- **Git:**
  - HEAD `c9ea214` y nada preparado para commit.
  - 9 archivos modificados y 6 nuevos.
  - `tests/smoke ui_01/` (19 archivos, mismos hashes) y `tests/smoke_m:s/`
    (10 archivos) siguen sin seguimiento y no se tocan.
  - Diff de revisión: `build/e4b-report/e4b.diff`.
- **AU instalado:** `~/Library/Audio/Plug-Ins/Components/LABQ8.component`
  sigue siendo el de SMOKE-7 (`f274c663…`): todos sus archivos son idénticos
  al build `e4a-smoke-release-universal`. No hay ningún VST3 instalado, y la
  copia de SMOKE-6 sigue en `build/e4a-smoke-report/`.
