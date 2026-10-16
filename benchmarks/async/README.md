# Async benchmark

```sh
./xmakew bench async
./xmakew bench async --list
./xmakew f --libcoro=/path/to/libcoro
./xmakew build
./xmakew bench async
```

The benchmark uses the main project's built Carven executable. The implementations are:

| ID | Implementation |
| --- | --- |
| `carven` | Generated Carven program using the execution runtime |
| `cpp_task` | C++20 scalar Task with lazy activation and symmetric transfer |
| `carven_runtime` | Direct C++ orchestration using the Carven execution runtime |
| `libcoro` | Lazy `coro::task` from the checkout configured with `xmake f --libcoro=...` |

The first three run by default. The optional libcoro checkout supplies its headers;
the report records its revision and local changes. `cpp_task` and `libcoro` cover
scalar results and FIFO suspension. `carven_runtime` embeds scalar leaf work and
uses a loop for chains while retaining the runtime's task context, completion
carrier and FIFO driver. Stored operations retain their cold owner.

| Case | Work per iteration |
| --- | --- |
| `ready` | Construct and await a cold scalar leaf returning `value + 1` |
| `stored` | Store a cold leaf, compute `value + 1`, then await it and add both results |
| `yield` | Construct and await a leaf that resumes from a FIFO tail |
| `static_ready` | Ready leaf selected by a static boolean input |
| `static_yield` | Yielding leaf selected by a static boolean input |
| `chain` | Arithmetic await recurrence with `depth + 1` logical steps, eligible for native folding |

All implementations use the same dynamic inputs, wrapping unsigned arithmetic
and checksum formula. Static cases select a leaf through a Carven constant
parameter or a C++ boolean template parameter; their value inputs remain dynamic.
For `chain`, the initial iteration count is `max(1, iterations / depth)`. Its reported
nanoseconds per iteration measure a whole chain of `depth + 1` logical steps.
Native optimization may reduce this arithmetic chain to a single addition; the
timing then measures optimized checksum throughput rather than per-level
coroutine work. Each case and implementation calibrates its timing iteration
count to at least 20 ms before warmups and sampling. A zero-duration calibration
observation increases the work count; timed samples require a positive interval.
Calibration stops with a failure if it cannot reach that interval within one
billion iterations or the
exact checksum limit. Calibration observations are retained separately from
samples. Later samples below 20 ms carry `below_calibration_target`; intervals
below 1 ms also carry `short_interval`. Neither is filtered from summaries.

Timing starts inside the root body after configuration reads and stops after the
last full-expression cleanup. Root construction, process startup, final exit and
printing lie outside this interval. Timing builds have no stack observation calls
or replacement allocation functions. Helpers in `cpp_task` and `libcoro` have
internal linkage; generated Carven and `carven_runtime` helpers have external
linkage. Native optimization includes those visibility differences.

A separate build counts `new` and `new[]`, including aligned and nothrow variants,
their requested bytes and peak live requested bytes. Allocations made before the
measured body, including the root frame, and direct `malloc` calls are excluded.
Counted allocations remain tracked through root teardown. Two runs must produce
identical counts and release all counted bytes before reporting. Instrumented
durations are not timing samples. These runs retain the initial iteration count,
recorded as `allocation_iterations`, independently of calibrated timing work.
`counted_allocations` divides their allocation count by that work count;
`ns_per_iteration` divides each timing interval by its own `iterations`.

The stack build enables address observations with `-O0 -fno-optimize-sibling-calls`
at chain depths 32, 512 and `stack-depth`. It records each implementation's observed
span and requires Carven's span growth to stay within 4096 bytes. Every observation
must complete successfully. The span measures sampled address range, not total stack
memory use.

Native targets use C++20, `-O3 -DNDEBUG -fno-rtti`, and the main project's
configured native toolchain. Exception support is enabled for the common
comparison. The report retains the actual compiler, linker, runtime and flags.

The runner takes warmups and then shuffled, interleaved samples with order seed
1729. It records raw intervals, minimum, median, maximum, checksums, process wall
time, host, revisions, source hashes and native build observations. Reports default
to a fresh JSON file under `build/benchmarks/async/`. Workload snapshots remain
beside the report under `<report-name>-artifacts/`; native outputs remain in the
main build directories. Changes to workloads, instrumentation, code generation
or runtime require fresh measurements. These workloads measure direct await,
suspension, allocation and chains; I/O and cross-thread scheduling require their
own workloads.

`run.lua` owns sampling and reports; `xmake.lua` declares ordinary targets in
the main project. Each `.cv` file supplies a fixed workload. `workload.cpp` runs
the C++ Task implementations, and `runtime_workload.cpp` supplies direct runtime
orchestration. Each native profile shares one `measurement.cpp` object target.
`--case` selects one workload group. The chain workload includes stack observation
programs. `measurement.hpp` and `measurement.cpp` own configuration, measured
intervals, allocation tracking and stack observations.
