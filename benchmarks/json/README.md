# JSON validation

```sh
./xmakew bench json
./xmakew bench json --list
./xmakew bench json --case=events_32 --portable
```

This benchmark calls `validate_bytes` from `std::json` through an exported
Carven function. It measures the public byte-ingress API: UTF-8 validation,
JSON syntax, and failure delivery. It does not construct a DOM or convert numbers
to native numeric values.

The input families model API payloads rather than recorded production traffic:

| Family | Contents |
| --- | --- |
| `events` | Timestamped request events with IDs, paths, latency numbers, variable-length messages, Unicode, and escaped quotes, newlines and backslashes |
| `catalog` | Product records with Unicode names, prices, booleans, null, tag arrays and nested dimensions |

Each family has 1, 32 and 256 records. IDs and field lengths come from a fixed
integer generator. Object field order is fixed; Xmake's JSON encoder writes
string values and its decoder independently accepts the assembled document.
Documents are generated at run setup, loaded before timing and reused in memory.
The report records the exact byte length, non-ASCII byte count, content hash and
saved file path for every input. These are resident-buffer validation workloads.

Each document supplies four outcomes:

- `valid`: the entire document must be accepted.
- `trailing`: one appended `#` must produce `JSONError` at the original length.
- `truncated`: the missing final delimiter must produce `JSONError` at EOF.
- `invalid_utf8`: replacing the final delimiter with `0xff` must produce
  `UTF8Error` at that byte. This path stops after UTF validation and does not run
  JSON syntax validation; its input throughput describes that rejection path.

The wrapper returns `4 * length` on success, `4 * error_offset + 1` for JSON
failure, or `4 * error_offset + 2` for UTF failure. Every process checks one call
before timing, then verifies the accumulated result after timing. The harness
also checks the reported iterations, input length, result and checksum.

Each outcome starts calibration with 128 calls, outside sampling, and increases
the count until its measured loop reaches 20 ms. That iteration count is retained
for its warmups and samples; calibration
observations and actual sample durations remain in JSON. Defaults are one warmup
and seven shuffled samples. `ns/document` includes the exported function call,
validation, failure/result handling and checksum accumulation. Input generation,
allocation, file reads, initial correctness checks and printing are outside the
interval. The validator's own work remains inside it. `Input MB/s` uses the full
input length; all errors are placed at the end. Fluctuating samples below the
calibration target are flagged rather than discarded.
Calibration is capped at one billion calls and further limited to keep accumulated
checksums exact in Lua's numeric representation. A zero-duration calibration
observation increases the work count; timed samples require a positive interval.
Failed calls, invalid intervals, or failure to reach the target within that limit
stop the run.

Generated C++ and the harness occupy separate translation units with LTO disabled.
There are no volatile accesses or optimization barriers in the measured loop.
Native targets use C++20 and optimized builds. The main project selects the native
compiler and supplies the built Carven executable and resolved rules.
`--portable` selects the portable SIMD backend used by the standard library.
The report retains actual native flags and architecture.
