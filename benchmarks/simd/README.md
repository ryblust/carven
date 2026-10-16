# SIMD scans

Run `./xmakew bench simd`, or select one of `count_digits`, `count_scattered`,
`find_digits`, `find_scattered`, `prefix_digits`, `prefix_scattered`, and
`prefix_field` with `--case`. `--list` displays cases without building.

Each workload compares the standard Carven scan with an ordinary C++ byte loop.
Both use the same C++20 compiler, optimization settings, input bytes, and output
contract. Native optimization may vectorize the byte loop. This comparison
measures the complete generated scan, including the selected algorithm and native
optimization.

The digit set is the inclusive range 48..57. The scattered set is
`{0, 9, 34, 47, 65, 97, 127, 128, 200, 255}`, exercising the library's nibble lookup
plan. Count inputs use a deterministic byte distribution. Search inputs place
the first matching byte at the beginning or end, or contain no match. Prefix
inputs place the first rejected byte at those positions, or contain no rejection.
Searches begin at position one and return absolute positions; missing searches
return the input length in both implementations.

`prefix_field` scans the prefix of an unquoted CSV field. Its byte class includes
all bytes except LF, CR, double quote, and comma, expressed as five inclusive
ranges. Inputs repeat an email-shaped field value, with a comma at the early or
late position, or no delimiter. This measures structural-character scanning;
it does not parse complete CSV records or validate Unicode.

Lengths are 31, 32, 33, 16387, and 65536 bytes. Backing starts one byte before the
input view, exercising unaligned loads. The short inputs cover vector-width
boundaries. The 16387-byte input exercises bounded count accumulation and its
tail. Inputs are reused during each sample, so these are resident-buffer scans.

Every process constructs its input and computes the expected result through an
independent byte membership table. It verifies the selected kernel before timing.
The measured interval contains repeated kernel calls and checksum accumulation;
it excludes allocation, validation, and printing. Kernels occupy separate
translation units and link-time optimization is disabled, preventing the harness
from replacing repeated calls with a single known result. The reported checksum
is checked again after timing and across implementations.

`--iterations` supplies a minimum byte-work budget in units of 32 bytes,
defaulting to 200000. Each scenario and implementation starts with
`max(128, floor(budget * 32 / length))` scans, then increases that count until its
calibration interval reaches 20 ms. The resulting count stays fixed for its
warmups and samples. Independent calibration gives fast and early-terminating
kernels enough measured work; implementations may use different iteration counts.
Raw calibration observations and actual iteration counts remain in JSON. Samples
below the calibration target are flagged rather than discarded.

Calibration is capped at one billion calls and further limited so accumulated
checksums remain exact in Lua's numeric representation. A zero-duration calibration
observation increases the work count; timed samples require a positive interval.
A failed call, invalid interval, or inability to reach the target within that
limit fails the run.
Reported `ns/scan` includes the common call and checksum overhead, which matters
for early termination and short inputs. Search results are reported per scan
rather than per input byte because early termination does not read the complete
input.

The shared runner performs warmups followed by shuffled, interleaved samples.
Defaults are one warmup and seven measured samples. `--samples`, `--warmups`,
and `--output` control the run. `--portable` forces the portable runtime
backend; portable implementations may still use compiler vector operations.
The default selects the backend supported by the native target's enabled ISA;
this harness does not enable AVX2 automatically.

JSON records individual intervals, checksums, execution order, native target
architecture, compiler identity and flags, and source hashes. Carven results also
record `runtime_backend`; the byte loop has no runtime backend selection.
Workload snapshots remain beside the report. Generated C++ and executables
remain in the main project's build directories. The compiler and Crafts inputs
are hashed and checked for changes across the run.

`run.lua` owns sampling and reports; `xmake.lua` defines native targets.
Workloads use the main project's resolved dependency and shared standard-library
build. `--case` selects one workload group for building and measurement.
`main.cpp` prepares inputs and measures calls through the scan contract.
`scan.hpp` declares that contract for `byte_loop.cpp`; the Carven implementation
comes from the generated workload API.
