# AGENTS.md

Carven compiles `.cv` source files to C++ using xmake.

## Documentation

- `docs/README.md`: document scope and maintenance.
- `docs/development/conventions.md`: C++ source layout, modules,
  naming, ownership, and failure handling.
- `docs/compiler/README.md`: pipeline, semantic representations,
  ownership, publication, and dependencies.
- `docs/compiler/backend/README.md`: target-program construction,
  realization, and emission.
- `docs/development/testing.md`: test responsibilities, evidence,
  fixtures, and validation.
- `xmake/README.md`: local tasks, module-build support, and benchmarks.

## Build and validation

Use `./xmakew` (`.\xmakew.ps1` on Windows) for local build, test, formatting,
analysis, and clean commands. Stock Xmake is the fallback when the wrapper cannot
apply its versioned patch.

Build the compiler before testing. During implementation, run the relevant
test groups or the full suite. Select the applicable test commands:

```shell
./xmakew build
./xmakew test -g internal
./xmakew test -g language
./xmakew test -g crafts
./xmakew test -g interop
./xmakew test -g cli
./xmakew test -g examples
./xmakew test -g graver
./xmakew test
```

## Build-state recovery

For unexpected compiler, module, BMI, dependency-order, or apparently impossible
type errors, clean and rebuild, then reproduce the failure before diagnosing
implementation code:

```shell
./xmakew clean
./xmakew build
```
