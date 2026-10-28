# VitaPlug LABQ8

Ecualizador paramétrico dinámico de ocho bandas (objetivo final) para mezcla,
parte de **VitaPlug LAB BUNDLE**. C++20 · JUCE 9.0.3 · CMake · AU + VST3 · macOS
Apple Silicon e Intel.

**Fase actual: E0.** Base mínima verificable: una banda Bell completa
(frecuencia, ganancia, Q, bypass), mono/estéreo, estado guardable, editor
genérico de desarrollo y una batería de pruebas DSP que se ejecuta sin DAW.

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

Ejecutable de pruebas con detalle por test (admite un filtro por nombre):

```bash
./build/debug/labq8_tests_artefacts/Debug/labq8_tests
./build/debug/labq8_tests_artefacts/Debug/labq8_tests "dsp:"
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
- [docs/ROADMAP.md](docs/ROADMAP.md): fases y propuesta para E1.
