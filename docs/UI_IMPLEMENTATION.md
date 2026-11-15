# UI-1: implementación de la interfaz aprobada

**Estado:** UI-1 cerrada (`13a810f`). **UI-1.1** (§9) cambia la ergonomía
tras SMOKE-1: zoom, botones de banda ON/OFF y edición desde los nodos;
**cerrada**, aprobada en SMOKE-2 y SMOKE-3 (§9.1, §9.5). **UI-2a** (§10)
añade los medidores IN/OUT reales (cerrada, SMOKE-4 aprobado, §10.1).
**UI-2b** (§11) añade el analizador espectral FFT y las vistas EQ / SPECTRUM /
ALL (cerrada, SMOKE-5 aprobado, §11.1). Lo que
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
| `background.png` | `8c387ca4…0c468` | El maestro sin los elementos que dependen del estado, con las escalas re-rotuladas; desde UI-2b, la cabecera de la pantalla sin la pestaña iluminada, los nombres de las pestañas, el valor del Analyzer ni el recuadro "View" (antes `176895c9…40d69`; idéntico fuera de x 366–1257, y 188–246) |
| `dynamic_switch_on.png` | `f90fc1d5…d85c23f` | El interruptor del maestro (encendido), recortado a la forma de la píldora |
| `dynamic_switch_off.png` | `3409d491…846f8` | El mismo, reflejado y desaturado (apagado) |
| `meter_lit.png` | `7bc0aa5f…ed4d4` | UI-2a: las celdas encendidas de los cuatro medidores del maestro, con su brillo (x 1389–1550, y 302–477) |
| `axis_blank.png` | `ef78c413…fafaba14` | UI-2b: el eje izquierdo de la pantalla (x 356–404, y 240–484) sin sus siete etiquetas; el editor escribe ahí la escala dBFS en SPECTRUM |
| `viewtab_light.png` | `ee27e901…a38f99` | UI-2b: la luz que la pestaña iluminada del maestro añade sobre la cabecera limpia (maestro menos fondo, sin el nombre "EQ"; x 366–447, y 188–246) |

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
| Pestañas EQ / SPECTRUM / DYN / ALL, Analyzer, View, ajustes | ~~Sin analizador FFT ni vistas~~: EQ / SPECTRUM / ALL y Analyzer reales desde UI-2b (§11); DYN deshabilitada y "View: EQ + Dyn" retirado hasta UI-2c |
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

## 11. UI-2b: analizador espectral FFT

**Estado:** **cerrada**, sobre `761b7bf`; arquitectura y decisiones aprobadas
el 30-10-2026; escalas aprobadas el 31-10-2026; capturas aprobadas; **SMOKE-5
en Logic aprobado** (§11.1). Sin cambios de DSP, parámetros, IDs, estado ni
PNG maestro. Sin UI-2c (DYN), M/S, INTENTION ni PHASE BEHAVIOR.

**Captura** (`src/plugin/SpectrumTap.h`, adaptador; nada en `src/dsp/`):

- dos puntos, **PRE** (antes de `eq.process`) y **POST** (después: lo que
  recibe el host), en `processBlock` y en `processBlockBypassed` (bypass del
  host: PRE = POST = la señal sin procesar); solo lectura;
- un anillo de 65 536 muestras por canal, reservado una sola vez en el
  constructor (nunca en `prepareToPlay`); muestras como `std::atomic<float>`
  relaxed, así que ningún acceso es una carrera de datos;
- **seqlock sin espera:** el escritor anuncia hasta dónde va a escribir
  (`claimed`) antes de tocar el anillo y publica lo escrito (`written`,
  release) después; la interfaz toma `written` (acquire), copia, aplica un
  fence acquire y comprueba `claimed`: si el escritor pudo sobrescribir algo
  de lo copiado, la copia se rechaza y se reintenta en el fotograma
  siguiente;
- solo copia mientras el analizador se muestra (`setActive`); si no, el hilo
  de audio solo lee un flag;
- `prepareToPlay` abre una **generación** nueva (sample rate y canales): la
  interfaz no muestra nada anterior; al activar una traza tampoco se muestra
  nada de lo que el anillo tuviera antes.

**Análisis** (`src/ui/SpectrumAnalyzer`, hilo de mensajes):

- FFT real (`juce::dsp::FFT`, vDSP en macOS) de 8192 muestras a 44,1/48 kHz,
  16 384 a 88,2/96 kHz y 32 768 a 176,4/192 kHz: unos 170 ms y 5,9 Hz por
  bin a cualquier frecuencia de muestreo;
- ventana Blackman-Harris de 4 términos (lóbulos a −92 dB); calibrada por su
  ganancia coherente: un seno de amplitud A marca 20·log10 A dBFS en su pico
  (como mucho 0,83 dB menos entre dos bins);
- estéreo: media de potencia de L y R (L = R se lee como un canal; L = −R no
  se cancela; un canal solo se lee 3 dB por debajo); mono: su canal;
- puntos de pantalla cada 1,5 px de referencia (los de la curva): el mayor
  bin dentro del punto cuando lo hay, si no, interpolado en dB;
- **resolución medida** (dos tonos iguales con un valle de 3 dB, cerca de
  50 Hz): 13,5 Hz a 48, 96 y 192 kHz, 12,5 Hz a 44,1 kHz. **50 y 60 Hz no se
  separan.** Con 16 384 muestras a 48 kHz serían 6,5 Hz, pero con una ventana
  de 341 ms (el doble de lenta) y el doble de coste; no se adopta;
- balística: subida inmediata; caída lineal de 17,4 dB/s (decaimiento
  exponencial de potencia con τ = 250 ms) según el tiempo real.

**Pantalla** (`src/ui/SpectrumView`):

- bajo la curva y los nodos; eje horizontal 20 Hz–20 kHz con la misma escala
  que la curva (un tono de 1 kHz cae en la línea "1k"); eje vertical propio,
  **0 dBFS en la línea superior de la rejilla (y 251) y −96 dBFS en la
  inferior (y 474)**, 16 dB por línea, lineal entre líneas (las del maestro
  están a 36,5–38 px) y sin inclinación; idéntico en SPECTRUM y ALL; la EQ
  conserva su escala de ±24 dB (ajuste de 4 px respecto a los bordes del
  gráfico, aprobado el 31-10-2026);
- **escalas** (aprobadas el 31-10-2026): **EQ**, ±24 dB a la izquierda (la
  del fondo); **SPECTRUM**, 0 / −16 / −32 / −48 / −64 / −80 / −96 dBFS a la
  izquierda, sobre el eje sin etiquetas (`axis_blank.png`), con la fuente y el
  color de las de la EQ (Helvetica Neue Medium 17,75), en sus mismos centros
  y alineadas a su derecha (x 391), con "dBFS" encima, bajo la cabecera;
  **ALL**, ±24 dB a la izquierda y la escala dBFS discreta (más pequeña, en el
  verde del espectro) dentro del borde derecho del gráfico, bajo el icono de
  ajustes; esta se guarda como imagen, porque el espectro repinta debajo;
- POST o PRE solos: área verde (colores del espectro ilustrativo del
  maestro) con contorno más claro; **PRE + POST**: POST como área verde y PRE
  como contorno claro cálido sin relleno, encima;
- la traza termina en el último punto por debajo de Nyquist (a 44,1 y
  48 kHz el extremo derecho del gráfico queda por encima);
- unos 30 fotogramas por segundo (sobre el refresco de la pantalla). Sin audio
  nuevo (transporte parado) no se reanaliza el último bloque: el espectro cae
  hasta el reposo, nunca se congela. En reposo no se repinta;
- la curva y los nodos se guardan como imagen (`setBufferedToImage`): los
  fotogramas del espectro no recalculan las ocho respuestas; solo un cambio
  de la curva lo hace.

**Cabecera:**

- pestañas **EQ** (solo curva y nodos), **SPECTRUM** (solo espectro; sin
  curva, nodos ni edición) y **ALL** (ambos, **por defecto**); **DYN**
  atenuada y sin efecto ("DYN: not available yet") hasta UI-2c;
- la pestaña seleccionada lleva la píldora iluminada del maestro: su luz
  (`viewtab_light.png`) sobre la cabecera limpia, estirada al ancho de la
  pestaña (exacta en EQ); nombres en DIN Alternate (15,75; la seleccionada
  17,25, como el "EQ" del maestro);
- **Analyzer:** menú Pre / Post / Pre + Post, **Post** por defecto; el
  recuadro muestra "Analyzer: Pre", "Analyzer: Post" o "Pre + Post"
  ("Analyzer: Pre + Post" no cabe antes de la flecha); Helvetica Neue Medium
  16, como el maestro;
- el recuadro "View: EQ + Dyn" se retira del fondo hasta que exista la vista
  dinámica, para no sugerir una función inexistente;
- vista y fuente: por instancia, en memoria (como la banda seleccionada);
  nunca en el estado ni en los parámetros.

**Verificación** (`build/ui2b-release-universal`, máquina sin otras cargas):

- 151/151 pruebas en Release arm64 y x86_64 (16 nuevas de UI-2b); las de
  rendimiento del DSP también, repetidas sin compilaciones en paralelo (con
  el arnés compilando a la vez superaban sus umbrales de tiempo);
- `run_compat.sh all`: las diez bases OK en arm64 y x86_64, estricta sin
  diferencias frente a UI-2a (`761b7bf`, base nueva), UI-1.1 y UI-1;
  repetida frente a UI-2a tras el último cambio del editor, sin diferencias;
- ThreadSanitizer (`build/ui2b-tsan`, arm64): pruebas del espectro (incluida
  la de estrés de la captura) y de los medidores, 0 avisos;
- **captura bajo estrés:** 200 millones de muestras con bloques de 1 a 40 000
  contra copias continuas: toda copia aceptada es exactamente la secuencia
  más reciente; las sobrescritas se rechazan (cientos por ejecución); el
  límite de coherencia se prueba además exactamente en su frontera;
- **mutaciones**, todas detectadas (24): en la captura (sin comprobar
  `claimed`, `claimed` sin el bloque nuevo, margen +1 y +4096 muestras, PRE
  tras el EQ, ignorar el estado inactivo, no cortar la generación) y en el
  análisis, la vista y la cabecera (ventana rectangular o Hann, sin el factor
  2 de calibración, suma en vez de media de potencia, suma de amplitudes,
  primer bin en vez del máximo, dibujar más allá de Nyquist, FFT fija de 4096,
  caída de 30 dB/s, reanalizar sin audio nuevo, mostrar audio anterior a la
  activación, ignorar el cambio de sample rate, sin caché de la curva,
  SPECTRUM con la curva visible, EQ con la captura activa, DYN seleccionable,
  sondeo de parámetros sin invalidar la caché);
- **caché de la curva:** tras cambiar Gain, Frequency y Q desde el panel,
  desde el host (`setValueNotifyingHost`) y como aplica el wrapper la
  automatización del host (`setValue` y aviso a los listeners), con el
  espectro funcionando, la curva en caché coincide píxel a píxel con la de un
  editor recién creado; antes del sondeo de parámetros sigue mostrando la
  anterior (la caché es real);
- cabecera frente al maestro (EQ seleccionada, Analyzer: Post): posiciones de
  la tinta a 1–2 px; diferencia media 5,0 en las pestañas y 10,5 en el
  recuadro del Analyzer (bordes de glifos: texto dibujado frente a la tinta
  del maestro).

**Coste medido** (Release arm64, Apple M5, renderizador nativo):

- hilo de audio (`labq8_bench --ui2b`): captura PRE + POST visible
  0,005–0,035 % de un núcleo (0,005–0,037 % en x86_64); oculta,
  0,0001–0,002 %;
- análisis por fotograma (2 copias, 4 FFT y correspondencia a puntos):
  0,08 ms a 44,1/48 kHz, 0,19 ms a 96 kHz y 0,37 ms a 192 kHz;
- repintado del gráfico por fotograma (PRE + POST): 0,55 ms al 60 %, 1,5 ms
  al 100 % y 3,3 ms al 140 % a 1×; en Retina (2×), 2,9 / 2,6 / 9,2 ms; sin el
  espectro, 0,06–0,3 ms (el dibujo del espectro es casi todo el coste);
- **al 60 % (tamaño habitual), Retina, 30 fotogramas/s, con todos los
  editores abiertos a la vez:** POST 6,8 % de un núcleo por instancia (1: 6,8;
  2: 13,6; 4: 28,9; 8: 54,2 %), PRE + POST 9,1 % (1: 9,1; 2: 18,0; 4: 35,2;
  8: 69,9 %). Crece de forma lineal con las ventanas abiertas; un editor
  cerrado, en vista EQ o con el espectro en reposo no cuesta nada. Al 60 % el
  fondo y la curva en caché se re-muestrean (escala 1,2 en Retina), por eso
  cuesta algo más que al 100 %.

**Escalas** (tras la aprobación): prueba de qué escala muestra cada vista,
de las etiquetas en los centros de la rejilla, alineadas a la derecha, sin
solaparse entre sí, con el gráfico, la cabecera ni el icono de ajustes (tinta
real de los glifos), de las siete etiquetas dBFS dibujadas en SPECTRUM sin
restos de las de la EQ, y de que ALL conserva exactamente las de la EQ;
mutaciones detectadas (5): escala izquierda en ALL, escala derecha ausente,
correspondencia lineal sin pasar por las líneas, "dBFS" sobre el "0",
etiquetas alineadas a la izquierda. 152/152 pruebas en arm64 y x86_64;
compatibilidad estricta frente a UI-2a sin diferencias.

**Capturas** (`labq8_ui_snapshot <maestro> <dir> --ui2b`, programa
sintético a través del procesador con los ajustes del maestro):
`ui2b_9/10/11_editor_60_eq|spectrum|all.png`, `ui2b_1_editor_all_post.png`,
`ui2b_2_all_pre.png`, `ui2b_3_all_pre_post.png`,
`ui2b_4_spectrum_post.png`, `ui2b_5_eq.png`, `ui2b_6_all_silence.png`,
`ui2b_7_editor_40.png`, `ui2b_8_editor_140.png` y `ui2b_report.txt`. En
ellas los medidores IN/OUT aparecen apagados porque la herramienta no avanza
su refresco (en el editor real se mueven).

### 11.1 SMOKE-5 en Logic Pro: 5/5 PASS

Build instalado con `tools/install/install_au.sh` (Logic cerrado):
`build/ui2b-release-universal`, SHA-256 del binario
`0a7b26e2c03e4b980d0c78c13e5f0a8c563d1655ce21e8bb8dbd535991c01c6a`, el código de
las 152/152 pruebas y de la compatibilidad estricta; auval arm64 y x86_64 sin
avisos, 80 parámetros. Resultado aprobado por el usuario:

- EQ / SPECTRUM / ALL funcionan; DYN permanece desactivada;
- PRE / POST / PRE + POST funcionan y muestran las diferencias reales de la EQ;
- los medidores IN/OUT funcionan a la vez que el analizador;
- el espectro desaparece al detener la señal, sin congelarse;
- sonido, zoom, automatización y recuperación de la sesión correctos.

UI-2b queda cerrada. Copia de UI-2a (SMOKE-4) en
`build/ui2b-report/LABQ8.component.smoke4-backup`.

## 12. UI-2c: visualización dinámica real

**Estado:** **cerrada**, sobre `d7840f3`; SMOKE-6 en Logic aprobado (§12.6).
- **Aprobaciones:** auditoría y decisiones D1–D4 el 04-11-2026, con D3 modificada y D4 restringida; política de lectura A2 + A4 el mismo día.
- **Capturas aprobadas** el 04-11-2026 (§12.5), con el relleno estático de la banda seleccionada tal como está.
- **SMOKE-6** en Logic Pro aprobado por el usuario (§12.6).
- **Sin cambios** en el DSP (`src/dsp/`), los parámetros, los IDs, el estado v4 ni el PNG maestro.
- **Fuera de alcance:** M/S, INTENTION, PHASE BEHAVIOR y cualquier función DSP nueva.

### 12.1 Qué existe en el DSP (auditoría)

- **Solo Bell sin bypass tiene dinámica.** Lo fija `EqCore::updateEngaged`, y `EqBand::modulationFor` ignora la modulación en shelves, cortes y notch. Un `b{n}_dyn_on` guardado en otro tipo no actúa y no se dibuja.
- **Solo hay reducción, nunca expansión.** `dynamics::gainReductionDb` va de 0 dB al Range, con un codo de 6 dB. La banda recibe `−reducción` sobre su ganancia estática.
- **Ganancia efectiva** = clamp(estática − reducción, ±24 dB), el límite del diseñador. Por ejemplo, con estática −20 dB y una reducción de 12 dB solo se oyen 4 dB.
- **Reducción por banda:** `DynamicSection::getGainReductionDb()` es el valor aplicado en el último tick de 16 muestras. Ya incluye ataque, release, Range y el fundido de 20 ms del interruptor.
- **Nivel del detector:** no se transmite ni se representa (D4).

### 12.2 Telemetría (`src/plugin/DynamicsTelemetry.h`)

- **Una instantánea** contiene las ocho bandas de un mismo instante:
  - ganancia estática, frecuencia y Q efectiva tal como las suaviza el DSP (`EqBand::getCurrent…`);
  - la reducción de la sección;
  - si el filtro activo es Bell.

  El procesador solo copia valores que el DSP ya ha calculado, con accesores `const` desde el hilo de audio.
- **A4: sub-bloques.** El procesador llama a `eq.process` en sub-bloques de ≤ 256 muestras (`eqSubBlockSamples`) y ofrece una instantánea tras cada uno.
  - La salida de `EqCore` no depende de cómo se divide el bloque: su rejilla de control es absoluta.
  - Los parámetros se siguen leyendo una vez por bloque del host.
  - El sonido es idéntico bit a bit. Lo comprueban:
    - una prueba frente a una llamada por bloque, en 9 tamaños de bloque y con automatización;
    - la compatibilidad estricta frente a las 11 bases.
- **A2: qué instantánea se muestra.** Entre dos lecturas de la interfaz se conserva la instantánea ofrecida con mayor reducción efectiva total. Tras la lectura, la siguiente oferta la sustituye, sea cual sea su valor.
  - Se muestra siempre un instante real; nunca se combinan máximos de instantes distintos.
  - La interfaz marca lo leído por el identificador de la instantánea, así que una lectura que coincide con una escritura no deja un pico pegado (probado).
- **Seqlock sin espera:**
  - el escritor pone la secuencia impar, escribe y la vuelve par (release);
  - el lector copia entre dos lecturas iguales de la secuencia (acquire más fence);
  - todos los campos son atómicos relaxed, así que ningún acceso es una carrera de datos.
- **Cuándo escribe:**
  - solo mientras DYN o ALL están visibles (`setActive`); si no, el hilo de audio solo lee una bandera;
  - en el bypass del host ofrece "sin dinámica";
  - `prepareToPlay` abre una generación nueva.
- **Coste** (`labq8_bench --ui2c`, Apple M5):
  - una oferta: 80 ns en arm64 y 124 ns en x86_64, como mucho una por 256 muestras (0,0015 % y 0,0023 % de un núcleo a 48 kHz);
  - A4 frente a una llamada por bloque: sin diferencia medible, dentro del ruido entre ejecuciones.

### 12.3 Por qué A2 + A4: medición dentro del bloque

`labq8_bench --ui2c-intrablock` usa una Bell de 1 kHz con umbral −30 y Range 12 dB, a 48 kHz. La referencia es la reducción de cada tick, y se mide cuánto de cada pico deja de verse en una pantalla a 30 Hz. Pérdida en el peor caso, en dB:

| Señal, ataque/release | bloque | última del bloque | A2 | **A2 + A4** |
|---|---|---|---|---|
| ráfagas, 0,5 / 5 ms | 1024 | 9,98 | 0,00 | **0,00** |
| ráfagas, 0,5 / 5 ms | 4096 | 12,00 | 12,00 | **0,00** |
| ráfagas, 0,5 / 50 ms | 4096 | 8,81 | 8,81 | **0,00** |
| ráfagas, 10 / 100 ms | 4096 | 5,70 | 5,70 | **0,11** |
| percusión, 10 / 100 ms | 4096 | 1,89 | 1,89 | **0,01** |

Con A2 + A4, la pérdida es ≤ 0,11 dB en todos los casos medidos, unos 0,5 px de referencia. La tabla completa está en `build/ui2c-report/ui2c-intrablock.txt`.

### 12.4 Representación (`src/ui/DynamicsOverlay`, D1–D3)

- **Curva estática:** la de siempre, sólida y editable, con los nodos encima. Se calcula desde los parámetros, en la caché de `ResponseScreen`, y la dinámica nunca la repinta (probado).
- **Respuesta dinámica:** solo para las Bell con reducción efectiva > 0.
  - Se dibuja la curva estática más la diferencia que la reducción causa en el diseño de cada banda:

    Δ_b(f) = |H(f; g − r_ef)| − |H(f; g)| en dB

    con g, f y Q del DSP. La cascada es un producto, así que las Δ se suman exactamente.
  - **Trazo:** punteado, con el color de la banda que más mueve la curva en cada tramo.
  - **Relleno:** con el color de la banda, apilado entre la curva estática y la dinámica.
  - **Nodos:** en su posición estática.
- **DYN:** curva, nodos y dinámica, con la reducción efectiva escrita bajo el punto dinámico de cada banda que actúa ("−2.7"). No muestra espectro, y los taps de audio no copian.
- **ALL:** espectro, curva y dinámica, sin cifras.
- **EQ y SPECTRUM:** como en UI-2b, sin telemetría.
- **"View: EQ + Dyn":** sigue retirado (D2).
- **Refresco:**
  - unos 30 Hz;
  - repinta solo si alguna reducción se mueve ≥ 0,01 dB, o si cambian los parámetros o el estado de la banda; en reposo no repinta (probado);
  - sin suavizado añadido: se ven el ataque y el release reales.
- **Silencio:** se ve el release real hasta volver exactamente a la curva estática.
- **Sin bloques durante 100 ms, bypass del host o una generación nueva:** la superposición se retira en el acto, sin inventar ninguna recuperación (D3).
- **Veracidad (condición 4):**
  - **estado estacionario:** la curva punteada coincide con `EqCore::magnitudeAt` del DSP con una diferencia de 0,000000 dB, incluido el límite de ±24 dB y dos bandas solapadas;
  - **durante ráfagas** (ataque de 1 ms), comparada en el mismo instante: como mucho 0,0058 dB.
- **Limitaciones documentadas:**
  - dentro de cada tick de 16 muestras el DSP interpola los coeficientes de una muestra a otra; la pantalla muestra el diseño al final del tick;
  - la curva estática sale de los objetivos de los parámetros, mientras que el DSP los alcanza con rampas de 20 ms, así que al mover un control la referencia se adelanta hasta 20 ms;
  - durante el fundido de 20 ms de un cambio de tipo desde Bell, la reducción saliente no se dibuja, porque el filtro activo ya no es Bell.

### 12.5 Coste y capturas (aprobadas el 04-11-2026)

Coste medido con `labq8_ui_snapshot --ui2c` en Apple M5, con un editor y la señal de programa: mediana por fotograma de pantalla con densidad Retina, en % de un núcleo a 30 fotogramas/s.

**Aviso sobre la comparación:** en esta sesión, el código UI-2b sin cambios mide unas 2 veces más que el día de su medición (por ejemplo, el FFT tarda 0,080 frente a 0,041 ms). Por eso cada cifra se compara con la referencia sin dinámica medida en la misma pasada.

| Zoom | Vista | Analizador | Sin dinámica (UI-2b) | 2 bandas | 8 bandas |
|---|---|---|---|---|---|
| 60 % | ALL | Post | 13,6 % | 16,1 % | 19,1 % |
| 60 % | ALL | Pre + Post | — | 23,0 % | 23,3 % |
| 60 % | DYN | — | — | 9,6 % | 12,7 % |
| 100 % | ALL | Post | 11,2 % | 14,1 % | 17,7 % |
| 100 % | ALL | Pre + Post | — | 18,7 % | 22,4 % |
| 100 % | DYN | — | — | 4,3 % | 8,0 % |

- **Cálculo de la curva dinámica:** 0,043 ms con 2 bandas y 0,17 ms con 8, con la respuesta sin reducción de cada banda en caché.
- **Resto del coste:** el dibujo.
- **Capturas** (`build/ui2c-captures/`):
  - las cuatro vistas, a la vez que la señal de programa;
  - DYN al 100 % y al 60 %, con densidad 1× y Retina;
  - detalle a 2×;
  - ALL con Pre + Post;
  - ocho bandas dinámicas;
  - vuelta a la curva estática tras el silencio.

**Pruebas de UI-2c:**
- **Batería completa:** 163/163 en arm64 y x86_64, 11 de ellas nuevas en `tests/DynamicsTests.cpp`.
- **TSan:** sin avisos.
- **Compatibilidad estricta:** 11 bases × 2 arquitecturas OK.
- **Mutación:** 18 de 19 mutantes detectados (`build/ui2c-report/ui2c-mutation.txt`). El superviviente, M11 (el overlay ignora el bypass del host), es equivalente: en el bypass, la telemetría ya ofrece todas las bandas vacías.

### 12.6 SMOKE-6 en Logic Pro: aprobado

**Build instalado** con `tools/install/install_au.sh`, con Logic cerrado:
- carpeta `build/ui2c-release-universal`;
- SHA-256 del binario: `77a6da120d1ab03964be1212c2a4c81e6e4e123dc3db938620e58c6805fb44ea`;
- mismo código que pasó las 163/163 pruebas por arquitectura y la compatibilidad estricta en 11 bases;
- auval `-strict` en arm64 y x86_64 sin avisos, con 80 parámetros.

Lista de comprobaciones: `build/ui2c-report/UI2C_PRUEBA_LOGIC.md`.

**Resultado:** aprobado físicamente en Logic Pro por el usuario. UI-2c queda cerrada.

**Copia de UI-2b (SMOKE-5):** `build/ui2c-report/LABQ8.component.smoke5-backup`.

## 13. E4c: interfaz STEREO / MID / SIDE

Detalle, decisiones, pruebas y capturas: [MIDSIDE_E4C.md](MIDSIDE_E4C.md)
(auditoría: [MIDSIDE_E4C_AUDIT.md](MIDSIDE_E4C_AUDIT.md)).

- **El bloque STEREO MODE deja de ser un área inactiva.** STEREO y M/S
  escriben `proc_mode`. MID y SIDE, en el antiguo hueco de L/R, eligen el
  banco que se edita en M/S.
- **Nuevos assets** derivados del maestro: `mode_face_lit.png` y
  `mode_face_unlit.png`. En `background.png` solo cambia el hueco de L/R.
- **Banco mostrado:** todo el panel, los botones ON/OFF y la pantalla siguen
  al banco mostrado. En M/S se añaden la curva gris del otro banco (EQ y
  ALL) y la nota "M/S dynamics display: E4d" (DYN y ALL).

## 14. E4d: analizador y dinámica MID / SIDE

Detalle, pruebas y medidas: `docs/MIDSIDE_E4D.md`.

- **Analizador:** en M/S analiza el banco editado. MID = (L + R) / 2 y
  SIDE = (L − R) / 2 se calculan en `double` sobre las muestras en el tiempo
  PRE / POST, antes de una sola FFT; misma ventana, escala dBFS y balística
  que UI-2b. PRE se lee terminando en la misma posición de flujo que POST
  (`SpectrumTap::copyAtStream`), así PRE y POST corresponden al mismo audio.
- **Menú en M/S:** "MID" o "SIDE" con Pre, Post, Pre + Post; después
  "Output" con "L/R Post (output)". El selector dice "MID Post",
  "SIDE Pre + Post", "L/R Post"… En STEREO, menú y textos de UI-2b.
- **Dinámica:** DYN y ALL dibujan la reducción real del banco mostrado
  (`DynamicsOverlay::setBank` → `DynamicsTelemetry::setWatchedBank`). La
  instantánea lleva banco y disponibilidad: con el banco en standby o
  durante el fundido de 20 ms no se dibuja nada (no es 0 dB), y al cambiar
  de banco no queda nada del anterior.
- Se retira la nota "M/S dynamics display: E4d". Sin cambios de PNG,
  geometría, botones ni aspecto de la curva.
