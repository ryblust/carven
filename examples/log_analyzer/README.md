# Log analyzer

Analyze `LEVEL milliseconds message` records and print a JSON report. The bundled
input includes a malformed latency, a credential, and slow warning/error records.
One bad record increments `rejected` without discarding the rest of the input.

```sh
./xmakew run carven-example-log-analyzer
./xmakew run -q carven-example-log-analyzer --stdin < application.log
```

The default output is:

```json
{"accepted":3,"rejected":1,"warnings":1,"errors":1,"slow":["mixer token=[redacted] clipped=2","upstream timed out"]}
```

[`main.cv`](main.cv) selects bundled input or standard input.
[`analysis.cv`](analysis.cv) validates UTF-8 and numbers, matches severity levels,
redacts `token=` words, accumulates owning messages, and edits a JSON report.
Line endings may be LF or CRLF; a final newline is optional. Empty lines are
ignored. Latencies must be unsigned integers no greater than 60000 milliseconds.

The rules and parsed JSON template are compile-time constants. Each report starts
with an owning, writable copy of the template.
Static tests check redaction and the template, while runtime tests check record
recovery and ordering. The async entry reads raw bytes with
`await stdin().read_all(16 * 1024 * 1024)?`; validation and recovery apply to each
record. Command-line arguments are an ordinary `Vector<String>`.
Field parsing uses borrowed `split_once` results and `u64::parse`; redaction uses
`SplitCursor` and `starts_with` from the text library, preserving leading,
trailing, and repeated spaces. `ByteLines` separates records before UTF-8
validation, so invalid text rejects only its own record.

Record parsing reports one `RecordError` enum, retaining typed numeric, UTF-8,
and text-processing details. The entry models application failures with
`AnalyzerError`: input failures retain the standard `IOError` payload, and report
failures retain `ValueError`. Individual malformed records are counted and
processing continues.
`--help` prints usage. Unknown options print usage to stderr and fail before
reading input.
