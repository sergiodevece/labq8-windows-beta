# Compatibilidad de audio E0 / E1a → E1b

Pregunta: ¿las sesiones guardadas con E0 y con E1a suenan igual en E1b, donde
cada banda ha pasado a ser un `EqBand` multi-filtro?

**Respuesta:**

- **Sesiones E1a:** idénticas bit a bit en E1b, en las dos arquitecturas, en
  las **624** comparaciones. Es una comprobación estricta: cualquier
  diferencia la habría hecho fallar.
- **Sesiones E0:** mismo resultado que en E1a. Las diferencias solo aparecen
  en arm64 y solo con la banda 1 en 0 dB nominal: 2,98e-8 como máximo
  (−150,5 dBFS) y −144,5 dBFS en transiciones. Es la corrección deliberada de
  E1a, documentada en [COMPATIBILITY_E0_E1A.md](COMPATIBILITY_E0_E1A.md).
  E1b no añade ninguna diferencia nueva.

## Método

`tests/compat/run_compat.sh all` (reproducible; todo en `build/compat/<base>/`,
que Git ignora). Por cada versión base:

1. Exporta sus fuentes con `git archive <commit> src` y compila dos
   renderizadores universales con el mismo código
   (`tests/compat/CompatRender.cpp`): uno con el `LabQ8AudioProcessor` de la
   base y otro con el actual.
2. **El plugin base crea cada sesión.** Puede ser un estado real de
   `tests/fixtures/` o un ajuste hecho como lo haría un host. La guarda con su
   propio `getStateInformation()`.
3. **La misma sesión se abre** en una instancia nueva de la base y en una
   instancia nueva de E1b (con migración a v3), y las dos procesan la misma
   entrada con la misma automatización de host antes de cada bloque.
4. **Se comparan muestra a muestra.** Con E0 se permite solo la diferencia de
   0 dB nominal, y el arnés comprueba configuración a configuración que la
   explica. Con E1a se usa `--strict` y no se permite ninguna diferencia.

**Configuraciones por caso:** 44,1 / 48 / 96 kHz × mono / estéreo × bloques
de 64 / 512, 2 s cada una, en arm64 y x86_64 (Rosetta 2).

**Sesiones:**
- Base E0: 21 sesiones de banda 1 (las de COMPATIBILITY_E0_E1A).
- Base E1a: las mismas 21, creadas por E1a, más 5 de ocho bandas: los tres
  fixtures E1a reales (`e1a_default`, `e1a_custom` con 8 bandas distintas y 2
  en bypass, y `e1a_from_e0_custom`), la automatización aleatoria de las 8
  bandas y el bypass alternado en las 8.

## Resultados (29-10-2026, Apple M5, JUCE 9.0.3)

| Base | Arquitectura | Sesiones | Comparaciones | Idénticas | Diferencias | Veredicto |
|---|---|---|---|---|---|---|
| E1a `9095401` | arm64 | 26 | 312 | **312** | ninguna | OK (estricto) |
| E1a `9095401` | x86_64 | 26 | 312 | **312** | ninguna | OK (estricto) |
| E0 `dfcf25b` | arm64 | 21 | 252 | 156 | 96 configuraciones que pasan por 0 dB nominal en b1: máx. 2,98e-8 (−150,5 dBFS), 5,96e-8 en transiciones | OK (diferencia deliberada de E1a, sin cambios) |
| E0 `dfcf25b` | x86_64 | 21 | 252 | **252** | ninguna | OK |

Las cifras de la base E0 en E1b coinciden caso por caso con las de la base
E0 en E1a (mismo número de muestras distintas, mismos máximos y mismos
tiempos de convergencia).

## Por qué E1b no cambia el sonido de sesiones anteriores

- Las sesiones v1 y v2 no tienen `b{n}_type`. La migración v3 escribe Bell
  explícito en las ocho bandas.
- `EqBand` en modo Bell repite exactamente la aritmética de `BellBand` (E0),
  con el mismo suavizado, el mismo tick y la misma regla de idle. Lo verifican
  la prueba unitaria (`band: Bell type is bit-identical to the frozen E0
  BellBand`), la prueba E1a que compara la cascada con 8 `BellBand`, y esta
  comparación con los binarios de E0 y E1a compilados desde sus commits.
- El bypass de Bell sigue siendo una rampa de ganancia hasta 0 dB, así que
  las sesiones con bypass automatizado (`auto_bypass_toggle_gain_6`,
  `auto_8band_bypass_toggles`) son idénticas.

## Límites

- Las sesiones las generan los plugins de cada versión, no un DAW. No he
  probado en Logic (sigue prohibido).
- x86_64 se ha ejecutado bajo Rosetta 2, no en un Intel físico.
- La comparación es dentro de cada arquitectura (ver la sección sobre FMA en
  ARCHITECTURE.md).
