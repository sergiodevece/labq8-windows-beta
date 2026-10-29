# Compatibilidad de audio E0 / E1a / E1b → E1b.1

Pregunta: ¿qué sesiones guardadas suenan distinto con el Q efectivo de E1b.1
(shelves limitados a Q 1, cortes a Q 2)?

**Respuesta:**

- **Sesiones E0 y E1a:** sin cambios respecto a E1b. Solo contienen Bells, y
  la Bell no tiene límite nuevo. Las tablas son idénticas, caso por caso, a las de
  [COMPATIBILITY_E1B.md](COMPATIBILITY_E1B.md): E1a idéntica bit a bit (624/624)
  y E0 con la única diferencia deliberada de 0 dB en arm64.
- **Sesiones E1b sin shelves por encima de Q 1 ni cortes por encima de Q 2:**
  idénticas bit a bit en arm64 y x86_64. Incluye cortes justo en "2,00", que el
  host guarda como 2,0000002384 (por eso el límite tiene 1 ppm de tolerancia).
- **Sesiones E1b con algún shelf por encima de Q 1 o corte por encima de Q 2:**
  **suenan distinto, de forma deliberada.** Esa banda pierde la
  sobreoscilación o la resonancia que tenía, y lo demás no cambia. Los valores
  guardados no se modifican; al volver a Bell, la banda recupera su Q.

## Método

Es el mismo arnés que en E1b: `tests/compat/run_compat.sh all`, o `e1b` solo
para la base E1b `1e8db0b`.

1. El plugin E1b compilado desde su commit crea cada sesión y la guarda.
2. La sesión se abre en una instancia nueva de E1b y en una de E1b.1, que
   procesan la misma entrada con la misma automatización.
3. La comparación es estricta. La única excepción (`--qlimit`) son las
   configuraciones en las que, en algún bloque, un shelf tenía Q guardado > 1
   o un corte Q > 2. El arnés lo comprueba bloque a bloque con los parámetros
   del host, sin usar código de E1b.1.

**Sesiones con tipos** (solo con base E1b):

| Caso | Qué es |
|---|---|
| `fixture_e1b_within_limits` | Estado real de E1b: 8 bandas de todos los tipos, shelves con Q ≤ 1 y cortes con Q ≤ 2 (dos en "2,00") |
| `fixture_e1b_high_q` | Estado real de E1b: Low Shelf con Q 18, High Shelf con Q 4, Low Cut con Q 10, High Cut con Q 2,5, más Bell y Notch con Q 18 |
| `auto_types_random_q_le_limit` | Automatización aleatoria de tipo, f, ganancia, Q (≤ 1) y bypass en las 8 bandas |
| `auto_types_random_any_q` | Igual, con Q en todo el rango 0,1–18 |
| `auto_q_sweep_shelf_and_cut` | Q de un Low Shelf (+4 dB) y de un High Cut barrido de 0,1 a 18 durante la sesión |

**Configuraciones:** 44,1 / 48 / 96 kHz × mono / estéreo × bloques de 64 / 512,
2 s cada una, en arm64 y en x86_64 (Rosetta 2).

## Resultados (29-10-2026, Apple M5, JUCE 9.0.3)

| Base | Arquitectura | Casos | Idénticos | Diferencias | Veredicto |
|---|---|---|---|---|---|
| E0 `dfcf25b` | arm64 / x86_64 | 21 | igual que en E1b | solo la de 0 dB nominal en arm64 (E1a) | OK |
| E1a `9095401` | arm64 / x86_64 | 26 | 26 / 26 | ninguna | OK (estricto) |
| E1b `1e8db0b` | arm64 | 31 | 28 | 3 casos, todos con shelf/corte por encima del límite | OK (estricto + Q) |
| E1b `1e8db0b` | x86_64 | 31 | 28 | los mismos 3 | OK (estricto + Q) |

Los 26 casos de banda única y de ocho Bells siguen idénticos con base E1b, igual
que los dos casos con tipos dentro de los límites.

**Tamaño de las diferencias** (arm64; x86_64 da las mismas cifras):

| Caso | Pico E1b | Pico E1b.1 | RMS de la diferencia / RMS de E1b |
|---|---|---|---|
| `fixture_e1b_high_q` | +5,0 dBFS | −0,4 dBFS | −5,3 dB |
| `auto_q_sweep_shelf_and_cut` | +1,1 dBFS | −2,8 dBFS | −11,0 dB |
| `auto_types_random_any_q` | +42,0 dBFS | +35,6 dBFS | −4,9 … −1,0 dB |

- **Lectura:** las diferencias no son sutiles; es justo lo que se pretendía.
  En `fixture_e1b_high_q`, la respuesta de alguna banda cambia hasta 20,3 dB
  a 48 kHz (prueba `state: genuine E1b sessions…`), porque desaparece la
  resonancia.
- **Picos:** el pico baja en todos los casos.
- **Caso aleatorio:** ya era desmesurado en E1b (Bells de +24 dB con Q 18 en
  cascada). Su pico sigue alto en E1b.1 porque la Bell no tiene límite nuevo.

## Recomendación para sesiones existentes

- **Quién debe revisar:** solo las sesiones E1b que usen un shelf con Q > 1 o
  un corte con Q > 2. E1b no se ha distribuido (no hay instalación), así que en
  la práctica afecta a sesiones de prueba.
- **Cómo detectarlas:** la condición es comprobable en el estado guardado
  (`b{n}_type` 1/2 con `b{n}_q` > 1, o 3/4 con `b{n}_q` > 2).
- **UI:** la propuesta de ARCHITECTURE.md marcaría esas bandas.

## Límites

- Las sesiones las generan los plugins de cada versión, no un DAW. No he
  probado en Logic (sigue prohibido).
- x86_64 se ha ejecutado bajo Rosetta 2, no en un Intel físico.
