# VitaPlug LABQ8

Ecualizador paramétrico dinámico de ocho bandas (objetivo final) para mezcla,
parte de **VitaPlug LAB BUNDLE**. C++20 · JUCE 9.0.3 · CMake · AU + VST3 · macOS
Apple Silicon e Intel.

**Fase actual: E1b.1** (sobre E1b, `1e8db0b`). Ocho bandas independientes en
cascada, cada una con seis topologías seleccionables (Bell, Low Shelf, High
Shelf, Low Cut y High Cut de 12 dB/oct, Notch), crossfade sin clicks al
cambiar de tipo, bypass por banda, estado v3 compatible con sesiones E0, E1a
y E1b, editor genérico de desarrollo y 75 pruebas que se ejecutan sin DAW.
E1b.1 limita el Q efectivo de shelves (1) y cortes (2) sin tocar el Q
guardado.

## Requisitos

- macOS 11+ (objetivo de despliegue 11.0).
- Apple Command Line Tools (clang 21 probado). Xcode no es necesario.
- CMake ≥ 3.22 (probado con 4.4.3).
- JUCE 9.0.3, clonado **dentro** de este repositorio (no se comparte con otros proyectos):

```bash
git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git external/JUCE
```

Commit esperado: `be29c81492b6151c8ea8d14c840e1311963b3a83`. Otra ruta: `-DLABQ8_JUCE_DIR=/ruta/a/JUCE`.

## Compilar y probar

Siempre fuera del árbol de fuentes, en un directorio de build propio:

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug -j8
ctest --test-dir build/debug --output-on-failure
```

Release universal (arm64 + x86_64):

```bash
cmake -S . -B build/release-universal -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build/release-universal -j8
```

Ejecutable de pruebas con detalle por test (admite un filtro por nombre, por
ejemplo `dsp:`, `eq:`, `state:`, `rt:` o `perf:`). La prueba `perf:` solo se
ejecuta en Release; en Debug aparece como SKIP:

```bash
./build/debug/labq8_tests_artefacts/Debug/labq8_tests
./build/debug/labq8_tests_artefacts/Debug/labq8_tests "eq:"
```

Banco de CPU (en Release; no forma parte de ctest):

```bash
./build/release-universal/labq8_bench
```

Barrido matemático de respuesta, estabilidad y headroom de shelves y cortes
(E1b.1; tarda unos 30 s en Release):

```bash
./build/release-universal/labq8_sweep
```

## Builds de referencia en este equipo

| Directorio | Fuente | Uso |
|---|---|---|
| `build/debug`, `build/release-universal` | E0 (`dfcf25b`) | Referencia E0. No se recompila |
| `build/e1a-debug`, `build/e1a-release-universal` | E1a (`9095401`) | Referencia E1a. No se recompila |
| `build/e1b-debug`, `build/e1b-release-universal` | E1b (`1e8db0b`) | Referencia E1b. No se recompila |
| `build/e1b1-debug`, `build/e1b1-release-universal` | E1b.1 (árbol de trabajo) | Builds de E1b.1 |

Los SHA-256 de los binarios de referencia E0 y E1a se verificaron sin cambios
tras E1b, y los de E0, E1a y E1b tras E1b.1.

Comprobación de compatibilidad de audio con versiones anteriores (compila la
versión base desde git en `build/compat/`):

```bash
tests/compat/run_compat.sh all
```

## Artefactos

Los plugins quedan **solo** en el directorio de build; `COPY_PLUGIN_AFTER_BUILD`
está desactivado y nada se instala en `~/Library/Audio/Plug-Ins` ni en `/Library`:

- `build/<config>/LABQ8_artefacts/<Config>/AU/LABQ8.component`
- `build/<config>/LABQ8_artefacts/<Config>/VST3/LABQ8.vst3`

## Identidad del plugin

| Campo | Valor |
|---|---|
| Fabricante | `VitaPlug` / código `Vtpl` (compartido por la marca) |
| Código de plugin | `Lbq8` (único en VitaPlug) |
| Bundle ID | `com.vitaplug.labq8` |
| Tipo AU | `aufx` (efecto) |
| Categorías VST3 | `Fx`, `EQ` |

## Documentación

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): capas, decisiones DSP, tolerancias de las pruebas.
- [docs/PARAMETER_SCHEMA.md](docs/PARAMETER_SCHEMA.md): IDs estables, rangos y estado.
- [docs/COMPATIBILITY_E0_E1A.md](docs/COMPATIBILITY_E0_E1A.md): compatibilidad de audio de sesiones E0 en E1a.
- [docs/COMPATIBILITY_E1B.md](docs/COMPATIBILITY_E1B.md): compatibilidad de audio de sesiones E0 y E1a en E1b (`tests/compat/run_compat.sh`).
- [docs/COMPATIBILITY_E1B1.md](docs/COMPATIBILITY_E1B1.md): compatibilidad de audio de sesiones E0, E1a y E1b con el Q efectivo de E1b.1.
- [docs/ROADMAP.md](docs/ROADMAP.md): fases y pendientes tras E1b.1.
- [tests/fixtures/README.md](tests/fixtures/README.md): estados reales de E0 y E1a usados en las pruebas de migración.
