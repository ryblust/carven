# Incremental builds

`xmake.lua` registers one ordinary main-project target for all edit scenarios.
Its writable sources live under the configured build directory's
`benchmarks/incremental/workload/`. Xmake chooses native output and generated-file
paths. The target uses the main project's resolved Carven dependency. The source graph is
`library -> facade -> app`, with an independent `unrelated` module.
`workloads.lua` identifies baseline and replacement fixtures.

Run after building the compiler:

```shell
./xmakew bench incremental
./xmakew bench incremental --case=private_function_edit --samples=2 --warmups=0 --timings
```

Each scenario resets the workload sources, builds its baseline, applies an
operation and builds the same target. Later samples restore the baseline before
another edit. No-change samples reuse the built target. Preparation, restoration,
timestamp waits, observation snapshots and executable validation are outside the
measured interval.

The target uses C++20 with optimization disabled. Standard-library analysis and
generated C++ text are shared through the main build graph. The target compiles
the standard-library sources with its own native configuration. Stage timings
distinguish semantic analysis from C++ generation; `--timings` prints stage details
for the sample nearest the wall-time median.

Reports count generated artifact additions, byte changes and removals; unchanged
bytes with changed timestamps; and new or timestamp-changed native objects.
Deleted objects are excluded. The independent module has its own changed-object
count, which must remain zero in every scenario. Each successful sample runs the
resulting program and checks its value.
No-change and identical-content rewrites require zero Carven invocations, zero
object changes, and unchanged generated contents and timestamps.

These cases measure source-edit locality. They do not establish large-project
scaling or cross-target isolation by themselves.
