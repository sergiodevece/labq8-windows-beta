# VitaPlug LABQ8 — Especificación conceptual reservada v0.1

**Fecha de decisión:** 30 de octubre de 2026  
**Estado:** dirección de producto aprobada, **NO autorizada para implementación**.  
**Prioridad actual:** finalizar y validar UI-1.1; no iniciar este trabajo ni modificar el DSP.

## 1. MODE: STEREO / M/S

- Sustituir el control provisional **L/R** contiguo al selector de modo por controles **MID** y **SIDE** para elegir el banco que se edita **solo cuando M/S está activo**.
- **STEREO**: procesado L/R con un mismo banco de ocho bandas. MID/SIDE inactivos.
- **M/S**: procesamiento Mid/Side real; el selector MID/SIDE elige qué banco de ocho bandas se visualiza y edita. Mantener ajustes separados por banco, con recuperación y persistencia fiables.
- La selección del banco de edición no equivale a activación/bypass del banco.
- **Pendiente de especificación:** arquitectura exacta y migración de estado/parametría, automatización por banco, tratamiento de entradas mono y compatibilidad con sesiones antiguas. No dibujar controles con funcionalidades simuladas.

## 2. INTENTION — intención musical

**Propósito:** punto de partida musical, no sustituto de la decisión del productor. Los presets establecerán parámetros reales de las ocho bandas (tipo, frecuencia, ganancia, Q, dinámica, umbral, rango, ataque, release, bypass). Las curvas numéricas y umbrales deberán diseñarse y validarse mediante escucha musical real; aquí se aprueban los nombres y objetivos conceptuales, **no valores de DSP**.

### Colección propuesta: 12 presets

**DRUMS**
1. **Dark Room** — batería profunda, oscura y compacta; controlar resonancias de graves/medios graves.
2. **Punch Machine** — pegada en bombo/caja, definición y limpieza de zona turbia.
3. **Open Overheads** — claridad y apertura de platos sin aspereza.

**BASS**
4. **Afterpunk Colour** — contundencia, carácter y articulación de medios tipo post-punk.
5. **Deep Foundation** — cimiento de subgrave consistente y control dinámico de notas salientes.
6. **DI to Life** — presencia y vida para bajo grabado por línea.

**GUITAR**
7. **Screaming Solo** — proyección del solo con control de durezas.
8. **Wall of Sound** — densidad de guitarras sin enturbiar la mezcla.
9. **Clean Definition** — articulación y claridad en limpias/arpegios/DI.

**VOCAL / MIX BUS / MASTER**
10. **Voice Forward** — voz presente e inteligible sin estridencias.
11. **Mix Bus – Glue** — cohesión tonal con movimientos sutiles; no equivale a compresión de bus.
12. **Master – Gentle Polish** — corrección ancha y dinámica muy moderada.

### Comportamiento propuesto

- Cargar valores reales; reflejarlos en controles, curva y automatización con seguridad.
- Identificar un preset modificado como **Modified**.
- Permitir guardar, renombrar y recuperar presets de usuario.
- En el futuro modo M/S, establecer una regla explícita para aplicar el preset **solo al banco que se esté editando**, con confirmación del alcance y sin sobrescribir bancos involuntariamente.
- No confundir texto ilustrativo («Mix Bus – Glue» actual) con un preset ya cargado.
- Calibrar umbrales dinámicos a material real; no asumir que un threshold fijo funciona igual en todas las grabaciones.

## 3. PHASE BEHAVIOR — dos escenarios confirmados

**Decisión aprobada:** contemplar **ambos** escenarios, sin elegir uno como sustituto del otro.

**A. Análisis interno / una sola pista** — estudiar respuesta de fase de los filtros, retardo de grupo y coherencia L/R/mono según los datos disponibles, sin necesidad de sidechain externo.

**B. Análisis entre señales relacionadas** — usar **sidechain externo** como referencia para analizar y proponer actuaciones sobre diferencias temporales y de fase: bombo/overheads, bombo/bajo, caja superior/inferior, dos micrófonos de guitarra, DI/micrófono, etc.

La referencia externa requiere diseño explícito de buses, límites de host, gestión de latencia, fiabilidad de mediciones y comprobaciones en Logic; no está implementada.

### Seis modos conceptuales propuestos

| Modo | Escenario | Función prevista | Estado |
|---|---|---|---|
| **NATIVE** | Interno | Mantener procesamiento EQ actual, sin compensación adicional de fase | El comportamiento base ya existe; el selector no |
| **PHASE MAP** | Interno | Visualizar fase / retardo de grupo de la cadena de EQ | I+D, sin procesado correctivo |
| **MONO WATCH** | Interno | Diagnosticar cancelaciones entre L/R al sumar a mono | I+D, diagnóstico |
| **LOW END LOCK** | Referencia externa | Análisis focalizado de coherencia temporal/fase en graves | I+D, requiere sidechain |
| **MULTI-MIC ALIGN** | Referencia externa | Estudiar desfase y retrasos entre captaciones de una misma fuente | I+D, requiere sidechain |
| **PHASE SCULPT** | Avanzado / experimental | Explorar modelado de fase por regiones, p. ej. all-pass, sin cambio intencionado de magnitud | Investigación, sujeto a viabilidad |

**Flujo previsto:** `ANALYZE → PROPOSE → APPLY`.

- **ANALYZE:** medir, delimitar frecuencias y mostrar fiabilidad/condiciones de medición.
- **PROPOSE:** sugerir ajustes concretos, reversibles y explicables; no automatizar decisiones musicales.
- **APPLY:** solo tras aprobación explícita del usuario, con A/B y posibilidad de deshacer.
- No prometer alineación perfecta, «corrección» automática universal ni que más correlación equivalga a mejor sonido.
- Diferenciar retardo temporal, polaridad y respuesta de fase dependiente de frecuencia.
- Una alternativa de fase lineal, si llegara a estudiarse, es otro diseño DSP con latencia/pre-ringing; **no está aprobada** por esta decisión.

## 4. Restricciones, dependencias y orden

1. **Ahora:** terminar UI-1.1 (zoom real, botones de bypass ON/OFF, edición de nodos y tiradores Q) y smoke en Logic.
2. **Después, según roadmap:** UI-2 y estabilidad de visualización/telemetría; revisar prioridades antes de integrar MODE M/S.
3. Diseñar e implementar **MODE M/S** por fase independiente con pruebas DSP, estado y automatización.
4. **INTENTION:** primero presets deterministas de parámetros reales, luego edición y persistencia de usuario.
5. **PHASE BEHAVIOR interno:** primero medición/visualización verificable sin procesado adicional.
6. **PHASE BEHAVIOR con referencia:** sidechain + análisis fiable; comenzar por casos de uso de interés musical, y solo después estudiar ajustes reversibles.
7. **PHASE SCULPT:** I+D opcional, condicionado a utilidad musical, estabilidad y CPU.

**Reglas transversales:** no alterar DSP ya aprobado ni PNG maestro sin aprobación explícita; no presentar controles futuros como operativos; mantener compatibilidad AU/VST3, pruebas arm64/x86_64, migración de estado, automatización y smoke real en Logic; cada etapa requiere autorización independiente.

## 5. Qué está decidido y qué no

**Decidido:** reemplazo de L/R por MID/SIDE para el modo M/S; propósito dual de PHASE BEHAVIOR (interno + referencia externa); nombres y filosofía de los modos/presets propuestos; desarrollo posterior a UI-1.1 y en fases verificables.

**No decidido:** curvas numéricas de presets, algoritmo DSP de fase, umbrales de análisis, límites del sidechain, algoritmo all-pass y correspondencias exactas entre nuevos parámetros/estados. Ninguna de esas funciones se considera implementada ni autorizada por este documento.
