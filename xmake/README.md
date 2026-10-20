# Xmake support

This directory contains repository formatting support, the generated-C++ analysis
profile, and the Clang module-build overlay. The root `xmake.lua` registers the
formatting tasks and applies the analysis profile.

| Path | Responsibility |
| --- | --- |
| `format.lua` | Format or check handwritten C++ and Carven sources |
| `generated.clang-tidy` | Configure clang-tidy checks for generated C++ |
| [clang-module-pipeline](clang-module-pipeline/README.md) | Wrap Xmake with versioned Clang module-build support |

## Formatting

`format.lua` implements `format` and `format-check`. Both require clang-format 23
or newer and a built `carven-format`. Run from the repository root:

```sh
./xmakew build carven-format
./xmakew format-check
./xmakew format
```

`format-check` reports violations without changing sources; `format` applies
changes. On Windows, use `.\xmakew.ps1` with the same arguments.

C++ formatting covers `.cpp`, `.cppm`, `.h`, and `.hpp` files in `src/`, `include/`,
`tests/`, `crafts/`, `examples/`, `benchmarks/`, `tools/format/src/`,
`tools/format/tests/`, and `tools/workspace/`.
Carven formatting covers `.cv` files in `crafts/`, `examples/`, `tests/`, and
`benchmarks/`, benchmark `.cv.fixture` inputs, and the formatter's expected-output
fixtures. Deliberately unformatted formatter inputs are excluded. Formatting or
parse failures fail the command.

## C++ analysis profiles

Run C++ analysis from the repository root:

```sh
./xmakew check clang.tidy
```

Analysis covers configured translation units and the project headers they include.
The base `.clang-tidy` profile covers handwritten C++ and public headers under
`include/`.

`generated.clang-tidy` inherits the base checks and adjusts parameter, borrow, and
embedded-NUL checks for generated C++. The root build publishes it as `.clang-tidy`
in Carven output directories, with the base profile in the parent directory.
Review findings against source semantics and native interface contracts. Fix
actionable generated-code findings in the generator. A local suppression names
the conflicting check and explains the semantic or interface constraint.

## Module build support

`clang-module-pipeline/` contains the POSIX and PowerShell wrappers and their
versioned Xmake patch. The root `xmakew` and `xmakew.ps1` entry points invoke them.
The overlay supports Clang module compilation and incremental dependency checks.
