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
| [`benchmark/incremental.lua`](benchmark/incremental.lua) | Incremental build timings, C++ object changes, and artifact checks |
| [`benchmark/compile.lua`](benchmark/compile.lua) | Module-batch and structured source-to-C++ compiler timings |
| [`benchmark/runner.lua`](benchmark/runner.lua) | Shared compiler selection, sampling, medians, and temporary-directory cleanup |
| [`benchmark/options.lua`](benchmark/options.lua) | Benchmark option parsing and validation |
| [`benchmark/report.lua`](benchmark/report.lua) | Shared sample progress and result tables |
| [`benchmark/timings.lua`](benchmark/timings.lua) | Carven timing reports, rounded durations, and bounds |

## Formatting

`format.lua` implements `format-check` to report formatting violations and
`format` to apply formatting.
Both use clang-format for `.cpp`, `.cppm`, `.h`, and `.hpp` files under `src/`,
`tests/`, `crafts/`, `examples/`, and Graver's source and test directories.
On macOS, the script queries Homebrew's local installation prefix and looks in
`opt/llvm/bin`, then falls back to PATH. Other platforms use PATH.

Both formatting commands require a built Graver and use it for `.cv` files
under `crafts/`, `examples/`, and `tests/`, plus Graver's expected-output fixtures.
Deliberately unformatted Graver inputs and the three lexical/syntax rejection fixtures listed in
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

## Benchmarks

Benchmark commands run from the repository root through `./xmakew`; Windows uses
`.\xmakew.ps1` with the same arguments. Each invocation updates the configured
compiler before measuring:

```shell
./xmakew bench compile
./xmakew bench incremental
```

Both benchmarks use the Carven executable selected by Xmake's current project
configuration. `--compiler` selects an existing executable and skips the build;
its build mode is reported as unknown. Temporary workloads are removed even
when a benchmark fails.

| Option | Meaning | Default |
| --- | --- | --- |
| `--compiler=<path>` | Use an existing compiler executable | Current `carven` target output |
| `--samples=<count>` | Positive number of measured runs | 3 for both benchmarks |
| `--warmups=<count>` | Nonnegative number of warmup runs | 1 |
| `--verbose` | Show individual samples, warmups, and changed object paths | Off |
| `--list` | List stable case identifiers and input sizes without building | Off |
| `--case=<id>` | Run one exact case identifier | All cases |
| `--output=<path>` | Save run metadata, inputs, raw samples, and summaries as JSON | No file |
| `--timings` | Include observed Carven stage timings in samples | Off |

For example, `./xmakew bench --samples=2 --warmups=0 incremental` performs two
measured runs per scenario. `./xmakew bench --help` lists the available options.
Benchmark options work before or after the topic; long options with values use
`--key=value`. Listing cases and rejecting an unknown case happen before compiler
selection or building:

```shell
./xmakew bench compile --list
./xmakew bench compile --case=independent_16 --samples=7 --warmups=2 --verbose
./xmakew bench compile --case=independent_16 --timings --output=build/bench/compile.json
./xmakew bench incremental --case=private_function_edit --output=build/bench/incremental.json
./xmakew bench incremental --case=private_function_edit --timings --output=build/bench/incremental-stages.json
```

Output shows case progress and wall-time summaries. The result table lists valid sample
counts, medians, minima, and maxima, with time columns in milliseconds. Incremental
cases also report changed and unrelated object counts. Failed or incomplete cases
are identified separately; missing measurements appear as `-`. Verbose output
includes individual samples, warmup measurements, and case details. Timings have no performance
pass/fail threshold.

### Incremental build

`benchmark/incremental.lua` uses a small `library -> facade -> app` dependency chain
alongside an unchanged, independent `unrelated` module in the same target:

| Scenario | Operation |
| --- | --- |
| No changes | Rebuild the already built project |
| Identical content rewrite | Rewrite the library with exactly the same bytes |
| Private function edit | Change a private function body in the library |
| Public interface edit | Add a field to the exported value type and initialize it |
| Add module | Add an independent exported structure |
| Remove module | Remove that independent module |

The temporary native project uses Debug mode and C++20 regardless of the selected
Carven compiler's build mode. Each scenario has its own temporary project. The
first run uses the initial build as its baseline. Later edit runs restore and
build that baseline before applying the operation; no-change runs reuse the
already built project. Setup, restoration, edits, and filesystem timestamp waits
are outside the timed region. Timings include Xmake startup, Carven generation,
native compilation, and linking as required by that build.

Each row reports the median build time and the number of new or timestamp-changed
C++ object files in the `Changed objects` column. If counts differ across measured
runs, the output shows their minimum and maximum. Counts include any cached object
files that change; deleted objects are not counted. The fixture crosses a
whole-second timestamp boundary before each operation so fast rebuilds remain
observable. Module add and remove operations also check that generated headers
and sources appear and disappear as expected. These small scenarios observe build
locality, not large project scaling.

The `Unrelated objects` column reports changed objects for the independent module.
Counts have no pass/fail threshold. Identical rewrites change the source timestamp
while preserving content and source locations.

The fixture uses the selected Carven compiler, the rule repository associated
with Xmake's selected Carven package, and the running Xmake executable. This
also supports local rule repositories used in dual-repository checkouts.

### Compile

`benchmark/compile.lua` measures source-to-C++ compilation, including process
startup, parsing, analysis, and generation. Native C++ compilation and linking
are excluded.

Module batches compile independent modules into fresh output directories;
these timings include generated-artifact writes. Structured workloads launch
the compiler on temporary source files with a fixed linkage domain and send
C++ inspection output to the null device.

| Structured workload | Input distinction |
| --- | --- |
| Independent functions and call chains | Independent bodies, caller-first order, and callee-first order |
| Test-stop chains | Test termination propagated through calls |
| Shared dependencies | Repeated nominal field dependencies and native result-type queries |
| Pattern coverage | Wide enums with one arm per case; boolean payloads with one independently constrained field per arm and a wildcard fallback |
| Constants | Repeated values and distinct values |
| Constant execution | A 1000-iteration scalar loop and repeated SIMD table construction, with static result checks |
| Nested loops | Loop nesting with explicit `break` exits |
| Mutable loop state | 512 live locals updated across a loop, then read after the loop |
| Wide argument lists | Reads alone and reads interleaved with side effects |
| Fallible calls | Repeated calls that propagate typed failures |
| Nested expressions | Nested function-call expressions |

Independent functions provide a reference for comparing call-chain timings.
Call-chain comparisons, shared dependencies, and pattern coverage use multiple
sizes to observe timing growth. The payload workload increases both field and arm
counts; its source size grows quadratically because each arm lists every field.

The output reports input sizes; workload definitions live in
[`benchmark/compile.lua`](benchmark/compile.lua). Timings include process startup, which
can dominate small inputs.

### Comparing results

Use the same machine, compiler build mode, inputs, linkage domain, and flags.
Record the compiler revision and local source changes with the results.
For comparisons between two compiler builds, preserve both executables, warm
each one, and alternate measured runs.

`--output` saves compiler and environment metadata, fixture inputs, command
records, samples, and summaries as JSON. Command records retain captured stdout
and stderr. Warmups and failed samples are excluded from summary statistics;
caught failures retain completed samples and the failure status. Serialization
happens outside the measured intervals. External compiler build modes are
reported as unknown.

`--timings` shows the valid sample closest to each case's median wall time,
identified by sample number. Its Carven invocations appear separately, with stage
durations and reported percentages of invocation total time. These are the selected
sample's measurements, not stage medians. Add `--verbose` to show every measured
sample's timing reports. JSON retains all samples and stores
each report's label, total duration, and stages in `timings.reports`; stage
`share_raw` retains a reported percentage when available. `timings.status` is
`reported` or `unreported`. `unreported` means no report was captured. Durations
are rounded; `<0.1 ms` is an upper bound. Incremental benchmarks enable the rule's
`timings` option for initial, baseline-restoration, and measured builds. Reused
generation emits no report. Compare runs with the same timing setting.
