# Examples

Each directory contains one application, with source files grouped by
responsibility. Start with [`hello.cv`](hello.cv) for a single-file program.

| Program | What it does | Start here |
| --- | --- | --- |
| `log_analyzer` | Parse logs, reject malformed records, redact credentials, and emit a JSON report | [README](log_analyzer/README.md), [main.cv](log_analyzer/main.cv) |
| `audio_mixer` | Mix two tracks, clip samples, and report peak and RMS levels | [README](audio_mixer/README.md), [main.cv](audio_mixer/main.cv) |
| `stream_collector` | Collect framed records from concurrent TCP streams with a deadline and partial failure recovery | [README](stream_collector/README.md), [workflow.cv](stream_collector/workflow.cv) |

Build and run from the repository root:

```sh
./xmakew build
./xmakew build examples
./xmakew run carven-example-log-analyzer
./xmakew run carven-example-audio-mixer
./xmakew run carven-example-stream-collector
./xmakew test -g examples
```

The programs include their sample input. The collector opens nonblocking TCP
sockets on loopback with OS-assigned ports. Its C++ host supplies socket readiness;
Carven owns concurrent collection,
record framing, typed recovery, and timeout-driven cancellation.

The programs also share a small line format:

```text
LEVEL milliseconds message
```

`LEVEL` is `INFO`, `WARN`, or `ERROR`. For example,
`WARN 37 mixer clipped=2 samples=10`. The analyzer reports messages at or above
30 milliseconds and redacts words beginning with `token=`.
The mixer can emit this format, and the collector can publish standard input
over its first TCP connection and emit the collected records. After building,
connect the applications:

```sh
./xmakew run -q carven-example-audio-mixer --logs > mixer.log
./xmakew run -q carven-example-stream-collector --stdin --records < mixer.log > collected.log
./xmakew run -q carven-example-log-analyzer --stdin < collected.log
```

The collector's other feeds supply a truncated stream and a stalled stream.
Rejection and timeout are handled independently for each feed. Output records
keep feed order and record order within each feed.

The example test group checks documented output, application behavior, and the
connected pipeline.
