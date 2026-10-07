# Benchmarks

This directory owns workload code and fixtures. Parameterized Lua generators
construct source inputs; they do not build programs or schedule measurements.
Build/run scripts and shared sampling/reporting live in `xmake/benchmarks/`.

| Topic | Workload | Command |
| --- | --- | --- |
| `compile/` | Module batches and structured compiler inputs | `./xmakew bench compile` |
| `incremental/` | Library, facade, application, and independent module edits | `./xmakew bench incremental` |
| `async/` | Generated async, native tasks, and matched runtime orchestration | `./xmakew bench async` |

Use `--list` to inspect cases without building, `--case=<id>` to select a case,
and `--samples`/`--warmups` to select sampling. Each run uses a fresh report name.
Reports default to `build/benchmarks/<topic>/`. Source workspaces, generated C++,
objects, executables and input evidence remain in
`build/benchmarks/<topic>/<report-name>-artifacts/`, including on failure.
`--output` selects a report destination; build artifacts stay under `build/`.

Compile measures the compiler process, excluding native compilation. Incremental
measures the native build after a declared source edit. Async measures the native
program's internal interval, with separate allocation and stack observations.
See [Xmake procedures](../xmake/README.md) and [async protocol](async/README.md).
