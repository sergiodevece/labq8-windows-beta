# UI-1: implementación de la interfaz aprobada

**Estado:** UI-1 cerrada (`13a810f`). **UI-1.1** (§9) cambia la ergonomía
tras SMOKE-1: zoom, botones de banda ON/OFF y edición desde los nodos;
**cerrada**, aprobada en SMOKE-2 y SMOKE-3 (§9.1, §9.5). **UI-2a** (§10)
añade los medidores IN/OUT reales (cerrada, SMOKE-4 aprobado, §10.1). Lo que
UI-1.1 y UI-2a sustituyen está señalado en cada apartado.

UI-1 sustituye el editor genérico de JUCE por el diseño aprobado
**LABQ8_UI_DESIGN_APPROVED_01**
([docs/design/](design/README.md), SHA-256 `4e14b925…f30b01`).

- **Sin cambios de DSP:** el motor de E2c.1 no cambia, ni sus parámetros, ni el
  estado.
- **El PNG maestro queda intacto:** no se modifica ni se incrusta en el plugin.

## 1. Arquitectura

| Capa | Qué es | Dónde |
|---|---|---|
| A. Gráficos estáticos | `assets/ui/background.png`, derivado del maestro (§2), incrustado con `juce_add_binary_data` | `assets/ui/`, `tools/ui/GenerateUiAssets.cpp` |
| B. Controles interactivos | 7 potenciómetros, Filter Type, interruptor Dynamic EQ, 8 botones ON/OFF de banda, nodos y tiradores de Q de la pantalla, icono de configuración (zoom) | `src/ui/LabQ8Editor.cpp` |
| C. Valores y estados | Displays numéricos, cabecera "● BAND n" (indicador de color), posición de los potenciómetros | `src/ui/LabQ8Editor.cpp` |
| D. Visualización | Respuesta estática real del EQ y nodos, calculados con los mismos diseños, límites y Q efectivo que el DSP | `src/ui/UiModel.cpp` |

- **Espacio de referencia:**
  - todo se dibuja en el espacio de **1672 × 941** del maestro, con las
    coordenadas medidas en `src/ui/UiGeometry.h`;
  - ese archivo lo comparten el generador de assets y el editor, así que lo
    que se borra del fondo y lo que se dibuja encima coinciden siempre;
  - un componente `Content` contiene todo y se escala con una única
    transformación.
- **Tamaño del editor:**
  - seis tamaños fijos con la proporción 1672:941, elegidos en el menú Zoom
    (40 / 60 / 80 / 100 / 120 / 140 %); sin tirador de esquina ni
    redimensionado por el host (§9.4; UI-1 abría al 75 % con esquina);
  - abre al **60 %** (1003 × 565) o al último tamaño elegido (preferencia
    global, §9);
  - las zonas de clic escalan con el dibujo.
- **Conexión con los parámetros:**
  - los potenciómetros usan `SliderAttachment`;
  - Filter Type, Dynamic EQ, los botones ON/OFF y el indicador de "BAND n"
    usan `ParameterAttachment`, con gestos completos de inicio y fin;
  - los arrastres en la pantalla escriben los parámetros con un gesto de
    inicio y fin por parámetro (§9).
- **Selección de banda:**
  - se hace desde los nodos de la pantalla (UI-1.1);
  - vuelve a conectar los diez controles del panel (siete potenciómetros,
    Filter Type, Dynamic EQ y el indicador) a los parámetros de la banda
    elegida;
  - crear o destruir las conexiones nunca escribe un parámetro;
  - la banda elegida se guarda en el procesador (`uiSelectedBand`), así que
    sobrevive a cerrar y abrir el editor;
  - no es un parámetro, no se guarda en la sesión y no afecta al audio.
- **Separación del audio:** el editor solo lee y escribe parámetros públicos.
  No llama al DSP, no reserva memoria en el callback y no añade
  sincronización con el hilo de audio.
- **Repintado:** un temporizador de 30 Hz compara los 40 parámetros estáticos
  y repinta la pantalla solo si cambian, sea por automatización del host o
  desde el panel.
- **Fondo:** el editor lo pinta re-muestreado una sola vez por tamaño y
  densidad de pantalla, y después solo lo copia.

## 2. Assets derivados (reproducibles)

```bash
./build/<config>/labq8_ui_assetgen_artefacts/<Config>/labq8_ui_assetgen docs/design/LABQ8_UI_DESIGN_APPROVED_01.png assets/ui
```

| Archivo | SHA-256 | Contenido |
|---|---|---|
| `background.png` | `176895c9…40d69` | El maestro sin los elementos que dependen del estado, con las escalas re-rotuladas |
| `dynamic_switch_on.png` | `f90fc1d5…d85c23f` | El interruptor del maestro (encendido), recortado a la forma de la píldora |
| `dynamic_switch_off.png` | `3409d491…846f8` | El mismo, reflejado y desaturado (apagado) |
| `meter_lit.png` | `7bc0aa5f…ed4d4` | UI-2a: las celdas encendidas de los cuatro medidores del maestro, con su brillo (x 1389–1550, y 302–477) |

**Reproducibilidad:** los tres archivos son idénticos byte a byte generados en
Debug, en Release arm64 y en Release x86_64.

**Operaciones del generador** (todo lo demás son píxeles del maestro sin
tocar):

1. **Punteros de los potenciómetros:** borrados por interpolación angular sobre
   la tapa. El maestro dibuja dos punteros en Release; se borran ambos.
2. **Arcos verdes de valor:** borrados reflejando la mitad derecha del anillo,
   que es oscura.
3. **Dígitos de los siete displays:** borrados por interpolación por filas.
4. **"● BAND 4" y el contenido de Filter Type:** borrados. La flecha del
   desplegable se conserva.
5. **Selectores:** el botón 4 (seleccionado en el maestro) se sustituye por el
   botón 5; los puntos y números se borran en los ocho.
6. **Pantalla:** la curva, los nodos, las etiquetas y el espectro ilustrativo
   se eliminan. El fondo del gráfico se re-sintetiza y la rejilla se repone.
7. **Medidores IN/OUT:** los niveles ilustrativos pasan a segmentos apagados.
8. **Escalas re-rotuladas:**
   - GAIN −24/+24, Q 0.1/18, THRESHOLD −80/+24, ATTACK 0.5/500 y RELEASE
     5/5000;
   - **eje de la pantalla:** +24, +16, +8, −8, −16, −24 (el "0" del maestro se
     conserva);
   - los números nuevos se centran sobre la tinta antigua (en el eje,
     alineados a su borde derecho);
   - color muestreado del original y altura de tinta calibrada (Helvetica Neue
     Medium).
9. **Interruptor Dynamic EQ:**
   - la píldora del maestro se borra del fondo, porque el editor dibuja el
     estado;
   - los sprites se recortan a la forma de la píldora (canal alfa con borde
     suave de 1,5 px), lo que elimina el borde residual del estado apagado.

## 3. Controles y parámetros

| Control (diseño) | Parámetro (banda n seleccionada) | Rango público | Display / estado |
|---|---|---|---|
| FREQUENCY | `b{n}_freq` | 20 Hz – 20 kHz, logarítmico | "820 Hz", "2.50 kHz" |
| GAIN | `b{n}_gain` | −24 … +24 dB | "-4.2 dB" |
| Q | `b{n}_q` (guardado) | 0,1 … 18, logarítmico | "1.2" / "0.71" |
| Filter Type | `b{n}_type` | 6 tipos (menú) | icono + nombre |
| Dynamic EQ | `b{n}_dyn_on` | off / on | interruptor; deshabilitado si el tipo no es Bell |
| THRESHOLD | `b{n}_threshold` | −80 … +24 dBFS | "-20 dB" |
| RANGE | `b{n}_range` | 0 … 24 dB | "8.0 dB" |
| ATTACK | `b{n}_attack` | 0,5 … 500 ms | "10 ms" / "0.5 ms" |
| RELEASE | `b{n}_release` | 5 … 5000 ms | "120 ms" |
| Botón 1–8 (UI-1.1) | `b{n}` de ese botón: `b{n}_bypass` | off / on | lámpara encendida = activa; apagada = bypass |
| Punto de color junto a "BAND n" | `b{n}_bypass` (solo lectura) | | iluminado = activa; atenuado = bypass |
| Nodo en la pantalla (UI-1.1) | selección; `b{n}_freq`, `b{n}_gain` | | anillo y tiradores de Q |
| Tiradores de Q (UI-1.1) | `b{n}_q` | 0,1 … Q efectivo máximo del tipo | |

**Potenciómetros:**

- **Posición:** la fracción normalizada del parámetro, la que ve el host. El
  recorrido va de 222° a 138° (276°).
- **Ratón:** arrastre (240 px para todo el recorrido) y rueda.
- **Doble clic:** vuelve al valor por defecto.

**Bypass por banda** (UI-1.1: los botones 1–8, §9; en UI-1 se hacía con un
clic en el punto de "BAND n", que ahora es solo indicador):

- **Aspecto del indicador de "BAND n":**
  - **banda activa:** el punto iluminado del diseño;
  - **en bypass:** relleno al 18 % y contorno al 55 % del color de la banda, y
    el nodo de la pantalla al 35 %.

**Dynamic EQ en tipos no Bell:**

- **Por qué:** la dinámica solo actúa sobre Bell (DSP, E2b).
- **Aspecto:** con Low/High Shelf, Low/High Cut o Notch, el interruptor se
  muestra deshabilitado (píldora apagada al 35 %), con el aviso "Dynamic EQ:
  Bell bands only".
- **Comportamiento:** no responde a clics, y `b{n}_dyn_on` no se modifica.
- **Al volver a Bell:** se muestra y se puede usar el estado guardado.
- **Automatización:** también aplica cuando el host cambia el tipo.

## 4. Elementos sin implementación (inactivos)

Conservan su aspecto y posición. Absorben el clic sin efecto y muestran un
aviso emergente provisional: "… not available yet".

| Elemento | Motivo |
|---|---|
| STEREO MODE (STEREO / M/S / L/R) | Sin M/S ni L/R en el DSP |
| POLARITY | Sin inversión global de polaridad |
| INTENTION | Sin INTENTION ni presets. **El texto "Mix Bus - Glue" es ilustrativo, del diseño**: no representa un preset cargado. Su presentación provisional se resolverá antes del smoke definitivo |
| Pestañas EQ / SPECTRUM / DYN / ALL, Analyzer, View, ajustes | Sin analizador FFT ni vistas |
| Phase Behavior ("Minimum Phase") | Investigación futura; el DSP actual es de fase mínima |
| Medidores IN/OUT | ~~Apagados~~: reales desde UI-2a (§10) |

**En la pantalla:**

- **Se dibuja:** solo lo real, es decir, la respuesta estática calculada a
  partir de los parámetros (escala ±24 dB) y los nodos.
- **No se dibuja:** espectro, curva dinámica ni las etiquetas "[n]".

## 5. Decisiones visuales aprobadas (29-10-2026)

**Aprobado:**

- chasis, composición y controles de APPROVED_01;
- rotulación corregida de los parámetros;
- pantalla de ±24 dB;
- los seis iconos de filtro;
- Helvetica Neue;
- arcos y punteros dinámicos;
- colores de selectores y nodos;
- estado visual de las bandas en bypass;
- tooltips provisionales.

**Integrado tras la aprobación:**

- escala de la pantalla en ±24 dB;
- bypass desde el indicador de "BAND n";
- Dynamic EQ deshabilitado en tipos no Bell;
- corrección del borde del interruptor apagado.

**Sin decidir:**

- el display de Q muestra el Q guardado; el efectivo (E1b.1) sigue pendiente;
- con Cut o Notch, GAIN muestra la ganancia guardada, aunque esos tipos la
  ignoran;
- la presentación provisional de INTENTION (§4).

## 6. Verificación

**Pruebas:** 118 en total; 11 de ellas son de la interfaz
(`tests/EditorTests.cpp`):

- formatos, ejes (±24 dB) y respuesta dibujada igual a la del núcleo
  (≤ 1e-6 dB);
- abrir y cerrar 30 veces, tamaño, proporción y zonas de clic a 4 escalas;
- seleccionar bandas no cambia ningún parámetro;
- las ediciones solo llegan a la banda seleccionada;
- la automatización del host actualiza el panel;
- el audio sale idéntico bit a bit con el editor abierto, cerrado y cambiando
  de banda;
- cargar un estado restaura los 80 parámetros;
- los controles inactivos no cambian nada;
- **bypass desde el indicador:**
  - solo afecta a la banda mostrada, con un gesto de inicio y uno de fin;
  - la zona de 34 × 34 px responde en las esquinas;
  - el clic derecho se ignora;
  - es independiente de la selección;
- **bypass desde fuera:** la automatización, el guardado y la carga actualizan
  el indicador;
- **Dynamic EQ:**
  - deshabilitado en los cinco tipos no Bell, sin cambiar `b{n}_dyn_on` ni con
    la llamada directa ni con el clic;
  - se recupera al volver a Bell, también cuando el tipo lo cambia el host.

**Mutaciones**, todas detectadas:

| Mutación | Pruebas que fallan |
|---|---|
| Panel siempre conectado a la banda 1 | 4 |
| Seleccionar escribe un parámetro | 3 |
| Filter Type conectado a otra banda | 1 |
| El indicador conmuta siempre la banda 1 | 2 |
| El interruptor ignora el tipo de filtro | 1 |

La última solo se detectó después de corregir la prueba, que alternaba dos
veces y se compensaba.

**Compatibilidad de audio:** estricta frente a E2c.1, **62/62 idénticas bit a
bit** en arm64 y x86_64.

**Comparación con el maestro** (`labq8_ui_snapshot`): diferencia media por
canal (0–255):

| Zona | Diferencia | Comentario |
|---|---|---|
| Imagen completa | 3,45 | |
| Chasis superior | 0,00 | Idéntico |
| Cabecera | 1,56 | |
| Potenciómetros y displays | 3,15 | Valores reales y escalas corregidas |
| Selector | 4,42 | |
| Pantalla | 15,4 | Curva real en ±24 dB; sin espectro ilustrativo |
| Medidores | 12,8 | Apagados |

**Rendimiento gráfico:** un repintado completo cuesta 1,5–1,7 ms al 75 % y al
100 % (Release arm64, renderizador software, fondo pre-escalado). El editor no
añade trabajo al hilo de audio.

## 7. Capturas finales

`labq8_ui_snapshot <maestro> <dir> --final` genera:

| Captura | Qué muestra |
|---|---|
| `final_1_band_active.png` | Banda 4 activa (Bell, Dynamic EQ encendido) y banda 2 a +21 dB en la pantalla de ±24 dB |
| `final_2_band_bypassed.png` | Banda 5 en bypass y seleccionada: indicador atenuado, nodo al 35 % |
| `final_3_low_shelf_dynamic_disabled.png` | Banda 4 en Low Shelf con `b4_dyn_on` guardado encendido: interruptor deshabilitado |

Sin `--final`, la herramienta genera `editor_100.png`, `editor_75.png`,
`compare.png` y `difference.png`, que comparan el editor con el maestro.

## 8. Archivos

| Archivo | Contenido |
|---|---|
| `src/ui/UiGeometry.h` | Geometría medida |
| `src/ui/UiModel.{h,cpp}` | Formatos, ejes y respuesta |
| `src/ui/LabQ8Editor.{h,cpp}` | El editor |
| `tools/ui/GenerateUiAssets.cpp` | Generador de assets |
| `tools/ui/UiSnapshot.cpp` | Capturas y comparación |
| `assets/ui/*.png` | Assets derivados |
| `tests/EditorTests.cpp` | Pruebas del editor |

**Cambios en archivos existentes:**

- `PluginProcessor.{h,cpp}`: crea el editor y guarda la banda seleccionada.
- `CMakeLists.txt`.
- `tests/compat/CMakeLists.txt` y `run_compat.sh`.

## 9. UI-1.1: ergonomía tras SMOKE-1

**Estado:** **cerrada** sobre `13a810f`; propuestas visuales aprobadas el
30-10-2026; **SMOKE-2 y SMOKE-3 en Logic aprobados** (§9.1, §9.5); UI-1.1
aprobada por el usuario. Quedan por registrar en Logic las comprobaciones de
§9.2. Sin cambios de DSP, parámetros, IDs, estado ni PNG maestro.

**Alcance:** UI-1.1 es solo ergonomía de la interfaz existente. M/S,
INTENTION y PHASE BEHAVIOR no forman parte de ella: su dirección de producto
está en [specs/LABQ8_INTENTION_PHASE_MS_v01.md](specs/LABQ8_INTENTION_PHASE_MS_v01.md),
sin autorización de implementación, y sus controles siguen inactivos (§4).

**Zoom** (versión vigente tras SMOKE-2 en §9.4; esta es la de SMOKE-2):

- del 50 % al 100 %, proporción fija; abre al **60 %** (1003 × 565);
- **preferencia global del usuario**, nunca en el estado del plugin:
  `~/Library/Application Support/VitaPlug/LABQ8/LABQ8.settings`
  (`src/ui/UiPreferences.{h,cpp}`);
- **acceso principal:** el icono de engranaje de la cabecera de la pantalla
  (zona de clic de 32 × 32 px sobre el icono del fondo, con tooltip): menú
  "Zoom", el actual marcado;
- **acceso adicional:** el mismo menú con clic derecho (o ctrl-clic) en el
  chasis o en la pantalla;
- se mantenía el tirador de esquina de JUCE (retirado en §9.4).

**Botones de banda 1–8:** interruptor ON/OFF de su propia banda
(`b{n}_bypass`), sin selección. Lámpara vintage encendida = activa, apagada =
bypass (colores más profundos y cálidos que los de la pantalla). Clic
izquierdo, al pulsar, con gesto completo; el clic derecho no hace nada. Cada
botón queda conectado a su banda para siempre, así que también sigue a la
automatización y a la carga de estado. El punto de "BAND n" es solo
indicador: no recibe el ratón.

**Pantalla:**

- **clic en un nodo:** selecciona la banda (también en bypass) y muestra un
  anillo y dos tiradores de Q; el nodo más cercano dentro de 11 px gana;
- **arrastre del nodo:** horizontal = FREQUENCY, vertical = GAIN (37 px =
  8 dB; solo Bell y shelves), diagonal = ambos; relativo al punto de partida;
  un temblor de menos de 3 px solo selecciona; cada parámetro tiene un gesto
  que empieza con el primer cambio y termina al soltar; un eje que no se mueve
  no se escribe;
- **tiradores de Q:** desde el Q efectivo; hacia fuera baja Q (la mitad cada
  45 px), hacia dentro sube; limitado a [0,1, Q efectivo máximo del tipo];
  empujar contra un límite no escribe nada;
- **posición de los tiradores:** Bell en los puntos de media ganancia en dB
  (definición de ancho de RBJ), Notch en sus puntos de −3 dB, con una línea
  punteada; shelves y cortes no tienen ese ancho, así que los tiradores son un
  control de Q puro (de 14 a 48 px del nodo según el Q, escala logarítmica en
  su rango efectivo), sin línea; nunca a menos de 14 px del nodo;
- **ocultar:** clic en la pantalla vacía o en el chasis; clic en otro nodo
  cambia de banda;
- sin gestos con teclas modificadoras.

**Revisión del enrutado del ratón** (los fallos de SMOKE-1 siguen sin causa
confirmada):

- con el editor visible, `getComponentAt` lleva cada botón, el engranaje, la
  pantalla a su componente en cada zoom (prueba);
- el punto de "BAND n" de UI-1 recibía el clic y su lógica era correcta fuera
  de Logic; no se reprodujo el fallo;
- no se pudieron inyectar eventos reales del sistema (la ventana de prueba
  queda tapada por otras aplicaciones), así que el comportamiento dentro de
  Logic solo se comprobará físicamente;
- los botones nuevos conmutan al pulsar, sin depender de dónde se suelta el
  ratón.

**Verificación:**

- 124 pruebas (6 nuevas de UI-1.1) en Release arm64 y x86_64: 124/124;
- compatibilidad de audio: `run_compat.sh all`, ocho bases en arm64 y x86_64,
  todas OK; frente a UI-1 (`13a810f`), estricta, 31/31 casos idénticos bit a
  bit por arquitectura;
- mutaciones, todas detectadas: botones conectados a la banda 1 (2 pruebas),
  indicador que recibe clics (1), arrastre vertical que reescribe la
  frecuencia (1), Q hasta 18 en todos los tipos (1), tiradores a distancia
  fija (1), zoom no guardado (1), sin umbral de temblor (1), un gesto por
  movimiento (2);
- repintado completo (Release arm64, software): 3,9–4,3 ms a 1254 px y
  4,3–5,0 ms a 1672 px, frente a 3,0–3,5 ms de UI-1 medido en la misma
  sesión (las lámparas añaden unos 1 ms; los repintados normales son locales).

**Capturas** (`labq8_ui_snapshot <maestro> <dir> --ui11`):
`ui11_1_editor_100.png`, `ui11_2_editor_60_default.png`,
`ui11_3_editor_50.png`, `ui11_4_buttons_mixed_on_off.png`,
`ui11_5_q_handles.png`.

### 9.1 SMOKE-2 en Logic Pro (30-10-2026)

Build instalado: `build/ui11-release-universal` (árbol de trabajo sobre
`13a810f`), SHA-256 del binario
`bb3c0bc6defa235a6639b10d6b451eae8b804b4e551ad2e067d30671ff63f035`; auval
arm64 y x86_64 sin avisos, 80 parámetros. Resultados aprobados por el usuario:

- **EQ estática y dinámica:** resultado musical excepcional con bajo DI,
  batería, guitarra acústica y mezcla completa. El DSP queda aprobado
  musicalmente y no se modifica;
- **tamaño inicial del 60 %:** correcto;
- **botones coloreados ON/OFF:** funcionan en Logic (el fallo de SMOKE-1 en
  el bypass no se reproduce con los botones nuevos; su causa sigue sin
  confirmar);
- **selección de bandas desde sus nodos:** aprobada;
- **tiradores gráficos de Q:** aprobados como parte de la identidad de LABQ8;
- **superposición de bandas:** se mantiene el comportamiento actual (los
  nodos pueden superponerse; el clic elige el más cercano dentro de 11 px, sin
  reordenar ni apartar nodos).

### 9.2 Comprobaciones pendientes tras SMOKE-2

Las dos primeras quedaron superadas en SMOKE-3 (§9.5); las dos últimas no se
han registrado en ningún smoke y siguen sin darse por superadas en Logic:

- **zoom en Logic** (ahora con los seis tamaños de §9.4, sin esquina): que
  cada tamaño disponible redimensione de verdad la ventana de Logic y que el
  ratón responda donde se dibuja;
- **recuperación de la preferencia de zoom:** tras SMOKE-2 no existe
  `~/Library/Application Support/VitaPlug/LABQ8/LABQ8.settings` (Logic no
  está en sandbox), así que no se llegó a guardar ningún zoom: o no se cambió,
  o falló el guardado dentro de Logic. Falta comprobar en Logic que se escribe
  al elegir un zoom y que se recupera al reabrir el editor y tras reiniciar
  Logic. Fuera de Logic está cubierto por pruebas (archivo temporal real,
  editor nuevo que lo lee, valores no válidos);
- **automatización en Logic:** gesto de `Band n Bypass` desde los botones y
  de Frequency/Gain/Q desde los nodos (Latch/Touch), y que botones, nodos y
  potenciómetros sigan la reproducción. Fuera de Logic está cubierto por
  pruebas;
- **persistencia de sesión en Logic:** guardar, cerrar y reabrir el
  proyecto (valores, bypass, banda seleccionada). Fuera de Logic está
  cubierto por pruebas.

### 9.3 Verificación de cierre (30-10-2026)

Sobre el mismo árbol de trabajo; recompilar `build/ui11-release-universal`
no cambia nada (el binario sigue siendo el instalado en SMOKE-2):

- 124/124 pruebas en Release arm64 y x86_64 (entre ellas estado v1–v4 y
  migración, automatización del host, carga de estado con el editor abierto,
  bypass desde los botones, zoom y edición desde los nodos);
- `run_compat.sh all`: las ocho bases OK en arm64 y x86_64; frente a UI-1
  (`13a810f`) y E2c.1 (`3041019`), estricta, sin diferencias;
- sin cambios frente a `13a810f` en `src/` fuera de `src/ui/`, ni en
  `assets/` ni en `docs/design/`; PNG maestro `4e14b925…f30b01`.

**Instalación para pruebas en host:** `tools/install/install_au.sh` (solo con
autorización). Se niega, sin tocar nada, si están abiertos Logic Pro,
MainStage, GarageBand o AUHostingService, o si algún proceso tiene abierto el
binario instalado; la copia anterior se aparta, nunca se borra. Probado en una
carpeta temporal (`LABQ8_COMPONENTS_DIR`), con procesos simulados.

### 9.4 Zoom en seis tamaños, sin tirador de esquina (30-10-2026)

Cambio aprobado tras SMOKE-2 para cerrar el redimensionado: el tamaño ya no
depende del tirador de esquina, que no respondió en SMOKE-1 y no se llegó a
validar en SMOKE-2.

- **Menú "Zoom"** del engranaje (y del clic derecho): **40 / 60 / 80 / 100 /
  120 / 140 %**, el actual marcado. Tamaños exactos: 669 × 376, 1003 × 565,
  1338 × 753, 1672 × 941, 2006 × 1129 y 2341 × 1317 (proporción del diseño;
  todo se dibuja con una única transformación, así que el ratón responde
  donde se dibuja).
- **Abre al 60 %** o al último tamaño elegido. Solo se guardan los seis
  tamaños, y solo al elegir uno en el menú; un tamaño de una build anterior
  (p. ej. 75 %), un valor ilegible o un archivo dañado vuelven al 60 %.
- **Ventana del host:** el editor cambia de tamaño con `setSize`, y el
  wrapper AU de JUCE ajusta con él la vista que Logic aloja
  (`EditorCompHolder::childBoundsChanged` → `resizeHostWindow`); VST3 avisa
  al host con `resizeView`. Que Logic redimensione su ventana se comprobará
  físicamente.
- **Pantalla disponible:** un tamaño cabe si el editor más 80 puntos de
  altura para la ventana del host (barra de título y cabecera de plug-in de
  Logic) cabe en el área útil de la pantalla (sin barra de menús ni Dock) en
  la que está el editor, o, antes de mostrarse, en la que está el ratón.
  Los que no caben aparecen deshabilitados en el menú con "(too large for
  this screen)" y no se aplican. 40 % siempre está disponible. Si el tamaño
  guardado no cabe al abrir, el editor abre al mayor tamaño menor que cabe,
  sin recortar nada, y la preferencia se conserva para una pantalla mayor.
  En esta máquina (1710 × 1072 puntos útiles) caben hasta el 100 %; 120 y
  140 % aparecerán deshabilitados.
- **Sin tirador de esquina** y sin redimensionado por el host
  (`setResizable (false, false)`): ningún control engañoso.
- **Por encima del 100 %** el fondo se interpola desde los 1672 × 941 px del
  maestro (no hay más detalle); textos, potenciómetros, botones, curva y
  nodos se dibujan nítidos a cualquier tamaño.

**Limitación conocida:** si se arrastra la ventana a una pantalla más pequeña
con el editor abierto, el tamaño no se reduce solo; el menú ya solo ofrece lo
que cabe allí.

**Verificación** (`build/ui11zoom-release-universal`, árbol de trabajo):

- 125/125 pruebas en Release arm64 y x86_64; nuevas o reescritas: los seis
  tamaños exactos con su proporción y escala, guardado inmediato y
  recuperación desde un archivo real por un editor nuevo (como al reiniciar
  el host), valores no válidos, sin esquina, enrutado del ratón en los seis
  tamaños, menú con tamaños deshabilitados en pantallas de 1710 × 1072,
  1440 × 875 y 640 × 400, y límites exactos de encaje;
- mutaciones, todas detectadas (9): setZoom sin comprobar la pantalla,
  apertura sin reducir, esquina restaurada, sin margen para el host, 40 % no
  garantizado, menú sin deshabilitar, guardar cualquier valor, cargar sin
  exigir un tamaño exacto, no guardar al elegir;
- `run_compat.sh all`: las ocho bases OK en arm64 y x86_64; frente a UI-1 y
  E2c.1, estricta, sin diferencias;
- PNG maestro `4e14b925…f30b01`; sin cambios en el DSP, los parámetros ni el
  estado;
- capturas (`--ui11`): `ui11_3_editor_40.png` y `ui11_6_editor_140.png`
  sustituyen a la del 50 %.

### 9.5 SMOKE-3 en Logic Pro (30-10-2026)

Build instalado con `tools/install/install_au.sh` (Logic cerrado):
`build/ui11zoom-release-universal`, SHA-256 del binario
`c87e66f004d4fb6e2f9f531875873552ffca5d9b9673554a1bc38c0d0c909c9d`; auval
arm64 y x86_64 sin avisos, 80 parámetros. Resultado aprobado por el usuario:

- **el selector de zoom funciona perfectamente en Logic** (los tamaños
  redimensionan la ventana del host);
- **el tamaño elegido se recuerda tras reiniciar Logic;** Logic escribió la
  preferencia en `~/Library/Application Support/VitaPlug/LABQ8/LABQ8.settings`.

Con esto **UI-1.1 queda aprobada**. Siguen sin registrarse en Logic la
automatización y la persistencia de sesión (§9.2), cubiertas fuera de Logic
por las pruebas.

## 10. UI-2a: medidores IN/OUT reales

**Estado:** **cerrada**, sobre `7833bd8`; arquitectura y decisiones
aprobadas el 30-10-2026; colores ámbar y rojo aprobados sobre las capturas;
**SMOKE-4 en Logic aprobado** (§10.1). Sin cambios de DSP,
parámetros, IDs, estado ni PNG maestro. Sin FFT ni visualización de la
reducción dinámica.

**Medición** (`src/plugin/LevelMeter.h`, adaptador; nada en `src/dsp/`):

- **pico de muestra** (sample peak, dBFS) por canal, por bloque: IN antes de
  `eq.process` (que trabaja in situ), OUT después (lo que recibe el host); solo
  lectura, el audio no cambia;
- **bypass del host** (`processBlockBypassed`): el paso directo de JUCE,
  medido; IN = OUT = la señal sin procesar, salida idéntica;
- **mono:** se mide su único canal; la interfaz lo muestra en L y en R (no se
  genera ni se procesa un segundo canal);
- muestras NaN o infinitas: +inf (medidor lleno); el audio, como antes;
- `prepareToPlay` y la apertura del editor descartan los picos anteriores.

**Transporte sin bloqueos:** cuatro `std::atomic<float>` sin locks
(`static_assert`). El hilo de audio eleva el máximo de cada canal
(`compare_exchange_weak`, relaxed); la interfaz lo toma y lo pone a 0
(`exchange`). Ningún pico se pierde entre dos lecturas, sea cual sea el tamaño
de bloque o el refresco, y el silencio lee exactamente 0. Sin asignaciones ni
llamadas al sistema en el hilo de audio.

**Interfaz** (`src/ui/LevelMeterView.{h,cpp}`, `UiModel`):

- refresco con la pantalla (`juce::VBlankAttachment`), independiente del
  temporizador de parámetros (30 Hz); se detiene al cerrar el editor;
- balística: subida instantánea; caída lineal de **11,8 dB/s** en función del
  tiempo real transcurrido (independiente de la frecuencia de refresco). El
  valor se tomó como referencia de la caída de IEC 60268-18 (20 dB en 1,7 s);
  **no se ha validado la conformidad con esa norma**;
- **26 celdas** por columna, filas medidas en el maestro (17 encendidas en él
  desde y 307; 9 por encima, la novena es la celda "0"). Umbral de cada
  celda: la escala impresa (+12 / +6 / 0 / −6 / −12 / −24 / −48 en las filas
  224 / 262 / 299 / 339 / 380 / 431,5 / 468) en la fila central de la celda,
  con la celda "0" exactamente a 0 dBFS y la superior a +12. De abajo arriba:
  −49,3 · −43,1 · −37,2 · −31,2 · −25,3 · −22,1 · −19,6 · −17,0 · −14,6 ·
  −12,3 · −10,8 · −9,4 · −7,7 · −6,1 · −4,6 · −3,1 · −1,6 · **0,0** · +1,3 ·
  +2,8 · +4,4 · +5,9 · +7,4 · +9,0 · +10,6 · +12,0 dB;
- **colores:** verde hasta 0 dBFS (18 celdas), ámbar por encima de 0 hasta
  +6 dBFS (4), rojo por encima de +6 dBFS (4). Indican sobrenivel, no clipping
  confirmado. Las celdas que el maestro muestra encendidas se dibujan con sus
  propios píxeles (`meter_lit.png`); las demás, con la luz de una celda
  encendida de la misma altura (encendida menos apagada) con el tono cambiado
  (ámbar 36°, rojo 2°) sobre su fondo apagado;
- cada celda ocupa desde la mitad del hueco superior hasta la mitad del
  inferior, con 6 px de brillo lateral cortado en la mitad entre L y R;
- se repinta solo la columna cuyo número de celdas cambia; en reposo, nada;
- el área ya no es una zona inactiva ("Level meters" sin tooltip); no recibe
  el ratón.

**Verificación** (`build/ui2a-release-universal`):

- 135/135 pruebas en Release arm64 y x86_64 (10 nuevas): silencio exacto,
  senos de nivel conocido en IN y OUT (Bell +6 dB), +6 dBFS sin recorte,
  canales independientes, mono, picos de una muestra en cualquier posición
  con bloques de 1 a 4096, retención hasta la lectura, bypass idéntico y
  medido, NaN/Inf, sin asignaciones, concurrencia (seis rondas de 400 000
  escrituras contra tomas continuas: ningún pico perdido, ninguno leído dos
  veces, ninguno inventado), escala, colores, balística, repintado, mono en
  la vista y picos antiguos descartados;
- **dibujo idéntico al maestro:** a −1 dBFS (17 celdas, el estado del
  maestro) la zona de los medidores coincide píxel a píxel con el maestro
  (diferencia máxima 0; prueba y captura);
- ThreadSanitizer (`build/ui2a-tsan`, arm64): las pruebas de medidores sin
  avisos;
- mutaciones: 16 de 17 detectadas; la que no se detecta
  (`if (p > c) store (p)`) es equivalente con un solo escritor (ver "Por qué
  el CAS" más abajo);
- `run_compat.sh all`: las nueve bases OK en arm64 y x86_64, estricta sin
  diferencias frente a UI-1.1 (`13df851`, base nueva) y UI-1;
- **CPU** (`labq8_bench --ui2a`, estéreo, mediana de 9): los dos medidores
  cuestan **0,006–0,029 %** de un núcleo en arm64 (Apple M5; 1,3–3,2 ns por
  trama estéreo) y 0,005–0,020 % en x86_64 (Rosetta), a 48 y 96 kHz con
  bloques de 32 a 512;
- **repintado** de una columna (software, Release arm64): 0,05 ms al 100 %,
  0,11–0,12 ms al 60 y al 140 %.

**Por qué el CAS.** El hilo de audio eleva el máximo con un bucle
`compare_exchange_weak`, y la interfaz lo toma con `exchange (0)`. Las dos son
operaciones lectura-modificación-escritura sobre el mismo atómico, así que
cada toma devuelve exactamente el máximo de lo escrito desde la anterior,
con cualquier número de escritores. Un `load` seguido de un `store` **no** es
equivalente en general, ni siquiera con un solo productor: si la interfaz
toma el valor entre el `load` y el `store`, un `store (max (c, p))`
**republica `c`, un máximo ya consumido**, que se leería dos veces (prueba:
20 de 20 ejecuciones lo detectan). La variante `if (p > c) store (p)` solo
escribe el pico nuevo, que nadie ha tomado aún, y con un único escritor
resulta equivalente (ninguna prueba puede distinguirla). Pero lo es por ese
detalle, deja de serlo con dos escritores sobre el mismo contador (el
`store` de uno pisaría un pico mayor del otro) y también si la interfaz
escribiera algo distinto de 0. El código definitivo **mantiene el CAS**, que
cuesta lo mismo en el caso normal: una comparación, y solo repite si la toma
coincide.

**Prueba de concurrencia** (corregida). La primera versión anotaba un contador
`published` antes de cada toma y suponía un orden entre ese contador y el
medidor. Con `relaxed` y `acquire/release` en ARM ese orden no está
garantizado, y al repetirla con varias semillas dio falsos "peak lost" con el
código correcto (2 de 25 ejecuciones). La versión actual solo usa la
coherencia del propio atómico. Las escrituras de un hilo conservan su orden,
así que las tomas cortan la secuencia escrita en tramos consecutivos y cada
toma debe ser el máximo de su tramo. La prueba comprueba que ese corte
existe: ningún valor inventado ni leído dos veces, ninguno perdido, en seis
rondas de 400 000 escrituras. Resultados de 20 a 40 ejecuciones por
implementación:

| Implementación | Falla |
|---|---|
| CAS (definitivo) | 0 de 40 |
| `store (max (c, p))` | 20 de 20 (leído dos veces) |
| toma con `load` + `store (0)` | 20 de 20 (pico perdido) |
| último bloque en vez del máximo | 5 de 5 (pico perdido) |
| `if (p > c) store (p)` | 0 de 20 (equivalente con un escritor) |

**Capturas** (`labq8_ui_snapshot <maestro> <dir> --ui2a`, audio real por el
procesador): `ui2a_1_editor_master_level.png`,
`ui2a_2_states_silence_-1_+3_+9_+13.png` (silencio, verde, ámbar, rojo,
lleno), `ui2a_3_independent_eq_mono.png` (L −12 / R −30; IN −6 con Bell
+6 dB en OUT; mono), `ui2a_4_zoom_40.png`, `ui2a_5_zoom_140.png` y
`ui2a_report.txt`.

**Límites conocidos:** pico de muestra, no true peak (los picos entre
muestras pueden quedar unos 3 dB por debajo); resolución de la escala del
diseño (1,5 a 6 dB por celda); retardo visual de un bloque más un fotograma;
si el host deja de llamar al plugin (transporte parado, pista inactiva), los
medidores caen a reposo.

### 10.1 SMOKE-4 en Logic Pro (30-10-2026)

Build instalado con `tools/install/install_au.sh` (Logic cerrado):
`build/ui2a-release-universal`, SHA-256 del binario
`d5cb0d856778059b23de9fafcc24f200353d2d70eb0925fe0d111ab5a8247779`; auval
arm64 y x86_64 sin avisos, 80 parámetros. Resultado aprobado por el usuario:
**los cuatro medidores funcionan perfectamente y el sonido aprobado se
conserva**. UI-2a queda aprobada funcional y musicalmente.

Después del smoke solo cambiaron la prueba de concurrencia y esta
documentación; el código del plugin es el mismo que se instaló.
