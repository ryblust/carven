# Audio mixer

Mix two ten-sample tracks with a compile-time gain preset. Clip the result to
`[-1, 1]`, then report clipped samples, peak level, and root mean square (RMS).
Ten samples leave a partial final SIMD block.

With `carven` on `PATH` and a C++20 toolchain available, run from this
example's directory:

```sh
carven main.cv mix.cv
carven main.cv mix.cv -- --logs
carven main.cv mix.cv -- --voice=0.75 --music=0.25
```

The default report starts with these levels, followed by the ten mixed samples:

```text
Mixed 10 samples; clipped: 2
Peak: 1.00; RMS: 0.652
```

[`main.cv`](main.cv) supplies the tracks, iterates `Vector<String>` arguments,
and parses optional `--voice=...` and `--music=...` gains directly with
`split_once` and `f32::parse`. Parsing rejects malformed values and
unrepresentable float values before mixing. Unknown flags report
`MixerError::UnknownArgument` with the original argument.
[`mix.cv`](mix.cv) validates track lengths and gains, mixes eight floating lanes
at a time, masks inactive tail lanes, and retains the bounded output in a Vector.
Nonfinite samples or intermediate results are rejected with `MixError`.
The SIMD path uses `.is_finite()`, `.abs()`, and `.clamp()`; peak and RMS use
scalar `.max()` and `.sqrt()`. Samples use f32; energy and RMS use f64.
Only active tail lanes contribute to statistics.

The same mixing function runs in the compile-time tail test and the native
program. Runtime tests also exercise invalid configuration and empty tracks.
The standard SIMD runtime selects its available backend; this example requires
no target-specific instruction flags.

`--logs` emits `LEVEL milliseconds message` records for the analyzer.
Tracks are supplied as in-memory samples; the program prints mixed values.

The entry exposes one `MixerError` enum for unknown arguments, invalid gains,
option parsing, and mixing failures, preserving each relevant typed detail.
`--help` prints usage. Invalid options or gains print usage to stderr and fail
before mixing; successful stdout remains suitable for a pipeline.
