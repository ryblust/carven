# AGENTS.md

Carven compiles `.cv` source files to C++ using xmake. The main areas are
`src/` (compiler), `crafts/` (runtime and libraries), `tests/`, `docs/`, and
`xmake.lua` (build configuration).

Before changing C++ in `src/`, `tests/`, or `crafts/`, read and follow
`docs/conventions.md`. Changes in `src/` also require `docs/compiler.md` and
`docs/backend.md`. Before changing tests or their build configuration, read
and follow `docs/testing.md`.

Run normal project build, test, static analysis, and clean commands with the
repository wrapper `./xmakew` (`.\xmakew.ps1` on Windows). Stock Xmake is only
the documented fallback when the wrapper cannot apply its versioned patch.

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

When an unexpected compiler, module, BMI, dependency-order, or apparently
impossible type error occurs, clean with the wrapper, rebuild, and reproduce the
failure before attributing it to implementation code:

```shell
./xmakew clean
./xmakew build
```
