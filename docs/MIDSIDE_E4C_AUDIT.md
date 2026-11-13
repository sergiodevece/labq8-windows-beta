# LABQ8 E4c — Auditoría de interfaz STEREO / MID / SIDE

**Base:** `d27b9b1` (E4b cerrada). **Estado:** propuesta para aprobación. **No
implementada:** no se han modificado código, DSP, imágenes ni componentes de
interfaz. Las maquetas de esta auditoría están en `build/e4c-audit/` (fuera
del repositorio) y se han hecho sobre el fondo derivado que pinta el editor,
con caras de botón recortadas del PNG maestro.

**Objetivo de E4c:** conectar la interfaz aprobada con los tres bancos
reales, sin tocar el DSP, los 241 parámetros ni el estado v5.
**Fuera de alcance:** telemetría M/S, analizador por componente (E4d),
INTENTION y PHASE BEHAVIOR.

---

## 0. Situación actual del editor (d27b9b1)

- **El bloque STEREO MODE** (STEREO / M/S / L/R) es un `InactiveArea`
  {458, 80, 310, 58}: conserva su aspecto y no responde.
- **El editor solo conoce el banco STEREO.** Hay **seis puntos** que
  eligen IDs de banda; todos usan directamente `params::bandIDs` /
  `params::dynamicIDs`:

  | Punto | Qué enlaza |
  |---|---|
  | `parameterId()` | Los 7 potenciómetros (frecuencia, ganancia, Q, umbral, rango, ataque y release) |
  | `attachPanel()` | Interruptor dinámico, selector de tipo e indicador de bypass |
  | `BandButton`, en el constructor | Los 8 botones ON/OFF, enlazados de forma fija a `b{n}_bypass` |
  | `ResponseScreen::readBands()` | La curva, los nodos y los tiradores de Q, que también alimentan a `DynamicsOverlay` |
  | `ResponseScreen`, al arrastrar | Las escrituras de frecuencia, ganancia y Q desde los nodos y los tiradores |
  | `parameterSnapshot()` | La decisión de volver a dibujar: lee solo los 40 estáticos STEREO |
- **Desde E4b el audio ya es M/S** cuando `proc_mode = Mid/Side`. Hoy el
  editor muestra entonces el banco STEREO, que no se oye, sin dinámica
  dibujada (decisión de E4b).

---

## 1. Botones STEREO y M/S (activación real)

- Los dos botones del maestro pasan a ser **reales y excluyentes**,
  enlazados a `proc_mode` mediante `juce::ParameterAttachment`, con gestos
  `begin`/`end`. Un clic escribe el parámetro y, por tanto, se graba como
  automatización si el host está en escritura.
- **Encendido:** el botón del modo activo usa la cara verde del maestro, la
  del "STEREO" encendido. El apagado usa la cara oscura del maestro.
- **Geometría** (medida en el maestro; caras, en píxeles de referencia):

  | Botón | Marco (aprox.) | Cara | Texto (tinta del maestro) |
  |---|---|---|---|
  | STEREO | x 466–561, y 88–130 | x 471–556, y 92–126 | 55 × 12 px, x 486–540, y 104–115 |
  | M/S | x 565–659, y 88–130 | x 570–655, y 92–126 | 27 × 13 px, x 599–625, y 104–116 |
  | (L/R, se sustituye) | x 665–760, y 88–130 | x 669–754, y 92–126 | 24 × 13 px, x 699–722 |

- **Tipografía:** Helvetica Neue Condensed Bold, con mayúscula de 13 px.
  Identificada por anchura de tinta: "M/S" 28 frente a 27 px, "L/R" 24 frente
  a 24, "STEREO" 56 frente a 55.
- **Colores:** texto claro (182,174,163) sobre la cara apagada (46,44,41);
  texto oscuro (9,32,11) sobre la cara encendida (169,249,156).
- **Implementación prevista:** como las pestañas de vista de UI-2b. El
  generador de assets recorta del maestro una cara encendida y otra apagada,
  sin texto (estiramiento en 9 tramos), y el editor dibuja el texto.

## 2. Sustitución de L/R por MID y SIDE (excluyentes)

- El hueco de L/R (marco x 665–760) se divide en **dos marcos de 47 px con
  1 px de separación**: MID en el marco x 664–710 (cara de unos 41 px) y SIDE
  en el marco x 712–758. Los marcos y las caras son los mismos que en
  STEREO y M/S, estirados en 9 tramos.
- **Texto:** la misma fuente y la misma altura de mayúscula que "M/S"
  (13 px). "MID" mide 28 px y "SIDE" 33 px, lo que deja unos 4 px de margen
  en SIDE. La alternativa es una mayúscula de 11 px (SIDE de 28 px, unos
  6,5 px de margen). **Decisión D2.**
- **Excluyentes:** siempre hay uno seleccionado.
  - Seleccionar uno es **estado de interfaz**: no escribe ningún parámetro,
    no crea gestos y no altera el audio (§3).
  - **En STEREO** los dos aparecen atenuados (texto al 35–45 %, como el
    interruptor Dynamic EQ deshabilitado) y no responden. Tooltip:
    "MID / SIDE: M/S mode only" (spec v0.1, §1).
- **Fondo derivado:** el generador borra **solo** la cara y la tinta de L/R
  (rellena el hueco con el pozo oscuro del propio maestro). **El PNG maestro
  no se toca.**
  - Prueba de diferencia de píxeles: el `background.png` nuevo solo cambia
    dentro del rectángulo {660, 86, 104, 46}.
  - Se retira el `InactiveArea` "Stereo Mode"; los de POLARITY, INTENTION y
    PHASE BEHAVIOR siguen inactivos.

**Estados visuales** (maquetas en `build/e4c-audit/mode_buttons_*`):

| Estado | STEREO | M/S | MID | SIDE |
|---|---|---|---|---|
| Modo STEREO | encendido | apagado | atenuado | atenuado |
| M/S, editando MID | apagado | encendido | **seleccionado** | apagado |
| M/S, editando SIDE | apagado | encendido | apagado | **seleccionado** |

Cómo se marca "seleccionado" es la **decisión D1**:

- **Estilo A:** la misma cara verde encendida del maestro. Solo usa
  elementos que ya existen, pero hay dos botones verdes juntos (M/S y MID).
- **Estilo B:** cara oscura con el texto en verde (155,245,130) y un trazo de
  2 px bajo el texto. La cara verde llena queda reservada al modo de
  proceso, pero añade un elemento visual nuevo.

## 3. Selección independiente del banco editado

- **El banco editado es estado de interfaz**, no un parámetro. Se guarda en
  memoria por instancia, como la banda seleccionada (`uiSelectedBand`):
  `LabQ8AudioProcessor::get/setUiEditBank` (MID o SIDE, MID por defecto).
- **No forma parte de la sesión v5,** así que el estado no cambia. Al cerrar
  y abrir el editor, se conserva.
- **Banco mostrado** = STEREO si `proc_mode = Stereo`, o el último MID/SIDE
  elegido si es `Mid/Side`.
- **La banda seleccionada (1–8) se comparte** entre bancos: al pasar de MID a
  SIDE sigue seleccionada la banda 4. **Decisión D9.**

## 4. Correspondencia exacta entre banco, nodos, curva, controles y parámetros

- **Un único punto de verdad:** `BankIDs shownBank = params::bankOf
  (displayedBank())`. Todo lo del §0 lee y escribe a través de él.
- **Al cambiar de banco o de modo,** se vuelven a enlazar los 7
  potenciómetros, el selector de tipo, el interruptor dinámico, el
  indicador de bypass y **los 8 botones ON/OFF**, que dejan de estar fijos a
  `b{n}_bypass`. La pantalla lee las bandas del banco mostrado y los
  arrastres de nodos y tiradores escriben en sus IDs.
- **Cabecera del panel** (decisión D3; maquetas en
  `build/e4c-audit/header_*`):
  - En M/S se antepone el banco con letra pequeña, en el gris de las
    etiquetas del chasis: "MID  BAND 4" / "SIDE  BAND 4". "BAND 4" queda
    igual, en el color de la banda.
  - El texto acaba en x ≈ 308, antes de la etiqueta "Filter Type" del chasis
    (x 346), y **no requiere cambiar el fondo**.
  - Se descartó "SIDE · BAND 4" a tamaño completo porque **choca** con
    "Filter Type". La alternativa (c), "MID 4" / "SIDE 4" en lugar de
    "BAND 4", también cabe.
- **Colores:** los de las bandas no cambian. El banco se identifica por el
  botón MID/SIDE y por la cabecera.

## 5. Conservación de los ajustes al cambiar de banco

- Cambiar de banco o de modo desde la interfaz **solo vuelve a enlazar**:
  crear o destruir los enlaces de JUCE no escribe ningún parámetro.
- **Arrastre en curso:** si cambia el banco o el modo mientras se arrastra un
  potenciómetro o un nodo (por ejemplo, por automatización de `proc_mode`),
  antes de volver a enlazar se termina el gesto (`endDrag`). Así ningún valor
  del arrastre llega al banco nuevo.
- **Pruebas:** §10, T3 y T6.

## 6. Automatización del host y cambios de modo

- **Automatización de `proc_mode`:** los botones STEREO/M/S lo siguen
  mediante su enlace. El banco mostrado cambia en el siguiente tick del
  editor (30 Hz), aplicando la regla del §5 para un arrastre en curso.
- **Automatización de cualquier banco:** `parameterSnapshot()` pasa a leer
  los 120 parámetros estáticos de los tres bancos más `proc_mode`.
  - Si se mueve el banco mostrado, la curva y los nodos lo siguen, como hoy.
  - Si se mueve el otro banco (MID/SIDE), la curva gris lo sigue (§7).
  - Si se mueve el banco STEREO estando en M/S, no hay nada visible que
    cambie.
- **Editor genérico del host:** sigue funcionando; es la misma vía de
  parámetros.

## 7. Curva del banco no seleccionado (línea gris fina)

- **Solo en M/S,** y en las vistas EQ y ALL: la curva compuesta del otro
  banco (SIDE si se edita MID, y al revés).
  - **Estilo:** 1 px de referencia, gris (200,200,190) con una opacidad de
    0,4. Sin relleno, sin nodos y sin interacción: no recibe clics.
  - **Etiqueta:** "SIDE" o "MID", pequeña, en el mismo gris, en su extremo
    derecho. Es la **decisión D5**: con o sin etiqueta.
- **En STEREO no hay curva gris,** porque solo se oye un banco. El banco
  STEREO nunca se muestra mientras se está en M/S.
- Se dibuja en la imagen en caché de la pantalla, así que solo se recalcula
  cuando cambian sus parámetros. Coste: una cascada de 8 bandas más por
  redibujado.

## 8. Legibilidad (40 / 60 / 80 / 100 / 120 / 140 %)

Maquetas en `build/e4c-audit/mode_buttons_cap13_1x.png` y
`mode_buttons_cap11_1x.png` (seis zooms × cinco estados), y en
`*_retina.png` (densidad 2x).

| Zoom | Mayúscula 13 de referencia, en píxeles lógicos | Valoración |
|---|---|---|
| 40 % | 5,2 px (10,4 en Retina) | En pantallas 1x es tan pequeño como los STEREO/M/S y los displays de valores actuales; en Retina se lee |
| 60 % (por defecto) | 7,8 px | Legible; MID/SIDE se distinguen sin esfuerzo |
| 80–140 % | 10,4–18,2 px | Claro |

- **Riesgo real:** 40 % en una pantalla 1x. Es una limitación que ya tiene
  todo el texto del diseño a ese zoom, no algo exclusivo de MID/SIDE.
- **La mayúscula de 11 px** da más margen interior, pero al 40 % baja a
  4,4 px.

## 9. Lo que necesita E4d (telemetría y analizador)

- **`DynamicsTelemetry`:**
  - **Hoy (E4b):** ofrece la dinámica del banco STEREO solo cuando se escucha
    STEREO, y "nada que mostrar" en M/S.
  - **En E4d:** el editor indica qué banco observa (un `atomic<int>`, como
    `setActive`). El procesador ofrece los 8 valores de ese core (MID o
    SIDE), y cada instantánea lleva el banco para que el editor descarte las
    de otro banco.
- **Analizador:**
  - **Hoy:** tomas PRE/POST de L/R, sin cambios en el hilo de audio.
  - **En E4d:** en M/S, el editor deriva M = (L+R)/2 o S = (L−R)/2 de esas
    mismas tomas antes de la FFT, sin tocar el audio. Opción "Output L/R" y
    rótulos cortos ("Post · M", "Post · S", "Post · L/R").
- **Medidores IN/OUT:** sin cambios. Siempre miden la señal L/R final.
- **En E4c, mientras no llega E4d** (decisión D7):
  - en M/S no se dibuja la dinámica (como en E4b);
  - el analizador sigue mostrando L/R;
  - las pestañas DYN y ALL lo indican en su tooltip ("Mid/Side dynamics
    display: next phase").

## 10. Pruebas para impedir escrituras en el banco equivocado (y del resto)

| # | Prueba |
|---|---|
| T1 | **Por banco (STEREO, MID, SIDE):** se manipulan los 7 potenciómetros (con doble clic de reinicio), el selector de tipo, el interruptor dinámico, los 8 botones ON/OFF, el arrastre de cada nodo y los dos tiradores de Q de la banda seleccionada. **El conjunto de IDs cambiados debe ser exactamente el esperado y estar dentro del banco mostrado.** Ningún otro de los 241 cambia |
| T2 | Los botones STEREO y M/S escriben solo `proc_mode`, con un único gesto. MID y SIDE no escriben nada y no generan gestos (contados con un `AudioProcessorListener`) |
| T3 | Cambiar de banco o de modo desde la interfaz no cambia ninguno de los 241 valores, y el panel muestra exactamente los valores del banco nuevo |
| T4 | Automatización de `proc_mode`: el editor cambia de banco mostrado en un tick, sin escrituras |
| T5 | Automatización del banco no mostrado: la curva gris cambia, mientras que el panel y los nodos no |
| T6 | Un cambio de modo durante un arrastre termina el gesto, y ningún valor llega al banco nuevo |
| T7 | En STEREO, MID y SIDE no responden a clics |
| T8 | El audio es idéntico bit a bit con el editor abierto o cerrado ante cualquier acción que no sea una escritura de parámetro |
| T9 | Capturas (`labq8_ui_snapshot --e4c`) a los seis zooms en los tres estados; la diferencia de píxeles del fondo se limita al hueco de L/R |
| T10 | El banco editado sobrevive a cerrar y abrir el editor; una instancia nueva empieza en MID |
| T11 | Pista mono en M/S: el comportamiento de SIDE corresponde a la decisión D10 |
| — | Mutaciones dirigidas: banco equivocado en un potenciómetro, en un botón ON/OFF, en el arrastre de un nodo y en la instantánea; botón de banco que escribe un parámetro |

---

## 11. Archivos afectados (previstos)

| Archivo | Cambio |
|---|---|
| `src/ui/UiGeometry.h` | Rectángulos de STEREO, M/S, MID y SIDE (marcos, caras y centro del texto); posición del prefijo de la cabecera |
| `tools/ui/GenerateUiAssets.cpp` | Borrar L/R del fondo; recortar las caras encendida y apagada sin texto |
| `assets/ui/background.png` (regenerado), `assets/ui/mode_face_lit.png`, `assets/ui/mode_face_unlit.png` (nuevos) | Derivados del maestro |
| `CMakeLists.txt` | Datos binarios de los dos sprites |
| `src/ui/LabQ8Editor.{h,cpp}` | `ModeButton` y `BankButton`; banco mostrado; nuevo enlace de todos los controles; cabecera; curva gris; fin de gestos; retirada del `InactiveArea` "Stereo Mode" |
| `src/ui/UiModel.{h,cpp}` | Lectura de bandas por banco (función pura, probada) |
| `src/plugin/PluginProcessor.h` | `get/setUiEditBank` (memoria, como `uiSelectedBand`) |
| `tests/EditorTests.cpp` (o un `tests/EditorBankTests.cpp` nuevo) | T1–T11 |
| `tools/ui/UiSnapshot.cpp` | `--e4c`: capturas y diferencia de fondo |
| `docs/UI_IMPLEMENTATION.md`, `docs/ROADMAP.md` | Documentación |

**Sin cambios:** `src/dsp/*`, `src/parameters/*`, el estado v5, el PNG
maestro, la geometría del resto del chasis, los medidores, el analizador y
la telemetría.

---

## 12. Riesgos

| # | Riesgo | Mitigación |
|---|---|---|
| R1 | **Escribir en el banco equivocado** | Un solo `shownBank`; pruebas T1 y T6 y mutaciones dirigidas |
| R2 | Gesto abierto al cambiar de banco por automatización | `endDrag` antes de volver a enlazar; prueba T6 |
| R3 | Legibilidad al 40 % en pantallas 1x | Es la misma limitación que el resto del texto; maquetas para decidir D2 |
| R4 | Dos botones verdes encendidos (estilo A) que se lean como "dos modos" | Decisión D1 (estilo B) |
| R5 | Espacio de la cabecera | Opción (d), medida; no toca el fondo |
| R6 | M/S sin dinámica dibujada hasta E4d | Decisión D7 (tooltip); E4d inmediatamente después |
| R7 | Pista mono: editar SIDE no tiene efecto audible | Decisión D10 |
| R8 | Sesiones antiguas pasadas a M/S quedan con MID/SIDE planos | Decisión D8 (comandos de copia) |
| R9 | Regenerar `background.png` puede alterar otras zonas | Prueba de diferencia de píxeles limitada al hueco de L/R (T9) |
| R10 | Coste de dibujo de la curva gris | En la imagen en caché; solo se recalcula al cambiar sus parámetros |

---

## 13. Decisiones pendientes de aprobación

| | Decisión | Recomendación |
|---|---|---|
| D1 | Estilo del banco seleccionado | **A** (cara verde del maestro): identidad estricta. B si preocupa confundirlo con el modo |
| D2 | Altura de mayúscula de MID/SIDE | 13 px, igual que "M/S" (cabe) |
| D3 | Cabecera del panel en M/S | (d): "MID  BAND 4" con prefijo pequeño gris |
| D4 | MID/SIDE en modo STEREO | Atenuados y sin respuesta (spec v0.1) |
| D5 | Curva gris del otro banco | Sí, solo en M/S, en EQ y ALL, con etiqueta pequeña |
| D6 | Dónde se guarda el banco editado | En memoria por instancia; no en la sesión (sin cambios en v5) |
| D7 | DYN y analizador en M/S durante E4c | Sin dinámica y analizador en L/R, con tooltip; la funcionalidad completa en E4d |
| D8 | Comandos de copia (STEREO → MID + SIDE; MID ↔ SIDE) | Proponer incluirlos en E4c, en el menú del icono de ajustes, con gestos explícitos; no son automáticos |
| D9 | Banda seleccionada al cambiar de banco | Compartida (se mantiene la misma banda) |
| D10 | Botón SIDE en una pista mono en M/S | Atenuado, con tooltip "SIDE: no signal on a mono track"; MID editable |
