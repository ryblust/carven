# Build performance pulse

`build_pulse.py` reports coarse, system-level signals for the normal Carven
build workflow.

## Signals

The pulse owns three signals:

- median end-to-end compiler wall time for a fresh 128-module batch;
- the median-time ratio between fresh 128-module and 16-module batches;
- the number of C++ objects rebuilt after a private implementation edit in a
  three-module project.

The batch modules are small and independent. They expose per-module compiler,
planning, generation, and output overhead while the ratio indicates a material
module-count scaling change. The edit fixture is a linear
`library -> facade -> app` dependency. It indicates whether a private source
edit expands downstream C++ invalidation beyond the changed implementation.
Together these signals cover fresh batch throughput, breadth scaling, and edit
locality across the compiler/build-system boundary.

## Measurement contract

Batch timings launch the configured compiler, write to a fresh output directory,
and include frontend analysis, backend generation, and artifact output. They do
not isolate an internal compiler phase. Report the 128-module median as the
coarse throughput signal and the 128-to-16 ratio as the breadth-scaling signal.

The private-edit workload performs a warm Xmake build, changes only a private
function body, rebuilds, and compares C++ object modification times. It measures
invalidation fanout, not incremental-build duration. The fixture holds one
explicit linkage domain constant throughout the measurement.

The pulse is observational: it does not run in CI, store baselines, or define an
absolute pass/fail threshold. Compare nearby revisions on the same machine and
in the same compiler mode. Debug reflects the normal edit loop; an optional
Release comparison is useful before releases or when investigating shipped
compiler performance.

## Run the pulse

Build the configured compiler first, then run:

```shell
./xmakew build
python3 benchmarks/build_pulse.py
```

The pulse reuses that exact compiler and the Carven rule package already selected
by Xmake, including a local rule package installed by a dual-repository checkout.
The default timing uses one warmup and five measured runs. Pass `--verbose` to
display raw samples and recompiled object paths.

## Structured analysis pulse

`analysis_pulse.py` measures independent functions, forward/reverse declaration
orders of call chains, and nested loops with no backedge. It launches an already
built compiler against temporary source files and discards generated C++ output.
Timings include parsing, analysis and generation; they do not isolate solver
CPU time. There are no timing assertions or stored baselines. Use the same build
mode and machine for comparisons; independent functions provide a breadth
baseline for the call chains.

```shell
./xmakew build
python3 benchmarks/analysis_pulse.py build/macosx/arm64/debug/carven
```

Pass the actual executable path for other platforms or build modes. Optional
`--samples` and `--warmups` control repetition. Semantic acceptance, rejection,
relationship identity and diagnostic checks belong in the internal tests.
