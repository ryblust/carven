# Examples

Each directory contains one application, with source files grouped by
responsibility. Start with [`hello.cv`](hello.cv) for a single-file program.

| Program | What it does | Start here |
| --- | --- | --- |
| `log_analyzer` | Parse logs, reject malformed records, redact credentials, and emit a JSON report | [README](log_analyzer/README.md), [main.cv](log_analyzer/main.cv) |
| `audio_mixer` | Mix two tracks, clip samples, and report peak and RMS levels | [README](audio_mixer/README.md), [main.cv](audio_mixer/main.cv) |
| `stream_collector` | C++ calls Carven to merge two memory streams, deduplicate records, and return a structured report | [README](stream_collector/README.md), [workflow.cv](stream_collector/workflow.cv) |

With `carven` on `PATH` and a C++20 toolchain available, run from this
`examples/` directory:

```sh
carven hello.cv
carven log_analyzer/main.cv log_analyzer/analysis.cv
carven audio_mixer/main.cv audio_mixer/mix.cv
```

The programs include their sample input. The collector has a C++ entry point.
Its repository build requires Xmake 3.1.1 and LLVM/Clang with libc++ and C++26
support; LLVM 23 is the validated version. On Windows, use LLVM-MinGW with its
`bin` directory on `PATH`. From the checkout root, one directory above
`examples/`, run:

```sh
./xmakew build
./xmakew build carven-example-stream-collector
./xmakew run carven-example-stream-collector
```

Use `.\xmakew.ps1` in Windows PowerShell.

The mixer and analyzer share a small line format:

```text
LEVEL milliseconds message
```

`LEVEL` is `INFO`, `WARN`, or `ERROR`. For example,
`WARN 37 mixer clipped=2 samples=10`. The analyzer reports messages at or above
30 milliseconds and redacts words beginning with `token=`.
The mixer can emit this format for the analyzer. From the same `examples/`
directory, connect the applications:

```sh
carven audio_mixer/main.cv audio_mixer/mix.cv -- --logs > mixer.log
carven log_analyzer/main.cv log_analyzer/analysis.cv -- --stdin < mixer.log
```
