# Interfaz de IAQ8 (Plan 3)

Este documento explica tres cosas:

- cómo sacar las instantáneas de la interfaz;
- en qué se diferencia hoy de la maqueta aprobada
  (`docs/superpowers/specs/iaq8-mockup-v2/index.html`);
- qué falta comprobar en Logic.

## Instantáneas: `iaq8_ui_snapshot`

El programa pinta el editor sin abrir ninguna ventana y guarda una imagen PNG
de cada estado. No necesita DAW ni red. Usa un servidor falso con respuestas
fijas, y no lee ni escribe tus preferencias (`IAQ8.ui`) ni tu código de acceso.

```sh
cmake -S iaq8 -B build/iaq8-debug
cmake --build build/iaq8-debug --target iaq8_ui_snapshot
build/iaq8-debug/iaq8_ui_snapshot_artefacts/Debug/iaq8_ui_snapshot <carpeta>
```

Tarda unos 7 segundos. Escribe en `<carpeta>`, que se crea si no existe:

| Archivo | Estado | Tamaño |
| --- | --- | --- |
| `1_inicio.png` | primera pregunta: «¿Qué tienes en esta pista?» con las fuentes en las teclas | 1672 × 941 (100 %) |
| `2_escuchando.png` | fuente Voz, 7 de 12 s escuchados (barra ESCUCHANDO, piloto LISTEN) | 1672 × 941 |
| `3_propuesta.png` | la conversación de la maqueta: pregunta sobre el grave, respuesta, propuesta (−4 dB en 250 Hz, +2,5 dB desde 8 kHz), curva, explicación, respuestas rápidas en las teclas y texto en la línea de comando | 1672 × 941 |
| `4_detalle.png` | la misma propuesta con VER DETALLE (tabla de bandas) | 1672 × 941 |
| `5_sin_conexion.png` | la siguiente petición encuentra el servidor caído: aviso «Sin conexión…» y tecla REINTENTAR | 1672 × 941 |
| `6_zoom_040.png` | la propuesta al 40 % | 669 × 376 |
| `7_zoom_140.png` | la propuesta al 140 % | 2341 × 1317 |
| `8_pensando.png` | la petición de la línea de comando, aún sin respuesta: la última línea dice «IAQ8> PENSANDO...» | 1672 × 941 |

Cada píxel de la imagen es un píxel del editor, como en una pantalla normal
(1x). En una pantalla Retina, Logic dibuja el doble de píxeles. Por eso el
40 % de la imagen 6 es el caso más exigente: se ve más nítido en un Mac
Retina.

Si un estado no se alcanza, el programa lo dice y sale con código 1.

## Diferencias con la maqueta

La comparación se hizo a ojo, con las instantáneas al lado del render de la
maqueta (Chrome sin ventana, 1672 × 941).

### Corregida en esta tarea

- **«Elige la fuente…»**: el texto del desplegable SOURCE sin fuente elegida
  salía pegado al borde izquierdo. JUCE lo dibuja en el borde de la caja, no
  donde va el texto. Ahora tiene el mismo margen de 16 px que «Voz». Lo
  comprueba la prueba `ui top: source combo drives the session`.

### Sin cambios (diferencias que se aceptan)

Posiciones, tamaños de letra y colores coinciden con la maqueta. Esto incluye
la fila superior, la pantalla, las teclas, el panel ASSISTANT, las cajas de
valores y los medidores. Lo que sigue es distinto a propósito o depende de los
datos:

1. **Eje de frecuencias de la curva.** La pantalla dibuja de 20 Hz a 20 kHz.
   Las marcas 100 / 1K / 10K y los puntos de las bandas (250 Hz, 8 kHz) caen
   donde corresponde. En la maqueta, el eje es decorativo y las marcas salen
   unos 30–90 px más a la izquierda.
2. **Relleno del espectro bajo la curva.** La maqueta pinta un espectro verde
   tenue. IAQ8 no lo dibuja, porque el editor no lee el audio (regla del hilo
   de mensajes) y el resumen del análisis no tiene un espectro para mostrar.
3. **Línea de comando en minúsculas.** La maqueta la muestra en mayúsculas.
   IAQ8 enseña lo que escribes tal cual, y la pantalla lo pasa a mayúsculas
   al enviarlo. En la instantánea no se ve el cursor, porque el editor no
   tiene el foco fuera de una ventana.
4. **Pilotos y LED.** La maqueta enciende LISTEN, el LED de ESCUCHAR y el de
   MÁS CLARIDAD a modo de adorno. IAQ8 los enciende solo cuando corresponde:
   LISTEN y ESCUCHAR mientras escucha, y los LED de las respuestas rápidas
   nunca (resolución R7).
5. **DESHACER** está apagada en la propuesta, porque todavía no hay nada
   aplicado. En la maqueta está encendida.
6. **Datos de la propuesta.** La maqueta dice «PROPUESTA 2» y LEVEL MATCH
   −1.2 dB. La instantánea es la primera propuesta, y la compensación sale de
   la señal de prueba (−0.2 dB). La conversación incluye además la línea del
   objetivo («TÚ> QUE SE ENTIENDA») antes de la pregunta.
7. **Medidores.** La altura de los segmentos encendidos depende de la señal.
   La instantánea pasa ruido a unos −12 dB.
8. **Barra de estado sin propuesta.** El cuarto elemento queda vacío. Con la
   EQ aplicada dice «EQ APLICADA» (resolución R12). La maqueta solo muestra
   el caso con propuesta.
9. **Al 40 % en 1x**, la barra de estado y las marcas de frecuencia (VT323 de
   unos 7–8 px) se leen con dificultad. La conversación, las teclas y las
   cajas se leen bien. En Retina (2x) se lee todo. Al 140 % todo cabe en su
   marco y nada se sale.

## Prueba en Logic — **Pendiente (autor)**

Hay que compilar el plugin e instalarlo a mano. El repositorio nunca copia
nada en `~/Library/Audio/Plug-Ins`. Abre Logic con el lanzador local del
servidor (`Abrir IAQ8 local.command`) y comprueba esta lista:

- [ ] El flujo completo al **60 %** (el tamaño por defecto) y al **100 %**:
      elegir la fuente, escuchar, escribir el objetivo, recibir la propuesta,
      comparar ANTES/DESPUÉS, APLICAR y DESHACER.
- [ ] Escribir en la línea de comando, con acentos, espacios y Enter. Logic no
      debe quedarse ninguna tecla: la barra espaciadora no debe arrancar la
      reproducción y la R no debe grabar.
- [ ] Cerrar y volver a abrir la ventana del plugin **mientras escucha**: la
      barra y los segundos siguen donde iban.
- [ ] Cerrar y volver a abrir la ventana **mientras espera respuesta**: al
      volver se ve la propuesta (o el aviso).
- [ ] Clic derecho sobre la pantalla verde: sale el menú de ajustes (zoom,
      código de acceso, Acerca de IAQ8).
- [ ] El menú de ajustes sale **donde está el puntero**, no en una esquina
      de la ventana.
- [ ] Clic derecho sobre una tecla (APLICAR, ESCUCHAR, ENVIAR, una
      respuesta rápida): la tecla **no hace nada**, solo sale el menú. No se
      aplica la EQ, no se envía nada y no vuelve a escuchar.
- [ ] Escribir «Voz» y pulsar Enter (o pulsar ESCUCHAR). Cuando la pantalla
      dice «Dale al play…», la barra espaciadora arranca la reproducción de
      Logic. Después, un clic en la línea de comando y Enter siguen enviando.
- [ ] Mientras espera respuesta, la última línea de la pantalla dice
      «IAQ8> PENSANDO» y los puntos avanzan (uno, dos, tres). Desaparece al
      llegar la respuesta o el aviso.
- [ ] Con una EQ aplicada: Cmd-Z de Logic, la automatización en modo Read y
      la vista «Controles» de Logic cambian la curva, CHANGES y «EQ APLICADA»
      al momento.
- [ ] El desplegable SOURCE: la lista se abre justo debajo de la caja, también
      al 40 % y al 140 %.
- [ ] Los rótulos gastados (SOURCE, COMPARE, STATUS y los textos bajo el logo)
      se ven nítidos, no borrosos, a 0,6, 0,8 y 1,2.
- [ ] Con el plugin compilado en **Release**, al **140 %** en una pantalla
      **Retina**: medir cuánto tarda en rehacerse la pantalla verde una vez
      (por ejemplo, al llegar una respuesta) y cuánta memoria usa el plugin
      (Monitor de Actividad o Instruments). Apuntar las dos cifras aquí.

Resultado: _sin probar todavía._

### Puntos que solo puede confirmar un host real

- **Escala de píxeles física** (`getPhysicalPixelScaleFactor`): las capas en
  caché se pintan a la densidad de la pantalla, contando también la
  transformación del contenido (el zoom). Hay que comprobar en Logic, en
  pantalla Retina y no Retina, que nada sale borroso ni pixelado.
- **Clic derecho real.** Las pruebas usan eventos de ratón simulados. Falta
  ver que Logic entrega el clic derecho (o Ctrl+clic) al plugin y que no
  muestra su propio menú.
- **Cursor en el diálogo del código de acceso** (AlertWindow dentro del
  editor): hay que ver que el cursor se ve y parpadea, y que se puede escribir
  y pegar el código.

## Notas

- El editor provisional de Plan 1 (`DebugEditor`) se ha retirado. Sus pruebas
  las cubren ahora las del editor (`ui …`). La sonda HTTPS del Plan 1 se
  conserva solo dentro de `tests/SmokeTests.cpp`, para la prueba de red
  opcional (`IAQ8_NETWORK_TESTS=1`).
- `Session::getLimitAdjustments()` ya no la usa ninguna vista. Se conserva por
  si una línea del detalle quiere mostrar los ajustes de límites.
