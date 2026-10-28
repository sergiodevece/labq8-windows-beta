# Fixtures de estado E0

Estados reales del plugin E0, generados con las fuentes del commit `dfcf25b`
(`git archive dfcf25b src`) compiladas con JUCE 9.0.3 en un directorio temporal
fuera del repositorio. El programa que los escribió está en
[e0_state_dump.cpp.txt](e0_state_dump.cpp.txt). Llama a `getStateInformation()`
del `LabQ8AudioProcessor` de E0 después de fijar los parámetros con
`setValueNotifyingHost`, igual que lo haría un host.

| Archivo | Contenido (b1) | SHA-256 del .bin |
|---|---|---|
| `e0_default.bin` | instancia nueva sin tocar | `bb6204c1d018a24e7f1f3767b54368936e1db575ebe23b3e7c58a5b0a74dee65` |
| `e0_custom.bin` | 3150 Hz, −7,25 dB, Q 4,5, bypass off | `af89534ccf8d445816bbf89ca565ae9edaac4dd507403e8fa36b0472ebed968b` |
| `e0_bypassed.bin` | 120 Hz, +9 dB, Q 0,7, bypass on | `814082d9a62471c6b8de3512ee2eb11d64252b1705141fee3acb6e902931ff2a` |

Cada `.bin` va acompañado de su `.xml` legible. Ojo: `e0_default` guarda
`b1_gain = -5.364418029785156e-7`, no 0: es el ajuste de JUCE a la rejilla de
0,01 dB en float (ver docs/ARCHITECTURE.md).

No se deben regenerar estos archivos con código posterior a E0: su valor está
en ser exactamente lo que guardó E0.
