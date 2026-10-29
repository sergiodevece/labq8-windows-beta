# UI-1: implementación de la interfaz aprobada

**Estado:** aprobada visualmente el 29-10-2026, con las decisiones del §5
integradas. Queda pendiente la aprobación final del commit.

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
| B. Controles interactivos | 7 potenciómetros, Filter Type, interruptor Dynamic EQ, 8 selectores de banda, nodos de la pantalla (clic = seleccionar banda) | `src/ui/LabQ8Editor.cpp` |
| C. Valores y estados | Displays numéricos, cabecera "● BAND n", resaltado del selector, posición de los potenciómetros | `src/ui/LabQ8Editor.cpp` |
| D. Visualización | Respuesta estática real del EQ y nodos, calculados con los mismos diseños, límites y Q efectivo que el DSP | `src/ui/UiModel.cpp` |

- **Espacio de referencia:**
  - todo se dibuja en el espacio de **1672 × 941** del maestro, con las
    coordenadas medidas en `src/ui/UiGeometry.h`;
  - ese archivo lo comparten el generador de assets y el editor, así que lo
    que se borra del fondo y lo que se dibuja encima coinciden siempre;
  - un componente `Content` contiene todo y se escala con una única
    transformación.
- **Tamaño del editor:**
  - es redimensionable, con proporción fija 1672:941;
  - va del 50 % al 100 % y abre al **75 %** (1254 × 706);
  - las zonas de clic escalan con el dibujo.
- **Conexión con los parámetros:**
  - los potenciómetros usan `SliderAttachment`;
  - Filter Type, Dynamic EQ y el indicador de bypass usan
    `ParameterAttachment`, con gestos completos de inicio y fin.
- **Selección de banda:**
  - vuelve a conectar los diez controles del panel (siete potenciómetros,
    Filter Type, Dynamic EQ y el bypass) a los parámetros de la banda elegida;
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
| Punto de color junto a "BAND n" | `b{n}_bypass` | off / on | iluminado = activa; atenuado = bypass |
| Selector 1–8 / nodo en la pantalla | — (solo vista) | | resaltado y "BAND n" |

**Potenciómetros:**

- **Posición:** la fracción normalizada del parámetro, la que ve el host. El
  recorrido va de 222° a 138° (276°).
- **Ratón:** arrastre (240 px para todo el recorrido) y rueda.
- **Doble clic:** vuelve al valor por defecto.

**Bypass por banda:**

- **Cómo se usa:** clic izquierdo en el punto de color de "BAND n". La zona
  de clic es invisible, de 34 × 34 px, centrada en el punto de 19 px.
- **Qué cambia:** alterna `b{n}_bypass` de la banda mostrada, con gesto
  completo.
- **Clic derecho:** no hace nada.
- **Independencia:** no cambia la selección, y los selectores 1–8 nunca
  alternan el bypass.
- **Aspecto:**
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
| Medidores IN/OUT | Apagados: no hay medición publicada desde el audio |

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
