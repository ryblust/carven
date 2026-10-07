# Async benchmark

Run the compiler and generated programs through the repository wrapper:

```sh
./xmakew bench async --list
./xmakew bench async --cxx=/path/to/clang++ --output=build/benchmarks/async/run.json
./xmakew bench async --cxx=/path/to/clang++ --libcoro=/path/to/libcoro \
    --samples=7 --iterations=200000 --depth=1024 --stack-depth=16384 \
    --output=build/benchmarks/async/comparison.json
```

`--compiler` selects an already built Carven executable. The default comparison
includes Carven, an independent handwritten C++20 coroutine, and a handwritten
C++ orchestration using the actual Carven runtime; `--libcoro` adds the official
[jbaldwin/libcoro](https://github.com/jbaldwin/libcoro) lazy `task` from an external
checkout. The harness records its commit and local changes. It does not install or
vendor a library. Choose a fresh output name for each run; source snapshots and
native build artifacts are kept under
`build/benchmarks/async/<report-name>-artifacts/`. Xmake and a C++20 native toolchain
are required. Reports default to a fresh JSON file under `build/benchmarks/async/`.
`--cxx` binds both the native compile and link driver through Xmake. Native
projects select LLVM-MinGW on Windows and LLVM on Unix; Windows and Linux select
libc++ explicitly. The report records actual compiler, linker, runtime selection
and resolved flags. Recorded macOS runs below do not establish Windows or Linux
execution evidence.

| Case | Work per iteration |
| --- | --- |
| `ready` | Construct and await a cold scalar leaf returning `value + 1` |
| `stored` | Store a cold leaf, compute `value + 1`, then await the saved operation and add both results |
| `yield` | Construct and await a leaf that resumes from a FIFO tail |
| `static_ready` | Ready leaf selected by a static boolean input |
| `static_yield` | Yielding leaf selected by a static boolean input |
| `chain` | Execute `depth + 1` logical steps of a dynamic recursive chain |

The same dynamic inputs and checksum formula apply to all implementations.
Unsigned integer arithmetic wraps in both Carven and native C++. Inputs and calls
are dynamic. `stored` uses a named cold owner across a statement boundary; its
checksum is twice the ready checksum. It measures whether the saved owner still
permits native frame elision. `static_ready` and `static_yield` use a Carven
`const` parameter and `const if` in a synchronous cold factory that selects a
ready or yielding async leaf; the native baselines use a boolean template
parameter and `if constexpr` in the matching cold factory. The value input remains
dynamic. The original ready/yield cases retain their ordinary boolean parameter.
Stack observations are disabled in timed runs. For `chain`, the iteration count
is `max(1, iterations / depth)`, so the work budget stays comparable across depth
selections. Reported nanoseconds per iteration therefore describe a
whole chain; divide by `depth + 1` for approximate cost per logical step.

Native Xmake targets request C++20, `-O3 -DNDEBUG -fno-rtti`, the same native compiler,
and the same probe translation unit. Exception support is enabled in the common
comparison because libcoro's task transports C++ exceptions. Carven additionally
compiles and executes the workload with `-fno-exceptions`. Samples run round robin
in deterministically shuffled order, after warmups (order seed 1729); the report retains raw times,
minimum, median, maximum, checksum, host, revisions, source hashes, target/toolchain
identities, native build steps, and input snapshots. The shared runner records
process wall time separately from the program-internal primary timing metric.

Timing starts inside the root body after configuration reads and stops after the
last full-expression cleanup. It excludes compiler work, process startup, root
construction, entry/final exit, and printing. Timing builds do not replace global
allocation functions. A separate instrumented build counts `new`/`new[]`, including
aligned and nothrow variants, their requested bytes, and peak live requested
bytes. It excludes `malloc` and the root frame constructed before the measured
body. Two runs must give identical counts and release all counted bytes before
reporting. Those instrumented durations are never timing samples.

A third build uses `-O0 -fno-optimize-sibling-calls` and observes native stack
addresses in chains of depth 32, 512, and `stack-depth`. The Carven run must finish
with stack span growth no greater than 4096 bytes. Native baseline crashes remain
failed stack observations; they are not valid performance samples. Symmetric
transfer can bound the native stack in a conventional C++ implementation as well.
The report retains each implementation's observed spans.

These kernels compare direct await, suspension, allocation, and deep chains.
Carven also implements lexical child closure, distinct cooperative cancellation,
and typed failures. The handwritten and libcoro kernels do not implement those
additional contracts. `handwritten_runtime` retains the actual Carven runtime
and directly embeds ready/yielding scalar work or a chain loop; stored operations
keep their cold owner. This comparison isolates source orchestration under the
same runtime rather than comparing independent runtime designs. These kernels
do not exercise every runtime contract. Contract tests establish those additional
semantics. These measurements apply to the listed kernels on the recorded host; I/O and
cross-thread scheduling need separate workloads. These are whole-program
comparisons: native helpers have internal linkage and generated Carven helpers
have external linkage, as do `handwritten_runtime` helpers. Differences include
code visibility and optimization opportunities as well as runtime work.

The report records the measured implementation and generated inputs. Runtime and
code generation changes require a new run under the same protocol.

## Recorded comparison

The 2026-10-06 `build/benchmarks/layout-validation/async-all.json` run records
all six kernels and four implementations on an Apple M1 Pro, macOS, and Clang
23.1.2. Eleven measured samples follow three warmups, with a 2,000,000-iteration
budget and chain depth 1,024. The compiler is the uncommitted implementation based
on revision `2323653fcf2a083ffa2d5d57352dfa6c17265c8e`; the report identifies its
release executable by SHA-256
`81dde6555f27b7a46429bd2f343043d2e6b61f348f6fdacf22d6ad9351c3708a`.
The archived inputs and native configuration define the scope of these results.

| Median time | Carven | Handwritten + Carven runtime | Handwritten task | libcoro task |
| --- | ---: | ---: | ---: | ---: |
| Ready, ns/work | 1.31 | 1.32 | 3.62 | 30.29 |
| Stored, ns/work | 4.41 | 5.19 | 4.27 | 36.35 |
| Yield, ns/work | 6.59 | 6.99 | 7.88 | 40.84 |
| Static ready, ns/work | 1.61 | 1.48 | 5.67 | 33.58 |
| Static yield, ns/work | 6.88 | 6.95 | 9.32 | 41.69 |
| Chain, ns/whole chain | 1,374.34 | 1,594.64 | 29,282.60 | 32,880.63 |

The report retains ranges and raw samples. All checksums match and all forty-eight
instrumented checks balance. Carven and matched runtime orchestration count zero
allocations in every kernel; the independent task and libcoro retain respectively
1,024 and 1,025 allocations per chain. Six Carven no-exception executions and
twelve unoptimized stack observations complete. The manifest identifies 240
retained native inputs. These observations do not attribute timing changes to
harness or directory changes and supply no portable performance threshold.

## Provider measurement boundaries

The retained pipe experiments are separate from `bench async`. Generated and C++
Operation roots share an external provider object in separate translation units
without LTO. Ready-I/O timing starts inside the root body and includes write/read
syscalls and provider cleanup. Registered recovery starts before `drive_root`
and ends after return, including controlled byte injection, poll, checks, clock
reads, provider frames and root teardown. A pending iteration starts from an empty
nonblocking pipe, reaches EAGAIN, and receives one byte at the pump's idle boundary.
Mixed runs alternate ready and registered recovery.

These experiments check registration, wait, commit, byte delivery and provider
cleanup under their recorded scopes. They do not establish blocked OS wakeup
latency, syscall-only cost, allocation counts, or external-library callback
quiescence. Their input snapshots and samples remain separate evidence for the
measured provider and source/native bridge.

## Evidence index

Reports are local artifacts under `build/benchmarks/`. Each completed kernel run
retains generated programs, native build observations, binaries, samples and an
input manifest. Inputs include runtime headers, benchmark code, generated sources,
build scripts and external libcoro headers when selected. Compiler and input
hashes identify the measured implementation. Earlier reports retain their original
harness commands and archive formats; changed implementations require fresh runs.

Paths below are relative to `build/benchmarks/`:

| Artifact | Evidence |
| --- | --- |
| `main-integration/{async,compile,incremental,validation}.json` | Four-implementation async measurements, compiler and incremental-build observations, and full-suite acceptance after main integration |
| `review-convergence/{review-async,validation}.json` | Async review acceptance before main integration |
| `layout-validation/{async-all,validation}.json` | Four-implementation Xmake run, workload groups and native-input acceptance |
| `async/yield-validation/{six-kernel,validation}.json` | Earlier matched runtime comparison and contract acceptance |
| `async/yield-validation/{ready,pending,mixed}-provider.json` | Shared-provider measurements with lifecycle counters |
| `async/tail-await-validation/{strict,loop-reference,validation}.json` | Interpreter/native tail contracts, logical-step witnesses and source snapshots |
| `async/performance-final/{strict,validation}.json` | Earlier full-suite and strict generated-code evidence |
| `async/const-transfer-validation/{validation,pending,ready-io}.json` | Source transfer, typed failure, cancellation closure and controlled event recovery |
| `async/const-scheduler-validation/validation.json` | Static children, FIFO yield, cancellation, shared root budget and strict generated-code evidence |
| `async/{timer,pipe,source}-provider-validation/` | Native providers and generated bridge contracts with original measurements |
| `async/{yield-investigation-v2,performance-investigation,performance-context,performance-driver}/` | Archived copied-input, dispatch, context and native-flag experiments |

Other historical kernel reports remain under `build/benchmarks/async/`, with their
own archived inputs. Host variation and different implementation contracts limit
comparisons between those runs. Validation manifests and logs retain commands,
modes and source snapshots; the static scheduler evidence adds no native
performance claim.
