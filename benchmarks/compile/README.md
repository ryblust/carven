# Compile workloads

`workloads.lua` defines parameterized compiler inputs and their source construction.
Module batches emit independent source files. Structured cases exercise call
chains, shared types, matching, constant execution, ownership and expression flow.
The CLI loads each workload together with its installed Standard Crafts.

`--command=compile` is the default. Every case starts a new compiler process and
writes generated C++ into a fresh directory for each warmup and measured sample.
Timing includes process startup, source loading, parsing, complete semantic
analysis, generation, and artifact writes. The output directory supplies the
generated program identity. Native C++ compilation and linking are excluded.

`--command=check` selects the same source workloads and completes analysis,
including required compile-time execution and const tests, without generating
C++ or writing artifacts. The command selects one measurement boundary for the run.
Use `--timings` to inspect compiler stages within either command.

Reports retain the generated input text, workload source count and byte count,
source selection, selected command, and output mode. Compiler stdout and stderr
are captured, including static output. Workload and harness sources are
snapshotted. Installed Crafts are hashed and checked for changes. Generated
source and artifact identities are recorded after measurement.

Warmups and samples are independent compiler processes. They can warm filesystem
and operating-system caches, but do not exercise retained workspace parsing or
analysis results.

```sh
./xmakew bench compile --list
./xmakew bench compile --case=modules_16 --timings
./xmakew bench compile --case=caller_first_64 --command=check --timings
```
