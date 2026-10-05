# AGENTS.md

Carven compiles `.cv` source files to C++ using xmake. The main areas are
`src/` (compiler), `crafts/` (runtime and libraries), `tests/`, `docs/`, and
`xmake.lua` (build configuration).

## Project documentation

- `docs/development/conventions.md` defines conventions for project-authored C++ in
  `src/`, `tests/`, and `crafts/`.
- `docs/compiler/README.md` defines the compiler pipeline, semantic
  representations, ownership and lifetime boundaries, publication gates, and
  dependency direction.
- `docs/compiler/backend/README.md` defines semantic-to-C++ realization, target-program
  construction, lowering, emission, and generated-artifact boundaries.
- `docs/development/testing.md` defines test-suite responsibilities, test placement,
  fixtures, assertions, build integration, and the validation workflow.

## Build and validation

Use the repository wrapper `./xmakew` (`.\xmakew.ps1` on Windows) for normal
local build, test, static-analysis, and clean commands. Stock Xmake is the local
fallback when the wrapper cannot apply its versioned patch.

Build or update the local compiler before testing. During implementation, run
relevant test groups or the full test suite.

```shell
./xmakew build
./xmakew test -g internal
./xmakew test -g language
./xmakew test -g interop
./xmakew test -g cli
./xmakew test -g examples
./xmakew test
```

## Build-state recovery

When an unexpected compiler, module, BMI, dependency-order, or apparently
impossible type error occurs, clean with the wrapper, rebuild, and reproduce the
failure before attributing it to implementation code:

```shell
./xmakew clean
./xmakew build
```
