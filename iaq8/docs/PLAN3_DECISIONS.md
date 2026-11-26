# IAQ8 Plan 3: decisiones y pendientes

Registro de la ejecución del Plan 3 (docs/superpowers/plans/2026-11-25-iaq8-plan-3-interfaz.md):
la interfaz de IAQ8. Cada tarea pasó su revisión, y una revisión final de toda la
rama fue seguida de una tanda de arreglos (commit aa93338) que también se revisó.
LABQ8 no ha cambiado (`git diff --stat 17c9c32 -- src tests CMakeLists.txt assets docs/ROADMAP.md` vacío).

Suite al cerrar: 164 pruebas, 161 pasan, 0 fallan, 3 omitidas (las de red/E2E, opcionales).

## Decisiones tomadas durante la ejecución

Cada una dice qué cuesta si resulta equivocada.

- **R1.** Los datos binarios de IAQ8 incluyen todos los recursos de LABQ8 que usa
  `LevelMeterView` (espacio de nombres `LabQ8Assets`). Coste: errores de enlace, ya resueltos.
- **R2.** Elegir "Otro" en SOURCE pone la línea de comando en modo "nota de fuente"; el
  siguiente Intro llama a `chooseSource(other, texto)`. Ese modo termina con cualquier otra
  acción, con Escape o con un turno nuevo. Coste: un pequeño cambio de uso.
- **R3.** La sonda de red del Plan 1 vive solo en `SmokeTests.cpp` (prueba de red opcional);
  `DebugEditor` se retiró. Coste: ninguno.
- **R4.** El diálogo del código de acceso es asíncrono (sin bucles modales). Coste: ninguno.
- **R5.** El brillo del CRT usa un desenfoque gaussiano propio en vez de
  `ImageConvolutionKernel`: se parece más a la maqueta. Coste: ninguno visible.
- **R6.** El cursor cambia cada 500 ms ("2 Hz" leído como ritmo de cambio). Coste: una constante.
- **R7.** Los LED de las teclas rápidas van apagados (la maqueta encendía uno solo por decoración).
- **R8.** La fila superior (SOURCE, COMPARE, STATUS) está en `TopRow.{h,cpp}`, para no
  agrandar más `Iaq8Editor.cpp`. Coste: un archivo más.
- **R9.** El piloto ONLINE se apaga solo sin conexión, al empezar, al restaurar y al empezar
  de nuevo; un error del servidor (sin acceso, límite…) cuenta como respuesta. Coste: una línea.
- **R10.** El desplegable dice "Mezcla completa" (como las teclas y la casilla), no "Mezcla".
- **R11.** La barra de estado dice "IAQ8 OS 1.0", como la maqueta aprobada (el plan decía "IAQ8 1.0").
- **R12.** Sin propuesta pero con EQ aplicada, la barra muestra "EQ APLICADA".
- **R13.** Se añadieron a `Session` lecturas que no se guardan (`getListenedSeconds`,
  `getProposalNumber`, `getConversationNumber`, `lastRequestAnswered`) y un cuarto
  elemento a la barra de estado. No cambia el formato del estado guardado.
- **R14.** La prueba en Logic (Tarea 8, paso 4) es del autor; nada se instaló fuera del repo.
- **R15.** La tanda final arregló los 5 hallazgos importantes de la revisión final (clic derecho
  en teclas, menú junto al puntero, aviso "PENSANDO", texto escrito durante la escucha,
  curva al día si Logic cambia parámetros) y algunos menores baratos (ir al final al enviar,
  soltar el foco al escuchar, fuentes del sistema de reserva, lista de Logic en `UI.md`).

## Pendiente del autor

- La prueba en Logic: lista en `iaq8/docs/UI.md`, sección "Prueba en Logic".

## Pendientes para una siguiente iteración

- Dividir `Iaq8Editor.cpp` (unas 1050 líneas): `AssistantPanel` y menú de ajustes.
- Compartir el transporte falso de las pruebas (`tests/FakeTransport.h`, hoy copiado 3 veces).
- Limitar en el tiempo (≈33 ms) el refresco por cambios de parámetros si la automatización
  densa se nota en Logic; su prueba de agrupado no comprueba nada todavía.
- ANTES apagada cuando no hay propuesta; "Otro" elegido durante la escucha termina el modo
  nota al acabar la escucha; volver a elegir "Otro" no permite editar la nota.
- Memoria a 140 % en Retina (fondo ≈49 MB por ventana) y tiempo de reconstrucción del CRT:
  medir en Release.
- ESCUCHAR y APLICAR siguen activas durante una petición; "PENSANDO" no se alinea con la
  columna del texto.
- Ganchos de prueba en código de producción; diálogo modal que bloquea otras ventanas de IAQ8.
- Menores de las tareas: radio del brillo a 0,4 en 1x, barra de estado sin recorte, cursor que
  no se detiene (sin repintar), `background.png` de 1,95 MB solo para los medidores.
