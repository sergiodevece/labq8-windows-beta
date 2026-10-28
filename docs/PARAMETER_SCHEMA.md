# LABQ8: esquema de parámetros

## Reglas de estabilidad

1. **Un ID publicado no cambia nunca.** El host guarda la automatización y el
   estado asociados a la cadena del ID.
2. Un ID retirado no se reutiliza para otro significado.
3. *Version hint* de JUCE: cada parámetro conserva el de la fase que lo
   introdujo (E0 = 1, E1a = 2). AU lo usa para ordenar los parámetros.
4. Los rangos solo se amplían con migración. Nunca se recortan.
5. IDs y frecuencias por defecto: `src/parameters/ParameterIDs.h`. Rangos:
   `src/dsp/BandLimits.h`. Están escritos literalmente, sin construir cadenas,
   para que se puedan buscar con grep.
6. **El orden de host se mantiene:** las cuatro `b1_*` ocupan los índices 0–3
   en el mismo orden que en E0. Las bandas 2–8 van después.

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

En total son 32 parámetros, todos automatizables. Además, el DSP limita la
frecuencia a 0,49·fs, convierte NaN/Inf en el valor por defecto, y el
adaptador trata |ganancia| < 0,005 dB como 0 exacto (ver ARCHITECTURE.md).

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

## Reservado para fases futuras (no implementado)

| ID | Tipo | Rango propuesto | Por defecto | Fase |
|---|---|---|---|---|
| `b{n}_type` | choice | Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch… | Bell | E1b |
| `b{n}_slope` | choice | 6–48 dB/oct (solo filtros de corte) | 12 | E1b |
| `out_gain` | float | −24 – +24 dB | 0 | E1b |
| `master_bypass` | bool (con rampa, `getBypassParameter`) | | off | E1b |
| `b{n}_place` | choice | Stereo, Left, Right, Mid, Side | Stereo | Fase M/S |
| `proc_mode` | choice | Stereo, Mid/Side | Stereo | Fase M/S |
| `b{n}_dyn_on`, `_thresh`, `_range`, `_attack`, `_release`, `_sc` | | ver ROADMAP | | Fase dinámica |

Los perfiles del Intention Engine no serán parámetros de host: se guardarán
como propiedades del estado.

## Estado guardado (v2)

```xml
<LABQ8 labq8StateVersion="2">
  <PARAM id="b1_bypass" value="0.0"/>
  <PARAM id="b1_freq" value="1000.0"/>
  ...
  <PARAM id="b8_q" value="1.0"/>
</LABQ8>
```

(Serializado con `copyXmlToBinary`; 32 `PARAM`.)

| Situación al restaurar | Resultado |
|---|---|
| Estado v1 (E0) | b1 se restaura; b2–b8 reciben su valor por defecto **explícito**; se vuelve a guardar como v2 con 32 valores. El resultado es idéntico byte a byte en cualquier instancia (probado con estados E0 reales) |
| Estado v2 con parámetros ausentes | Los ausentes reciben su valor por defecto explícito |
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
