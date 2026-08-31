# AGENTS.md

Carven compiles `.cv` source files to C++ using xmake. The main areas are
`src/` (compiler), `crafts/` (runtime and libraries), `tests/`, `docs/`, and
`xmake.lua` (build configuration).

## Project documentation

- `docs/conventions.md` defines conventions for project-authored C++ in
  `src/`, `tests/`, and `crafts/`.
- `docs/compiler.md` defines the compiler pipeline, semantic
  representations, ownership and lifetime boundaries, publication gates, and
  dependency direction.
- `docs/backend.md` defines semantic-to-C++ realization, target-program
  construction, lowering, emission, and generated-artifact boundaries.
- `docs/testing.md` defines test-suite responsibilities, test placement,
  fixtures, assertions, build integration, and the validation workflow.

## Build and validation

Use the repository wrapper `./xmakew` (`.\xmakew.ps1` on Windows) for normal
build, test, static-analysis, and clean commands. Stock Xmake is only the
documented fallback when the wrapper cannot apply its versioned patch.

Build before running any test. During implementation, run only the tests
relevant to the current change.

```shell
./xmakew build
./xmakew test -g internal
./xmakew test -g language
./xmakew test -g interop
./xmakew test -g cli
```

After implementation, run the full test suite, then run clang-tidy.

```shell
./xmakew test
./xmakew check clang.tidy
```

## Build-state recovery

When an unexpected compiler, module, BMI, dependency-order, or apparently
impossible type error occurs, clean with the wrapper, rebuild, and reproduce the
failure before attributing it to implementation code:

```shell
./xmakew clean
./xmakew build
```
