# Xmake support

The root `xmake.lua` defines targets and registers the local tasks in this
directory. The `carven-xmake-repo` package supplies the Carven source rule.

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
Deliberately unformatted formatter inputs are excluded. Formatting or parse
failures fail the command.

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

## Build configuration

Build the compiler and its module dependency with `./xmakew build` before building
or running tests, examples, or benchmarks. Keep tests, examples, benchmarks, and
development tools as non-default targets. Use the main project's compiler, Crafts,
resolved packages, and native toolchain.

The first build selects the remote rule repository by default. To develop the
two repositories together, select the local repository for that build:

```sh
CARVEN_XMAKE_REPO_DIR=/path/to/carven-xmake-repo ./xmakew build
```

Reconfigure and install local rule changes explicitly:

```sh
CARVEN_XMAKE_REPO_DIR=/path/to/carven-xmake-repo ./xmakew f -c -y
CARVEN_XMAKE_REPO_DIR=/path/to/carven-xmake-repo ./xmakew require --force -y
```

Reconfigure after changing the installed Crafts inventory.

## Shared targets

Declare sources and native settings on ordinary targets. Attach `@carven/carven`
to targets containing `.cv` sources. Declare inline-only suites as binary targets
with `tests = true`. Keep example checks with their application declarations.

Register shared sources in one producer per native configuration and reuse its
outputs with `add_deps`. Compiler, formatter, workspace, test support, and internal
fixture modules use `moduleonly` targets.

Internal, formatter, and workspace test executables share the entry object from
`carven-test-runner`. `carven-test-internal` collects the internal compiler cases
in one executable and registers six disjoint process selections for parallel
execution.

`target` defines the compilation and linking boundary. Multiple `add_tests`
registrations normally reuse that target's executable; supplying per-test
`files` clones a target and adds a separate build. Register ordinary behavior
on a shared target, and use arguments or a harness to run isolated process
scenarios. Ordinary Carven targets retain their program entry and omit runtime
test code; the rule registers their inline suites automatically for `xmake test`.
Use `tests = true` or `tests = {entry = false}` for explicitly configured suites and
register their executions with `add_tests`. Test names and `on_test` do not select
generation modes. Separate targets express different language standards, backend
settings, or entry contracts.

Choose process partitions by execution cost independently of build target count.
Xmake's test job limit controls their concurrency.

## Generation

Use one shared analysis target per semantic input set and one standard-library
static archive per native compilation context. Analysis targets publish retained
semantics and manifests. Standard-library archives consume one shared C++ generation
target and the installed native support. Consumer targets own their sources,
entries, native settings, and business dependencies.

Identical semantic inputs identify one batch and its output directory. Consumers
reuse that batch independently of target names and configuration order. Each
consumer generates C++ from the retained program into its own stable directory.
The directory identifies its native linkage domain; generated interface paths
carry that domain. Declare shared analysis and consumer generation as Xmake
preparation jobs. Connect each job to its input producers and finish generation before native
scanning. Preserve per-file native settings.

During test configuration, collect the selected automatic suites and prepare their
semantic producers together through Xmake's job graph. Register only nonempty
suites from their manifests. Reuse completed batches during the same command's
build. Repeat analysis that observed host input, time, or waiting on the next
build or test command. Publish successful generation from staging and retain
timestamps for unchanged bytes. Use Xmake dependency records, native header
scanning, compilation, linking, and job scheduling.
