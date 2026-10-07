# Workspace benchmarks

These non-default targets generate in-memory projects and validate query results
before reporting costs. Build the compiler and benchmark in the same mode:

```shell
./xmakew f -m release
./xmakew build
./xmakew build workspace-benchmark-analysis
./xmakew build workspace-benchmark-editor
./xmakew build workspace-benchmark-navigation
```

Run commands from the repository root. On Windows use `.\xmakew.ps1`.

## Cached analysis

```shell
./xmakew run workspace-benchmark-analysis --samples 5 > /tmp/carven-workspace-cost.csv
```

The workload has 10, 100, or 500 independent modules, each with one explicitly
typed literal-returning function, and an unselected document. It reports median
host-update and query times, computation-count deltas, and weak-owner observations.
`cold_hover` starts after a complete analysis, then requests complete semantic
analysis and scoped hover together. The closure may require its first computation.
These query measurements include result verification and local-result destruction.
`warm_semantic_hover` repeats the combined query twenty times and reports time per
iteration; count deltas cover the repetition group. Updates are measured separately
from edit queries. Weak owners observe retention, not allocated bytes.

## Editor workloads

```shell
./xmakew run workspace-benchmark-editor --samples 11 --warmups 2 > /tmp/carven-editor-profiled.csv
./xmakew run workspace-benchmark-editor --samples 11 --warmups 2 --no-timings > /tmp/carven-editor-unprofiled.csv
```

The generated sources contain 100, 500, or 2000 consumer functions. Workloads cover
one multi-function module, a three-edge import chain with inferred return types or
exported constant values, and shared static function dependencies. `--size N`
selects one size. Function counts describe named functions, not generated bodies.

Each fresh host executes cold, cached, version-only, unselected-edit,
body/constant-edit, and comment-edit queries. The import workload also changes an
inferred type from `i32` to `i64`; the single-module workload introduces and repairs
a syntax error. Constant and static-function edits verify captured execution output.
Warmups execute the same sequence without emitting samples. `--samples` defaults
to 7 and admits 1..100; `--warmups` defaults to 1 and admits 0..100.

`wall_us` measures `semantic()`. Host update, result verification, hover, and result
destruction are outside that interval. `update_us` measures the preceding update,
including input copying and invalidated-cache destruction; cold and warm queries
have no measured preceding update. Their sum measures update plus query, excluding
intervening benchmark bookkeeping.

Compiler stage intervals follow [analysis timing](../../../docs/compiler/README.md#analysis-timing).
Semantic detail intervals are included in the semantic total. Workspace source
preparation measures input copying, and indexing measures occurrence-index
construction. Other work, including import-graph construction and teardown, can
remain outside stage intervals. `--no-timings` omits compiler clock instrumentation.

## Navigation workloads

```shell
./xmakew run workspace-benchmark-navigation --samples 11 --warmups 2 > /tmp/carven-navigation.csv
```

Alternating paired runs compare full-project and import-scoped analysis on equal
sources and query targets. Each fresh host measures cold and warm navigation,
an unrelated module edit, a reachable constant changing from `i32` to `i64`, and
a full check. Workloads generate 10, 100, or 500 modules with twenty functions each.
The independent graph uses these modules directly; the shared graph adds one leaf,
and the dense graph adds a root with one function importing every generated module.
The CSV `size` is the generated-module count and `functions` includes the root.
`--size N` admits 2..1000 modules. Sample and warmup options use the editor bounds.

Full mode requests `semantic()` and looks up hover in its result. Scoped mode
requests `hover()`. `wall_us` includes analysis and hover selection; scoped analysis
also includes import discovery. `update_us` includes host update and invalidated-owner release.
Type, range, publication, version-scope, and work-count verification occurs after
timing. Scoped versions and static output describe the analyzed closure.
`check_after_navigation` measures the additional full-check cost; a navigation
selection covering the project can reuse its result. Compiler stage clocks are
omitted.

Compare distributions on the same machine and build mode. Sampling order,
instrumentation, allocation warmup, and machine load affect measured costs. The
generated workloads establish their own result and retention properties; their
latencies and memory behavior do not characterize other projects. Keep raw runs
outside reference documents.
