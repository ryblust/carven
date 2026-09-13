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
| [`format.lua`](format.lua) | C++ formatting and formatting checks |
| [`generated.clang-tidy`](generated.clang-tidy) | clang-tidy overrides for generated C++ |
| [`build_pulse.lua`](build_pulse.lua) | Fresh module-batch timings and incremental C++ rebuild observations |
| [`analysis_pulse.lua`](analysis_pulse.lua) | End-to-end compiler timings for structured source workloads |
| [`benchmark.lua`](benchmark.lua) | Shared compiler selection, sampling, medians, and temporary-directory cleanup |

## Build and maintenance commands

Run commands from the repository root using `./xmakew`; on Windows, use
`.\xmakew.ps1` with the same arguments.

```shell
./xmakew build
./xmakew test
./xmakew check clang.tidy
./xmakew format-check
./xmakew format
```

`format-check` reports formatting violations; `format` applies formatting.
Both cover `.cpp`, `.cppm`, `.h`, and `.hpp` files under `src/`, `tests/`,
`crafts/`, and `examples/`. On macOS, the script first looks for clang-format
in Homebrew's LLVM installation, then falls back to PATH. Other platforms use
PATH. See [C++ conventions](../docs/conventions.md) for source conventions and
[Testing](../docs/testing.md) for test responsibilities and validation workflow.

The root build copies `.clang-tidy` into each target's generated directory and
`generated.clang-tidy` into its `rules/` subdirectory. The latter inherits the
parent configuration and adjusts parameter, borrow, and embedded-NUL checks
for generated C++.

The wrappers apply the versioned overlay in `clang-module-pipeline/`. See its
[README](clang-module-pipeline/README.md) for supported Xmake versions,
prerequisites, dependency behavior, and stock-Xmake fallback. For unexpected
compiler, module, or dependency-order failures, clean and rebuild before
diagnosing implementation code:

```shell
./xmakew clean
./xmakew build
```

## Performance pulses

The pulses report compiler timings and C++ rebuild counts for manual comparison.
Build the configured compiler before running either task:

```shell
./xmakew build
./xmakew bench-build
./xmakew bench-analysis
```

Both tasks use the existing Carven executable selected by Xmake's current project
configuration. Temporary workloads are removed after execution.

| Option | Meaning | Default |
| --- | --- | --- |
| `--compiler=<path>` | Override the compiler executable | Current `carven` target output |
| `--samples=<count>` | Positive number of measured runs | Build: 5; analysis: 3 |
| `--warmups=<count>` | Nonnegative number of warmup runs | 1 |

For example, `./xmakew bench-analysis --samples=5 --warmups=2` changes repetition.
For `bench-build`, `--verbose` shows individual samples and changed object paths.

### Build pulse

`build_pulse.lua` reports:

- median compiler wall time for fresh batches of 16 and 128 independent modules;
- the ratio of the 128-module median to the 16-module median;
- changed C++ object counts after a private function-body edit;
- changed C++ object counts after adding and removing an independent module.

Each timed batch launches the compiler and writes to a fresh output directory.
The elapsed time includes process startup, frontend analysis, backend generation,
and artifact output. The medians describe batch throughput; the ratio describes
scaling across module counts.

The incremental fixture uses a `library -> facade -> app` dependency chain with a
fixed linkage domain. It builds a temporary Debug project, applies each edit,
rebuilds, and compares C++ object modification times. Counts include new objects
and existing objects whose timestamps changed. Cached objects left by removed
sources contribute when their timestamps change. Before each edit, the fixture
waits for the filesystem timestamp to advance beyond the previous object times.

The fixture uses the selected Carven compiler, the rule repository associated
with Xmake's selected Carven package, and the running Xmake executable. This
also supports local rule repositories used in dual-repository checkouts.

### Structured analysis pulse

`analysis_pulse.lua` reports median compiler wall time for these workloads:

| Workload | Sizes |
| --- | --- |
| Independent functions and call chains in caller-first and callee-first declaration order | 64, 128, 256 functions |
| Repeated and distinct constants | 128, 256, 512, 1024 functions |
| Nested loops that exit with `break` | Depths 4, 8, 12, 16 |
| Wide operand lists with reads or interleaved effects | 64, 128, 256 operands |
| Fallible root calls | 64, 128, 256 calls |
| Nested expressions | Depths 16, 32, 64 |

Each run launches the compiler on a temporary source file with a fixed linkage
domain and directs generated C++ to the null device. Timings use `os.mclock()`
in milliseconds and include process startup, parsing, analysis, and generation.
Independent functions provide a reference for comparing call-chain timings.

### Comparing results

Use the same machine, compiler build mode, inputs, linkage domain, and flags.
Record the compiler revision and local source changes with the results.
For comparisons between two compiler builds, preserve both executables, warm
each one, and alternate measured runs.
