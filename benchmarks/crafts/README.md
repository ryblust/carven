# Crafts performance experiment

Run from the repository after building and validating the compiler:

```sh
python3 benchmarks/crafts/run.py \
  --compiler /absolute/path/to/carven \
  --baseline /private/tmp/carven-json-first-slice-baseline/crafts \
  --output /private/tmp/carven-crafts-performance
```

`--baseline` names a frozen Crafts source root containing `carven/`. Choose the
snapshot for the comparison being measured; the example uses the initial JSON
experiment. Both sources must support the benchmark API with the chosen compiler.
The script stages that source and the current source with copies of the same
compiler. It generates the same exported API adapters, compiles separate C++20
translation units with Clang `-O3 -DNDEBUG -fno-exceptions -fno-rtti` and no LTO,
and links both versions into each measurement executable. Staging gives each
version its own runtime namespace, so differing inline runtime definitions have
distinct C++ identities. Include paths and product source remain unchanged.
Native and forced portable backends use separate executables; every translation unit in an
executable selects the same backend. This machine's native backend is recorded
in the result metadata. AVX2 requires both an x86 host and explicit `--cxx-flag=-mavx2`.

`--case-prefix=count/` measures only count cases. The selected prefix is recorded
in metadata; an unmatched prefix fails before sampling.

SIMD count, find, and prefix also compare with a plain C++ loop and a direct
runtime-vector implementation that reduces each block. Count additionally uses
`bounded_u8`, `bounded_u16`, and `bounded_u32`: ordinary C++ loops with unsigned
byte classification and local sums flushed to `usize` every 255, 32768, or 65536
bytes, respectively. Each batch fits its local sum type. They use no SIMD
intrinsics. All references return the same counts and offsets as the Carven
adapters. UTF and JSON compare the frozen source with
the current source.
JSON text validation receives UTF-8 text; byte validation includes UTF-8 checking.
Decode measures owned string creation and destruction for valid complete literals.
Its output is checked byte for byte before sampling and consumed in constant time
inside the measurement. Validation returns success or the error byte offset.

Inputs and expected results are constructed outside timing. Cases cover short
and long inputs, sparse and dense byte matches, search and prefix positions,
ASCII and multibyte UTF-8, JSON numbers, whitespace, nested containers, plain
strings, and escapes. Dense zero counts include 16319, 16320, 16321, and 65537
bytes to observe accumulation boundaries and tails; 8192-byte zero counts cover
both densities. Invalid inputs measure time to return an error, without
reporting whole-input throughput.

The harness performs two warmups, calibrates each case to at least 10 ms per
batch, then rotates implementation order across seven samples. `--batch-ms` and
`--samples` change those controls. Time includes the complete adapter call and
its result consumption, excludes input construction, correctness checks, and
printing, and uses `steady_clock`. `results.csv` records median, minimum, maximum,
iterations, and checksum; `samples.csv` retains each observation. Reported GiB/s
uses complete valid input length only for complete scans; early search/prefix
results and invalid inputs report zero in that column and are compared in ns/op.
Small inputs are best compared in ns/op.
On macOS the sampling thread requests `QOS_CLASS_USER_INITIATED` through
[`pthread_set_qos_class_self_np`](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/power_efficiency_guidelines_osx/PrioritizeWorkAtTheTaskLevel.html).
This sets a common scheduling preference, without pinning a CPU or eliminating
measurement noise. Other platforms retain their default scheduling policy.
The policy is recorded in metadata and executable output.
These measurements use repeatedly accessed input and describe this sampling
condition rather than cold-cache behavior. Implementations dispatch operation
and set once before their scan loops. Plain and bounded C++ loops and the portable
backend remain subject to ordinary C++ optimization, including automatic
vectorization; inspect the emitted loop before attributing results to it.
A compiler memory barrier at each call keeps
repeated invocations observable without adding a machine instruction.

`metadata.json` records the host, compiler version, flags, runtime namespaces,
original and staged source hashes, generation and build commands, sampling
controls, and backend. `generated_sha256` hashes the staged bytes actually
compiled; `generated_original_sha256` hashes the compiler's unmodified output.
Staged adapter and main hashes identify the compiled harness. Generated artifacts and native
build logs remain under the output directory. Existing output directories are
rejected so independent runs retain their own evidence.
