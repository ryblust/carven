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
Both require clang-format 23 or newer. Xmake detects it through PATH, with a
Homebrew LLVM fallback on macOS when it is absent from PATH.

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

The overlay prunes BMIs only for ordinary implementation leaves. Module source
files retain their BMIs for downstream targets.

## Standard-library compilation

Tests and examples use the packaged `@carven/carven` rule. It collects Crafts
sources and manages generation and artifact publication. Targets with matching
compiler, Crafts, native compilation settings, and prerequisite targets share
installed-library object targets. Project-local Crafts remain target inputs.

Put include paths needed only by an implementation in
`add_files(..., {includedirs = ...})`. The rule transfers `.cv` file settings to
generated C++. Put requirements used by other translation units through headers
on the target or its dependencies. Target-wide settings participate in
installed-library producer selection.
Native header imports that appear in generated public headers are requirements
of every including translation unit; use `add_includedirs(...)` on their target.

The Xmake project directory is the workspace root. Targets select sources; the
compiler manifest lists inputs and generated artifacts. Xmake's C++ rules own
native dependency scanning, compilation, linking, and build caches.

## Shared targets

Register shared sources in one producer per compilation configuration. Consumers
use `add_deps` to reuse its outputs. Compiler, formatter, workspace, test support,
and internal fixture modules use `moduleonly` targets.

Internal, formatter, and workspace test executables share the entry object from
`carven-test-runner`. `carven-test-internal` collects the internal compiler cases
in one executable and registers six disjoint process selections for parallel
execution. Source directories organize cases without splitting linkage.

`target` defines the compilation and linking boundary. Multiple `add_tests`
registrations normally reuse that target's executable; supplying per-test
`files` clones a target and adds a separate build. Register ordinary behavior
on a shared target, and use arguments or a harness to run isolated process
scenarios. Different language standards, backend flags, and entry/runner
contracts define separate targets.

Choose process partitions by execution cost independently of build target count.
The test job limit controls their concurrency. Measure preparation, compilation,
linking, execution, and queue delay separately under the same job limit.
