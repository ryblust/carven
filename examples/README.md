# Examples

Examples group source files by responsibility. Start with
[`hello.cv`](hello.cv) for a single-file program.

| Program | What it does | Start here |
| --- | --- | --- |
| `log_analyzer` | Parse logs, reject malformed records, redact credentials, and emit a JSON report | [README](log_analyzer/README.md), [main.cv](log_analyzer/main.cv) |
| `audio_mixer` | Mix two tracks, clip samples, and report peak and RMS levels | [README](audio_mixer/README.md), [main.cv](audio_mixer/main.cv) |
| `runtime` | Hot-reload a document preview while its C++ host keeps state and completes asynchronous requests | [README](runtime/README.md), [main.cpp](runtime/main.cpp) |
| `build` | Declare shared targets, or generate sources and assemble a plan with ordinary code | [declarative](build/declarative/build.cv), [imperative](build/imperative/build.cv), [README](build/README.md) |
| `stream_collector` | C++ calls Carven to merge two memory streams, deduplicate records, and return a structured report | [README](stream_collector/README.md), [workflow.cv](stream_collector/workflow.cv) |

With `carven` on `PATH`, interpret the single-file program from the checkout root:

```sh
carven interpret examples/hello.cv
```

The mixer, analyzer, and stream collector use repository Xmake targets and the
repository's compiler build environment. From the checkout root, run:

```sh
./xmakew build
./xmakew build carven-example-audio-mixer
./xmakew build carven-example-log-analyzer
./xmakew build carven-example-stream-collector
./xmakew run carven-example-audio-mixer
./xmakew run carven-example-log-analyzer
./xmakew run carven-example-stream-collector
```

Use `.\xmakew.ps1` in Windows PowerShell. The programs include their sample input.

The mixer and analyzer share a small line format:

```text
LEVEL milliseconds message
```

`LEVEL` is `INFO`, `WARN`, or `ERROR`. For example,
`WARN 37 mixer clipped=2 samples=10`. The analyzer reports messages at or above
30 milliseconds and redacts words beginning with `token=`.
The mixer can emit this format for the analyzer. From the checkout root,
connect the applications:

```sh
./xmakew run carven-example-audio-mixer --logs > mixer.log
./xmakew run carven-example-log-analyzer --stdin < mixer.log
```

The standalone projects in `build/declarative/` and `build/imperative/` each
have a `build.cv`. Run `carven build` from either directory.
