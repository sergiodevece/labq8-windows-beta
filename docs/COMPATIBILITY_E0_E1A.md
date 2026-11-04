# Compatibilidad de audio E0 → E1a

Pregunta: ¿una sesión guardada con E0 suena igual al abrirla en E1a,
especialmente con la ganancia nominal en 0 dB, teniendo en cuenta la
corrección de ganancia neutra de E1a?

**Respuesta:** sí, salvo en una diferencia deliberada y acotada:

- **Sin pasar por 0 dB nominal, idéntica bit a bit en las dos
  arquitecturas.** Esto incluye bypass, ganancias pequeñas de ±0,01 dB,
  extremos y automatización.
- **Con la banda 1 activa en el paso 0 dB:**
  - **x86_64:** idéntica bit a bit.
  - **arm64:** difiere como máximo en 2,98e-8, es decir **−150,5 dBFS**
    (−144,5 dBFS durante las transiciones de automatización). La diferencia
    es deliberada: E1a pasa la señal sin alterar, mientras que E0 aplicaba
    una campana de −5,36e-7 dB por un artefacto de redondeo.

No se ha encontrado ningún defecto real en el DSP y no se ha modificado.

## Método

`tests/compat/run_compat.sh` (reproducible; deja todo en `build/compat/`, que Git ignora):

1. Exporta las fuentes de E0 con `git archive dfcf25b src` y compila dos
   renderizadores universales (arm64 + x86_64) con el mismo código
   (`tests/compat/CompatRender.cpp`): uno con el `LabQ8AudioProcessor` de
   E0 y otro con el de E1a.
2. **El plugin E0 crea cada sesión.** Puede ser un estado E0 real de
   `tests/fixtures/`, o un ajuste hecho como lo haría un host, con
   `setValueNotifyingHost`. El estado se guarda con `getStateInformation()`
   de E0.
3. **La misma sesión se abre** en una instancia nueva de E0 y en una
   instancia nueva de E1a (`setStateInformation`, con migración v1 → v2 en
   E1a). Las dos procesan la misma entrada con la misma automatización de
   host, aplicada antes de cada bloque.
4. **Se comparan muestra a muestra.** Para cada configuración el
   renderizador anota si la banda 1 llegó a estar activa en el paso 0 dB
   nominal (|ganancia| < 0,005 dB y sin bypass). Cualquier diferencia en una
   configuración que no haya pasado por 0 dB se marca como **UNEXPECTED** y
   la comprobación falla.

**Condiciones:** 21 sesiones × 12 configuraciones (44,1 / 48 / 96 kHz × mono /
estéreo × bloques de 64 / 512) × 2 s, en dos arquitecturas: 504
comparaciones. Entrada: ruido uniforme de ±0,25 (semilla fija) más senos de
997 Hz (0,25) y 55 Hz (0,1), con un pico de ≈ 0,6.

## Resultados (28-10-2026, Apple M5, JUCE 9.0.3)

`ident` = configuraciones idénticas bit a bit. `0dB` = configuraciones que
pasaron por 0 dB nominal. `settle` = tiempo hasta volver a ser idénticas tras
salir de 0 dB.

### arm64

| Sesión | ident | 0dB | máx \|dif\| | dBFS | settle | Veredicto |
|---|---|---|---|---|---|---|
| fixture `e0_default` (real) | 0/12 | 12/12 | 2,98e-8 | −150,5 | — | deliberada |
| fixture `e0_custom` (real, −7,25 dB) | 12/12 | 0/12 | 0 | — | — | **idéntica** |
| fixture `e0_bypassed` (real) | 12/12 | 0/12 | 0 | — | — | **idéntica** |
| 0 dB nominal, 1 kHz | 0/12 | 12/12 | 2,98e-8 | −150,5 | — | deliberada |
| 0 dB nominal, 20 Hz Q 18 | 0/12 | 12/12 | 4,66e-10 | −186,6 | — | deliberada |
| tecleado +0,004 / −0,004 / +0,0049 dB (→ paso 0) | 0/12 | 12/12 | 2,98e-8 | −150,5 | — | deliberada |
| tecleado +0,006 dB (→ +0,01) | 12/12 | 0/12 | 0 | — | — | **idéntica** |
| ±0,01 dB | 12/12 | 0/12 | 0 | — | — | **idéntica** |
| +6 dB, −24 dB/20 kHz/Q 0,1, +24 dB/20 Hz/Q 18 | 12/12 | 0/12 | 0 | — | — | **idéntica** |
| bypass activado con +12 dB | 12/12 | 0/12 | 0 | — | — | **idéntica** |
| auto: ganancia de −0,05 a +0,05 en pasos de 0,01 | 0/12 | 12/12 | 5,96e-8 | −144,5 | 20 ms | deliberada |
| auto: ganancia alternando 0 ↔ +6 dB | 0/12 | 12/12 | 5,96e-8 | −144,5 | 22 ms | deliberada |
| auto: bypass alternando con +6 dB | 12/12 | 0/12 | 0 | — | — | **idéntica** |
| auto: barrido de frecuencia a 0 dB | 0/12 | 12/12 | 2,98e-8 | −150,5 | — | deliberada |
| auto: barrido de frecuencia a +6 dB | 12/12 | 0/12 | 0 | — | — | **idéntica** |
| auto: los 4 parámetros aleatorios en cada bloque | 8/12 | 4/12 | 2,98e-8 | −150,5 | 174 ms | deliberada en las 4 que tocaron 0 dB; las otras 8, idénticas |

Resultado: OK. No hay ninguna diferencia en configuraciones que no pasaran
por 0 dB nominal (0 UNEXPECTED).

### x86_64

Las 21 sesiones son idénticas bit a bit en las 12 configuraciones (252/252),
incluidas todas las de 0 dB. En x86_64, sin FMA, el paso 0 dB de JUCE es un 0
exacto, así que E0 ya dejaba la banda en reposo.

## Interpretación de la diferencia deliberada

- **Origen:** en arm64, JUCE guarda el paso "0.00 dB" como
  `-24 + 2400·0.01f` contraído en FMA, que da **−5,364e-7 dB**. E0 aplicaba
  una campana real con esa ganancia; E1a la trata como 0 exacto, deja la
  banda en reposo y la señal pasa bit a bit.
- **Cota teórica:** |1 − 10^(−5,364e-7/20)| = 6,2e-8 de desviación relativa
  como máximo (en f0). Con un pico de entrada de ≈ 0,6 da ≈ 3,7e-8
  (−148,6 dBFS). Lo observado, 2,98e-8 (−150,5 dBFS), está por debajo.
- **Transiciones de automatización:** al salir de 0 dB, E0 arranca la rampa
  desde −5,36e-7 dB y E1a desde 0 exacto. Las salidas difieren como máximo en
  5,96e-8 (−144,5 dBFS) y vuelven a ser idénticas bit a bit en ≈ 20 ms (la
  rampa más la extinción del estado residual), o en 174 ms con ajustes
  aleatorios de Q alto en graves.
- **Alcance del umbral (|g| < 0,005 dB):** como el parámetro está en una
  rejilla de 0,01 dB, ningún valor tecleado o automatizado distinto del
  paso 0 cae dentro del umbral. +0,004, −0,004 y +0,0049 se convierten en el
  paso 0 tanto en E0 como en E1a; +0,006 se convierte en +0,01, que es
  idéntico bit a bit. **El umbral solo afecta al paso 0 dB.**
- **Audibilidad:** −144 dBFS está por debajo del ruido de cuantificación de
  24 bits (≈ −144,5 dBFS) y muy por debajo de cualquier umbral audible.

## Contrafactual: sin la corrección de E1a

He repetido la comparación en arm64 quitando temporalmente la corrección
(archivo restaurado después, comprobado con `cmp`):
**0/252 configuraciones idénticas**, incluidas sesiones normales como
`e0_custom`, +6 dB o el bypass. El motivo es que en E1a las bandas 2–8
arrancan en el paso 0 dB, que en arm64 vale −5,36e-7 dB, así que estarían
procesando. Las diferencias llegaban a 1,19e-7 (−138,5 dBFS) y nunca se
extinguían. **La corrección es lo que hace que E1a sea bit a bit compatible
con E0 en todas las sesiones fuera de 0 dB.** Informe en
`build/compat/report_without_correction.txt`; no se versiona.

## Límites de la comprobación

- Las sesiones las crea el código de E0 en este equipo (arm64), no un DAW.
  Un DAW guarda el mismo `getStateInformation()`, pero no he probado en
  Logic (sigue prohibido abrir sesiones de Logic e instalar plugins).
- x86_64 se ha ejecutado bajo Rosetta 2, que reproduce la semántica IEEE de
  x86_64 (sin FMA en el código compilado para x86_64), pero no es un Intel físico.
- Un estado guardado en arm64 y abierto en x86_64 (o al revés) da valores
  que difieren en ~1 ulp tras el ajuste a la rejilla (ver
  ARCHITECTURE.md). Esa comparación cruzada no forma parte de este
  documento, que compara E0 y E1a dentro de cada arquitectura.
