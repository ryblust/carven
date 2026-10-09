# Xmake support

This directory contains build support for developing Carven. The root
[`xmake.lua`](../xmake.lua) defines targets and registers the tasks implemented
here. The Carven package and rules for compiling `.cv` projects are maintained
in the separate `carven-xmake-repo` repository, selected by the root build
configuration.

## Contents

| Path | Responsibility |
| --- | --- |
| [`clang-module-pipeline/`](clang-module-pipeline/README.md) | Versioned Xmake patch and wrappers for Clang module compilation and incremental dependency checks |
| [`format.lua`](format.lua) | C++ and Carven formatting and formatting checks |
| [`generated.clang-tidy`](generated.clang-tidy) | clang-tidy overrides for generated C++ |

## Formatting

`format.lua` implements `format-check` to report formatting violations and
`format` to apply formatting.
Both use clang-format for `.cpp`, `.cppm`, `.h`, and `.hpp` files under `src/`,
`tests/`, `crafts/`, `examples/`, `benchmarks/`, `tools/format/`, and `tools/workspace/`.
On macOS, the script queries Homebrew's local installation prefix and looks in
`opt/llvm/bin`, then falls back to PATH. Other platforms use PATH.

Both formatting commands require a built `carven-format` and use it for `.cv` files
under `crafts/`, `examples/`, `tests/`, and `benchmarks/`, plus benchmark
`.cv.fixture` inputs and the formatter’s expected-output fixtures.
Deliberately unformatted formatter inputs and the three lexical/syntax rejection fixtures listed in
`format.lua` are excluded. Other formatting or parse failures fail the command.

## Generated-code analysis profile

The root build copies `.clang-tidy` into each target's generated directory and
`generated.clang-tidy` into its `rules/` subdirectory. The latter inherits the
parent configuration and adjusts parameter, borrow, and embedded-NUL checks
for generated C++.

## Module build support

`clang-module-pipeline/` supplies the versioned Xmake overlay for Clang module
compilation and incremental dependency checks. The root `xmakew` and `xmakew.ps1`
entry points launch its wrappers.

## Standard-library compilation

Repository tests and examples attach the packaged `@carven/carven` rule. The
rule owns installed Crafts source collection, generation ordering, and native
object dependencies. Targets with matching compiler, Crafts, and native
compilation settings and prerequisite targets share the installed library's
objects. Target settings
select the configuration automatically; project-local Crafts remain target inputs.

The Xmake project directory is the workspace root. Each target supplies its
selected sources and compilation identity; the compiler's manifest reports that
selection and its generated artifacts. The packaged rule owns source discovery,
generation reuse and artifact promotion. Xmake's ordinary C++ rules own native
dependencies, compilation, linking and their caches.

Compiler, formatter, and workspace modules use `moduleonly` and reuse their BMIs.
Workspace scenarios share the compiler's internal test executable.
