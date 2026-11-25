# IAQ8 P1.1: prueba de red en el DAW

Primer riesgo de la spec (§9): comprobar que el plugin puede hacer HTTPS desde
Logic (AU, proceso aislado) y desde un host VST3.

## Resultado automatico (entorno de desarrollo, 2026-11-24)

| Comprobacion | Resultado |
|---|---|
| Build Debug (`cmake -S iaq8 -B build/iaq8-debug`), AU + VST3 | OK (`IAQ8.component`, `IAQ8.vst3` generados) |
| `ctest` (`iaq8_tests`) | 3 tests, 3 PASS |
| GET HTTPS sin clave a `https://api.anthropic.com/v1/models` (funcion `probeHttpStatus`, la misma que usa el boton) desde `iaq8_tests`, macOS 26.5 | **HTTP 401** (esperado) |
| `auval -v aufx Iaq8 Vtpl` | **No ejecutable**: `FATAL ERROR: didn't find the component`. auval solo ve componentes instalados en `~/Library/Audio/Plug-Ins/Components`; no se instalo nada. Pendiente del autor. |

Esto demuestra que el codigo HTTPS (JUCE `URL`, `JUCE_USE_CURL=0`, NSURLSession)
funciona en un proceso normal. No demuestra nada sobre Logic ni sobre un host
VST3: eso es lo que sigue.

## Pendiente del autor: Logic (AU) y host VST3

1. Compilar: `cmake -S iaq8 -B build/iaq8-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build/iaq8-debug -j8`
2. Copiar a mano `build/iaq8-debug/IAQ8_artefacts/Debug/AU/IAQ8.component` a
   `~/Library/Audio/Plug-Ins/Components/`.
3. `auval -v aufx Iaq8 Vtpl` -> debe terminar en "AU VALIDATION SUCCEEDED".
4. Abrir Logic, insertar IAQ8 como efecto, pulsar "Probar conexion": debe
   mostrar `HTTP 401`.
5. Copiar `IAQ8.vst3` a `~/Library/Audio/Plug-Ins/VST3/` y repetir el boton en
   un host VST3 (por ejemplo el AudioPluginHost de JUCE).
6. Rellenar la tabla de abajo. **Si Logic bloquea la red, parar y avisar**: el
   resto del plan depende de esto.

| Host | Version | Formato | Resultado del boton | Notas |
|---|---|---|---|---|
| Logic Pro | 13.0 (macOS 26.5) | AU | **HTTP 401** (red OK desde el proceso aislado de Logic) | 2026-11-25. `auval -v aufx Iaq8 Vtpl`: AU VALIDATION SUCCEEDED |
| (host VST3) | | VST3 | pendiente | No hay host VST3 instalado; riesgo menor (los VST3 cargan en el proceso del host) |

## Conclusion (2026-11-25)

Logic Pro 13 permite HTTPS desde IAQ8 (AU): el transporte del Plan 1 vale tal cual.
El riesgo principal de la spec §9 queda cerrado. Queda pendiente la comprobacion en
un host VST3.
