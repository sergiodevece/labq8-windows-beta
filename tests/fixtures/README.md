# Fixtures de estado E0, E1a, E1b y E2b

Estados reales del plugin generados con las fuentes de cada commit
(`git archive <commit> src`), compiladas con JUCE 9.0.3 en un directorio
temporal fuera del repositorio, en arm64. Los programas que los escribieron
están junto a ellos (`*_state_dump.cpp.txt`). Llaman a `getStateInformation()`
del `LabQ8AudioProcessor` de esa versión después de fijar los parámetros con
`setValueNotifyingHost`, igual que lo haría un host.

**No se deben regenerar estos archivos con código posterior:** su valor está
en ser exactamente lo que guardó cada versión.

## E0 (`dfcf25b`, estado v1) — [e0_state_dump.cpp.txt](e0_state_dump.cpp.txt)

| Archivo | Contenido (b1) | SHA-256 del .bin |
|---|---|---|
| `e0_default.bin` | instancia nueva sin tocar | `bb6204c1d018a24e7f1f3767b54368936e1db575ebe23b3e7c58a5b0a74dee65` |
| `e0_custom.bin` | 3150 Hz, −7,25 dB, Q 4,5, bypass off | `af89534ccf8d445816bbf89ca565ae9edaac4dd507403e8fa36b0472ebed968b` |
| `e0_bypassed.bin` | 120 Hz, +9 dB, Q 0,7, bypass on | `814082d9a62471c6b8de3512ee2eb11d64252b1705141fee3acb6e902931ff2a` |

Ojo: `e0_default` guarda `b1_gain = -5.364418029785156e-7`, no 0. Es el
ajuste de JUCE a la rejilla de 0,01 dB en float con FMA de arm64 (ver
docs/ARCHITECTURE.md).

## E1a (`9095401`, estado v2) — [e1a_state_dump.cpp.txt](e1a_state_dump.cpp.txt)

| Archivo | Contenido | SHA-256 del .bin |
|---|---|---|
| `e1a_default.bin` | instancia nueva sin tocar (32 parámetros) | `cf6defdc8d89a14cc9dd7e01783388c43cf916c512074436f311573515ca663d` |
| `e1a_custom.bin` | 8 bandas distintas, b3 y b7 en bypass (valores en el programa) | `cc3867ad074901983b98a5bf6d271e879925a19be44822858f57c66b03f88d51` |
| `e1a_from_e0_custom.bin` | `e0_custom.bin` abierto en E1a y guardado de nuevo (migrado v1 → v2) | `9b810297708e6b5652f0cf5f340e7dcd6cd85c038119d451378ff107084ebdb0` |

Cada `.bin` va acompañado de su `.xml` legible.

## E1b (`1e8db0b`, estado v3) — [e1b_state_dump.cpp.txt](e1b_state_dump.cpp.txt)

Para las pruebas de Q efectivo de E1b.1.

| Archivo | Contenido | SHA-256 del .bin |
|---|---|---|
| `e1b_high_q.bin` | 8 bandas con tipos; b1 Low Shelf Q 18, b2 High Shelf Q 4, b3 Low Cut Q 10 y b4 High Cut Q 2,5 (por encima del límite); Bell y Notch con Q 18; un shelf y un corte por debajo del límite | `ac3a1c332fa34079f4b920a6e7b7e0ea15b6b9769518692ca7cab079e54063b1` |
| `e1b_within_limits.bin` | 8 bandas con tipos; shelves con Q ≤ 1 y cortes con Q ≤ 2 (dos cortes justo en "2,00", guardado como 2,0000002384); b8 en bypass | `59492ba20a4e8f04ace97f99e59d43e4c84e945d0fa05451dab01ef89f4d2bdb` |

## E2b (`5623dc5`, estado v3) — [e2b_state_dump.cpp.txt](e2b_state_dump.cpp.txt)

Para la migración v3 → v4 de E2c.1. E1b.1, E2a y E2b guardan el mismo
formato v3 que E1b.

| Archivo | Contenido | SHA-256 del .bin |
|---|---|---|
| `e2b_custom.bin` | 8 bandas distintas con los seis tipos; b6 en bypass | `31c9e3c9eb61e3503c186a9d0daa834bcc5ce5ca2371277eddf6bca38b10ec61` |
