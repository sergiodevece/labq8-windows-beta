# LABQ8: esquema de parámetros

## Reglas de estabilidad

1. **Un ID publicado no cambia nunca.** El host guarda la automatización y el
   estado asociados a la cadena del ID.
2. Un ID retirado no se reutiliza para otro significado.
3. *Version hint* de JUCE: cada parámetro conserva el de la fase que lo
   introdujo (E0 = 1, E1a = 2, E1b = 3, E2c.1 = 4, E4a = 5). AU lo usa para
   ordenar los parámetros.
4. Los rangos solo se amplían con migración. Nunca se recortan.
5. IDs y frecuencias por defecto: `src/parameters/ParameterIDs.h`. Rangos:
   `src/dsp/BandLimits.h`. Están escritos literalmente, sin construir cadenas,
   para que se puedan buscar con grep.
6. **El orden de host se mantiene:** las cuatro `b1_*` ocupan los índices 0–3
   en el mismo orden que en E0, las bandas 2–8 los índices 4–31 (como en
   E1a), los ocho `b{n}_type` de E1b van a continuación (32–39) y los 40
   parámetros dinámicos de E2c.1 ocupan los índices 40–79. E4a añade
   `proc_mode` (80), el banco MID (81–160) y el banco SIDE (161–240). Los
   parámetros nuevos siempre se añaden al final.
7. **Bancos (E4a).** Los 80 parámetros 0–79 son el banco STEREO y conservan
   su significado. MID y SIDE repiten exactamente su contrato con IDs
   `mid_b{n}_*` / `side_b{n}_*`. El código elige los IDs de banda con
   `params::bankOf (Bank)`. Inventario completo y verificado:
   `tests/fixtures/contract_v5_e4a.txt`; detalle en
   [MIDSIDE_E4A.md](MIDSIDE_E4A.md).

## Convención

`b{n}_{campo}` para bandas (`n` = 1..8) y `{campo}` para globales.
Minúsculas ASCII y sin espacios.

## Implementado (congelado)

Para cada banda `n` = 1..8:

| ID | Nombre | Tipo | Rango | Unidad | Por defecto | Mapeo 0..1 | Hint |
|---|---|---|---|---|---|---|---|
| `b{n}_freq` | Band n Frequency | float | 20 – 20 000 | Hz | según la tabla siguiente | logarítmico | 1 (b1), 2 (b2–b8) |
| `b{n}_gain` | Band n Gain | float | −24 – +24 | dB | 0 | lineal, paso 0,01 dB | 1 / 2 |
| `b{n}_q` | Band n Q | float | 0,1 – 18 | — | 1,0 | logarítmico | 1 / 2 |
| `b{n}_bypass` | Band n Bypass | bool | off/on | — | off | — | 1 / 2 |
| `b{n}_type` | Band n Type | choice | 6 tipos (tabla siguiente) | — | Bell (0) | índice / 5 | 3 |

En total son 40 parámetros, todos automatizables. Además, el DSP limita la
frecuencia a 0,49·fs, convierte NaN/Inf en el valor por defecto, y el
adaptador trata |ganancia| < 0,005 dB como 0 exacto (ver ARCHITECTURE.md).

**Q guardado y Q efectivo (E1b.1):**
- `b{n}_q` guarda y automatiza el valor del usuario en todo su rango (0,1–18)
  para cualquier tipo.
- El filtro usa como mucho **Q 1 en Low/High Shelf y Q 2 en Low/High Cut**;
  Bell y Notch usan el rango completo.
- El valor guardado no se modifica nunca: al volver a Bell, una banda con Q 18
  guardado vuelve a usar Q 18.
- Detalles, medidas y propuesta de UI en
  [ARCHITECTURE.md](ARCHITECTURE.md#q-guardado-y-q-efectivo-e1b1).

### Dinámica por banda (E2c.1, version hint 4, índices 40–79)

Cinco parámetros por banda, en este orden y en bloques por banda: b1 ocupa
los índices 40–44, b2 los 45–49 … y b8 los 75–79.

| Índice | ID | Nombre | Tipo | Rango | Unidad | Por defecto | Mapeo 0..1 |
|---|---|---|---|---|---|---|---|
| 40 + 5(n−1) | `b{n}_dyn_on` | Band n Dynamic EQ | bool | off / on | — | **off** | — |
| 41 + 5(n−1) | `b{n}_threshold` | Band n Threshold | float | −80 … +24 | dBFS de seno | −20 | lineal, paso 0,1 dB |
| 42 + 5(n−1) | `b{n}_range` | Band n Range | float | 0 … 24 | dB | 6 | lineal, paso 0,1 dB |
| 43 + 5(n−1) | `b{n}_attack` | Band n Attack | float | 0,5 … 500 | ms | 10 | logarítmico, rejilla 0,01 ms |
| 44 + 5(n−1) | `b{n}_release` | Band n Release | float | 5 … 5000 | ms | 100 | logarítmico, rejilla 0,1 ms |

- **Rangos:** son los rangos validados del DSP (`dsp::dynamics::limits`), sin
  cambios.
- **Unidad del umbral:** el nivel del detector en dBFS de seno. Un seno de
  amplitud A en f0 lee 20·log10(A). Ver
  [DYNAMICS_E2A.md](DYNAMICS_E2A.md#1-arquitectura-del-detector).
- **Dónde actúa:** solo en bandas **Bell** sin bypass. En los demás tipos y
  con bypass, la dinámica no actúa y la reducción se desvanece en 20 ms.
- **Suavizado:**
  - Threshold y Range: rampas de 20 ms en el DSP;
  - Dynamic EQ: fundido de 20 ms;
  - Attack y Release: cambian solo una velocidad, así que se aplican en el
    siguiente tick.
- **Por qué hay rejilla en Attack y Release:** con un mapeo logarítmico en
  float, guardar y cargar movía el valor un ulp en cada ciclo. Con la rejilla,
  el valor queda fijo y el estado se vuelve a guardar idéntico byte a byte.
  - Frequency y Q, congelados desde E0, tienen ese mismo ulp de deriva en
    ~4 % de los valores (118 de 3200 en la prueba). Es inaudible (1e-7
    relativo) y no se toca, porque cambiar su mapeo afectaría a la
    automatización guardada.

### Diferencias con el diseño gráfico APPROVED_01 (pendientes para la fase de interfaz)

`docs/design/LABQ8_UI_DESIGN_APPROVED_01.png` sigue congelado. Sus escalas
son ilustrativas; los rangos públicos reales son estos:

| Control | Escala en la imagen | Rango público real | Qué habrá que resolver en la interfaz |
|---|---|---|---|
| GAIN | −18 … +18 | −24 … +24 dB (E0) | Rótulos de la escala o extensión visual |
| Q | 0,1 … 10 | 0,1 … 18 (E0; efectivo por tipo, E1b.1) | Ídem; además, mostrar el Q efectivo |
| THRESHOLD | −60 … 0 | −80 … +24 dBFS | Rótulos de la escala |
| RANGE | 0 … 24 | 0 … 24 dB | Coinciden |
| ATTACK | 0,1 … 200 | 0,5 … 500 ms | Rótulos (el DSP no baja de 0,5 ms por decisión de producto) |
| RELEASE | 10 … 2000 | 5 … 5000 ms | Rótulos de la escala |

Cualquier cambio de los rótulos de la imagen requiere aprobación expresa
(regla 10 del registro de diseño).

### Tipos de filtro (`b{n}_type`): índices congelados

| Índice | Nombre (choice) | `dsp::FilterType` | Usa ganancia | Neutro a 0 dB |
|---|---|---|---|---|
| 0 | Bell | `bell` | sí | sí (idle) |
| 1 | Low Shelf | `lowShelf` | sí | sí (idle) |
| 2 | High Shelf | `highShelf` | sí | sí (idle) |
| 3 | Low Cut | `lowCut` | no | no: sigue filtrando |
| 4 | High Cut | `highCut` | no | no: sigue filtrando |
| 5 | Notch | `notch` | no | no: sigue filtrando |

- El índice es el valor guardado en las sesiones y el valor de automatización
  del host. **No cambia nunca.** Los tipos nuevos solo pueden añadirse con
  índices 6, 7…
- Low Cut y High Cut son de segundo orden, con 12 dB/oct. Por ahora no hay
  pendiente seleccionable.
- En Cut y Notch la ganancia **no** altera la respuesta, pero se conserva en
  el estado. Al volver a Bell o a un shelf se recupera.
- El significado de Q por tipo y las fórmulas están en
  [ARCHITECTURE.md](ARCHITECTURE.md#significado-de-frecuencia-ganancia-y-q-por-topología).
- Un índice no válido en un estado editado se ajusta a uno válido (lo hace el
  `AudioParameterChoice` de JUCE); en el DSP, un índice fuera de rango equivale a Bell.

### Frecuencias por defecto y justificación

| Banda | b1 | b2 | b3 | b4 | b5 | b6 | b7 | b8 |
|---|---|---|---|---|---|---|---|---|
| Hz | **1000** | 40 | 100 | 250 | 2500 | 5000 | 10 000 | 16 000 |
| Zona | medios | sub / bombo | graves | cuerpo / "barro" | presencia | ataque / definición | brillo | aire |

- **Todas las bandas arrancan a 0 dB y Q 1.** El estado inicial es
  transparente bit a bit (probado), y la frecuencia por defecto solo decide
  dónde empieza cada banda cuando el usuario sube o baja la ganancia.
- **b1 conserva 1 kHz**, su valor de E0. Cambiarlo no afectaría a las
  sesiones guardadas (guardan el valor explícito), pero sí alteraría el
  comportamiento validado de E0 ("volver al valor por defecto" del host y la
  prueba E0 que comprueba ese valor), y E1a pide no alterarlo.
- **b2–b8 cubren el espectro alrededor de 1 kHz** en saltos de 1–1,3
  octavas, en las zonas de trabajo habituales en mezcla. Juntas, las ocho
  frecuencias (40, 100, 250, 1k, 2,5k, 5k, 10k, 16k) quedan repartidas de
  forma casi logarítmica.
- **Consecuencia:** la numeración no está ordenada por frecuencia (la banda 1
  está en el centro). La UI final puede ordenar visualmente por frecuencia sin
  tocar los IDs.

> **Decisión abierta (antes de distribuir ninguna build):** si se prefiere un
> orden ascendente (b1 = 30 Hz … b8 = 16 kHz), el momento de cambiarlo es
> ahora, mientras no existan sesiones de usuario. El coste es modificar una
> condición de la prueba E0 de valores por defecto.

### Modo y bancos MID / SIDE (E4a, version hint 5, índices 80–240)

| ID | Nombre | Tipo | Valores | Por defecto |
|---|---|---|---|---|
| `proc_mode` | Stereo Mode | choice | 0 = Stereo, 1 = Mid/Side (índices congelados) | Stereo |
| `mid_b{n}_{campo}` | Mid Band n … | como `b{n}_{campo}` | idénticos al banco STEREO | idénticos |
| `side_b{n}_{campo}` | Side Band n … | como `b{n}_{campo}` | idénticos al banco STEREO | idénticos |

`{campo}` = freq, gain, q, bypass, type, dyn_on, threshold, range, attack,
release, en ese orden dentro de cada banda (MID 81–160, SIDE 161–240).
**En E4a el DSP aún no procesa M/S:** `proc_mode` y los bancos se guardan y
se automatizan, pero el audio es el del banco STEREO (E4b).

## Reservado para fases futuras (no implementado)

| ID | Tipo | Rango propuesto | Por defecto | Fase |
|---|---|---|---|---|
| `b{n}_slope` | choice | 6–48 dB/oct (solo filtros de corte) | 12 | por decidir |
| `out_gain` | float | −24 – +24 dB | 0 | por decidir |
| `master_bypass` | bool (con rampa, `getBypassParameter`) | | off | por decidir |
| `b{n}_place` | choice | Stereo, Left, Right, Mid, Side | Stereo | Sin fase: E4 usa bancos, no colocación por banda; el ID sigue reservado y no se reutiliza |
| `b{n}_sc` (sidechain externo) | | | | por decidir |

Los perfiles del Intention Engine no serán parámetros de host: se guardarán
como propiedades del estado.

## Estado guardado (v5, desde E4a)

Mismo formato que v4 con 241 `PARAM` (los 80 de v4, `proc_mode` y los 160
de MID/SIDE). Un estado v1–v4 se migra añadiendo `proc_mode = Stereo` y los
bancos MID/SIDE con sus valores por defecto explícitos, y suena exactamente
igual. Comportamiento en versiones anteriores y política de versiones:
[MIDSIDE_E4A.md](MIDSIDE_E4A.md) §5.

## Estado guardado (v4)

```xml
<LABQ8 labq8StateVersion="4">
  <PARAM id="b1_attack" value="10.0"/>
  <PARAM id="b1_bypass" value="0.0"/>
  <PARAM id="b1_dyn_on" value="0.0"/>
  <PARAM id="b1_freq" value="1000.0"/>
  ...
  <PARAM id="b8_type" value="0.0"/>
</LABQ8>
```

(Serializado con `copyXmlToBinary`; 80 `PARAM`.)

| Situación al restaurar | Resultado |
|---|---|
| Estado v1 (E0) | b1 se restaura; b2–b8, los ocho tipos y los 40 dinámicos reciben su valor por defecto **explícito** (tipo Bell, dinámica apagada); se vuelve a guardar como v4 con 80 valores. El resultado es idéntico byte a byte en cualquier instancia (probado con estados E0 reales) |
| Estado v2 (E1a) | Las 32 entradas se restauran; tipos y dinámica reciben su valor por defecto explícito; se vuelve a guardar como v4 (probado con estados E1a reales) |
| Estado v3 (E1b, E1b.1, E2a, E2b: mismo formato) | Las 40 entradas se restauran; los 40 dinámicos reciben su valor por defecto explícito (apagados) y la sesión suena exactamente como la ruta estática (probado con estados reales de E1b y E2b) |
| Estado v4 con parámetros ausentes | Los ausentes reciben su valor por defecto explícito |
| Sin `labq8StateVersion` | Se migra como si fuera v1 |
| `PARAM` con ID desconocido | Se conserva en el árbol y no crea parámetros |
| Corrupto, vacío o con otra raíz | Se ignora y no cambia nada |
| Después de restaurar | El audio pasa a los nuevos valores con la rampa normal de 20 ms |

Los valores restaurados pueden diferir en ~1 ulp entre arm64 y x86_64 (ajuste
a rejilla con o sin FMA). Ver ARCHITECTURE.md.

## Identidad binaria (sin cambios desde E0)

| Campo | LABQ8 | Otros VitaPlug (leídos, sin modificar) |
|---|---|---|
| Fabricante | `Vtpl` | `Vtpl` |
| Plugin code | **`Lbq8`** | `Gn80`, `Lb2C`, `D67L`, `HmR1` |
| Bundle ID | **`com.vitaplug.labq8`** | `.gainer80`, `.lab2c`, `.drive670lab`, `.harmohit.master.r1` |
| VST3 CID | `ABCDEF019182FAEB5674706C4C627138` (verificado igual al de E0) | — |

Sigue pendiente comprobar los códigos de LAB2A y VitaDAW antes de distribuir.
