# Audio mixer

Mix two ten-sample tracks with a compile-time gain preset. Clip the result to
`[-1, 1]`, then report clipped samples, peak level, and root mean square (RMS).
Ten samples leave a partial final SIMD block.

```sh
./xmakew run carven-example-audio-mixer
./xmakew run carven-example-audio-mixer --logs
./xmakew run carven-example-audio-mixer --voice=0.75 --music=0.25
```

The default report starts with these levels, followed by the ten mixed samples:

```text
Mixed 10 samples; clipped: 2
Peak: 1.00; RMS: 0.652
```

[`main.cv`](main.cv) supplies the tracks, iterates `Vector<String>` arguments,
and parses optional `--voice=...` and `--music=...` gains directly with
`split_once` and `parse<f32>`. Parsing rejects malformed values and
unrepresentable float values before mixing. Unknown flags report
`MixerError::UnknownArgument` with the original argument.
[`mix.cv`](mix.cv) validates track lengths and gains, mixes eight floating lanes
at a time, masks inactive tail lanes, and retains the bounded output in a Vector.
Nonfinite samples or intermediate results are rejected with `MixError`.
C++ `<cmath>` supplies the square root used for RMS.

The same mixing function runs in the compile-time tail test and the native
program. Runtime tests also exercise invalid configuration and empty tracks.
The standard SIMD runtime selects its available backend; this example requires
no target-specific instruction flags.

`--logs` emits `LEVEL milliseconds message` records for the collector and analyzer.
Tracks are supplied as in-memory samples; the program prints mixed values.

The entry exposes one `MixerError` enum for unknown arguments, invalid gains,
option parsing, and mixing failures, preserving each relevant typed detail.
