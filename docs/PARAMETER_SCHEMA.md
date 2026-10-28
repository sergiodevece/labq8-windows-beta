# LABQ8: esquema de parámetros

## Reglas de estabilidad

1. **Un ID publicado no cambia nunca.** Una vez que una build se ha usado en
   una sesión guardada, el ID (cadena) queda congelado: el host guarda la
   automatización y el estado asociados a él.
2. No se reutiliza un ID retirado para otro significado.
3. El *version hint* de JUCE (`ParameterID{id, hint}`) es 1 para todo lo
   introducido en E0. Lo que entre en fases posteriores lleva un hint mayor,
   y los existentes conservan el suyo (lo exige el orden de parámetros de AU).
4. Los rangos pueden **ampliarse** con cuidado (cambia la normalización 0..1
   de la automatización guardada), pero nunca recortarse sin migración. Ante
   la duda, se fija el rango final desde el principio.
5. Los IDs están en `src/parameters/ParameterIDs.h`; los rangos, en
   `src/dsp/BandLimits.h`.

## Convención

`b{n}_{campo}` para bandas (`n` = 1..8), `{campo}` para globales. Minúsculas
ASCII, sin espacios, cortos (AU muestra el ID en algunos hosts).

## Implementado en E0 (congelado)

| ID | Nombre | Tipo | Rango | Unidad | Por defecto | Mapeo 0..1 | Hint |
|---|---|---|---|---|---|---|---|
| `b1_freq` | Band 1 Frequency | float | 20 – 20 000 | Hz | 1000 | logarítmico (centro geométrico 632 Hz = 0,5) | 1 |
| `b1_gain` | Band 1 Gain | float | −24 – +24 | dB | 0 | lineal, paso 0,01 dB | 1 |
| `b1_q` | Band 1 Q | float | 0,1 – 18 | — | 1,0 | logarítmico | 1 |
| `b1_bypass` | Band 1 Bypass | bool | off/on | — | off | — | 1 |

Además de los rangos del host, el DSP limita la frecuencia a 0,49·fs (solo se
nota por debajo de 40,8 kHz) y trata NaN/Inf como el valor por defecto.

## Propuesta para las ocho bandas (reservado, no implementado)

Por banda (`n` = 1..8). Los campos de E0 se repiten en todas:

| ID | Tipo | Rango propuesto | Por defecto | Fase |
|---|---|---|---|---|
| `b{n}_freq` | float | 20 – 20 000 Hz, log | ver tabla de frecuencias | E1 |
| `b{n}_gain` | float | −24 – +24 dB | 0 | E1 |
| `b{n}_q` | float | 0,1 – 18, log | 1,0 (0,71 en filtros de corte) | E1 |
| `b{n}_bypass` | bool | | off | E1 |
| `b{n}_type` | choice | Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt | Bell (b1 Low Cut y b8 High Cut, a decidir) | E1 |
| `b{n}_slope` | choice | 6/12/18/24/36/48 dB/oct (solo filtros de corte) | 12 | E1/E2 |
| `b{n}_place` | choice | Stereo, Left, Right, Mid, Side | Stereo | Fase M/S |
| `b{n}_dyn_on` | bool | | off | Fase dinámica |
| `b{n}_dyn_thresh` | float | −60 – 0 dBFS | −24 | Fase dinámica |
| `b{n}_dyn_range` | float | −24 – +24 dB | 0 | Fase dinámica |
| `b{n}_dyn_attack` | float | 0,1 – 200 ms, log | 10 | Fase dinámica |
| `b{n}_dyn_release` | float | 5 – 2000 ms, log | 120 | Fase dinámica |
| `b{n}_dyn_sc` | choice | Interno, Externo | Interno | Fase dinámica |

Globales:

| ID | Tipo | Rango propuesto | Por defecto | Fase |
|---|---|---|---|---|
| `out_gain` | float | −24 – +24 dB | 0 | E1 |
| `master_bypass` | bool (con rampa, expuesto como `getBypassParameter`) | | off | E1 |
| `proc_mode` | choice | Stereo, Mid/Side | Stereo | Fase M/S |
| `analyzer_on` | — | **no es parámetro de host**: estado de UI | | Fase análisis |

Frecuencias por defecto propuestas para E1: 30, 80, 200, 500, 1 000, 2 500,
6 000 y 14 000 Hz.

> **Pendiente de decisión en E1:** E0 publica `b1_freq` con valor por defecto
> de 1 kHz. Si en E1 la banda 1 pasa a otro valor (p. ej. 30 Hz), el cambio
> solo afecta a instancias nuevas, porque los estados guardados contienen el
> valor explícito. Aun así, conviene decidirlo antes de distribuir ninguna build.

Las prioridades de perfiles e intención (Intention Engine) **no** serán
parámetros de host: se guardarán como propiedades del estado, fuera de la
automatización.

## Estado guardado

```xml
<LABQ8 labq8StateVersion="1">
  <PARAM id="b1_bypass" value="0"/>
  <PARAM id="b1_freq" value="1000"/>
  <PARAM id="b1_gain" value="0"/>
  <PARAM id="b1_q" value="1"/>
</LABQ8>
```

(Serializado con `copyXmlToBinary`.)

- **Etiqueta raíz `LABQ8`: estable.** Si se cambia, todas las sesiones dejan de cargarse.
- `labq8StateVersion` se incrementa cuando una versión necesita migrar estados
  anteriores. La migración va en `setStateInformation`.
- Un estado corrupto, vacío o con otra etiqueta raíz se ignora y los
  parámetros conservan sus valores.
- Los `PARAM` con un ID desconocido se ignoran.
- Un parámetro **ausente** en el estado conserva su valor actual (comportamiento
  de JUCE `replaceState`). A partir de E1 la migración debe reiniciarlo a su
  valor por defecto. Ver el riesgo 3 en ARCHITECTURE.md.
- Tras restaurar el estado, el audio pasa al nuevo valor con la rampa normal de 20 ms.

## Identidad binaria

| Campo | LABQ8 | Otros VitaPlug (solo lectura, para evitar colisiones) |
|---|---|---|
| Fabricante | `Vtpl` | `Vtpl` (compartido, correcto) |
| Plugin code | **`Lbq8`** | `Gn80`, `Lb2C`, `D67L`, `HmR1` |
| Bundle ID | **`com.vitaplug.labq8`** | `com.vitaplug.gainer80`, `.lab2c`, `.drive670lab`, `.harmohit.master.r1` |
| VST3 CID (derivado) | `…5674706C4C627138` | — |

`Lbq8` tiene exactamente una mayúscula, como recomienda JUCE para AU. Antes de
la primera distribución hay que comprobar que LAB2A y VitaDAW no lo usan: sus
directorios no estaban bajo `~/Documents/VitaPlug` y no se inspeccionaron.
