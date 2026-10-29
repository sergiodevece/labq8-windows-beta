# Fixtures de estado E0 y E1a

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
