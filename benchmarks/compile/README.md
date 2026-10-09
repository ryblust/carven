# Compile workloads

`workloads.lua` defines parameterized compiler inputs and their source construction.
Module batches emit independent source files. Structured cases exercise call
chains, shared types, matching, constant execution, ownership and expression flow.
All workloads are self-contained and use `--explicit-inputs`, so unrelated Crafts
are not discovered or analyzed.

`--command=compile` is the default. Every case starts a new compiler process and
writes generated C++ into a fresh directory for each warmup and measured sample.
Timing includes process startup, source loading, parsing, complete semantic
analysis, generation, and artifact writes. A fixed linkage domain keeps output
identities independent of the sample directory. Native C++ compilation and
linking are excluded.

`--command=check` selects the same source workloads and completes analysis,
including required compile-time execution and const tests, without generating
C++ or writing artifacts. It receives no generation options. The command selects
one measurement boundary for the run; cases are not multiplied into a matrix.
Use `--timings` to inspect compiler stages within either command.

Reports retain the generated input text, source count and byte count, exact-input
selection, selected command, and output mode. Compiler stdout and stderr are
captured, including static output. Workload and harness sources are snapshotted
and checked for changes; unused Crafts are excluded from that snapshot. Generated
source and artifact identities are recorded after measurement.

Warmups and samples are independent compiler processes. They can warm filesystem
and operating-system caches, but do not exercise retained workspace parsing or
analysis results. These measurements are the one-shot compilation baseline;
workspace reuse requires an in-process trace. Results from the earlier automatic
Crafts and mixed file/stdout protocol are not directly comparable.

```sh
./xmakew bench compile --list
./xmakew bench compile --case=modules_16 --timings
./xmakew bench compile --case=caller_first_64 --command=check --timings
```
