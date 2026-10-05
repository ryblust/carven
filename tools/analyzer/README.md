# Analyzer

`carven-analyzer` is a resident, serial analysis process built on `tools/workspace`.
The caller supplies document identities, source bytes, versions, and an explicit
closed module selection. The process retains these inputs and their query caches
across requests.

```shell
./xmakew build
./xmakew build carven-analyzer
./xmakew test -g analyzer
```

## Session

`AnalyzerSession` executes typed requests and returns owning data.
`analyzer.protocol` translates requests and responses to bytes; the process entry
owns stdio and session lifetime. Hover renders type text while its semantic owner
is alive. Diagnostics retain codes, severity, messages, labels, notes, and helps.
Static output is captured in check results.

Queries use the current inputs after preceding requests. Their document versions
cover all available selected inputs; navigation locations also carry the destination
version. Missing selected inputs produce compilation-input diagnostics. Equal or
older updates fail without changing inputs. Identical bytes advance the version
while reusing content analysis. Closing retains the document's project mapping;
reopening starts a new version sequence. Project replacement validates every module
path before changing the selection. Compilation input validation reports duplicate
mappings.

Responses remain valid after later updates or session destruction. Acknowledgements
only confirm completion and carry no document versions. The process inherits the
workspace library's whole-project invalidation, observation coverage, and cache
retention; see [Workspace analysis](../workspace/README.md).

## Transport

The process accepts no arguments. Standard input and output carry framed messages;
standard error carries process and transport failures. Clients send one request
and read its response before sending the next. Responses are flushed immediately.
Stop acknowledges and exits, including when stdin remains open. EOF at a frame
boundary exits successfully. Truncated or oversized frames and stream failures
exit with status 2. Malformed payloads within a complete accepted frame receive
an error response without executing a request; processing then continues.

Both directions use a four-byte unsigned little-endian payload length followed by
that many bytes. Payloads are limited to 16 MiB. This bound applies to messages,
not accumulated document storage or compiler execution resources. An oversized
response is replaced by a `response_limit` error after request execution.

## Payload encoding

This is a private binary format. `u8`, `u32`, and `u64` are unsigned little-endian
integers. `i64` uses the same eight bytes as its two's-complement bit pattern.
`text` is a `u32` byte length followed by those bytes, without a terminator.
Document IDs and source text pass through verbatim. `list<T>` is a `u32` count
followed by elements; `optional<T>` is a presence byte (0 or 1) followed by the
value when present. Booleans are one byte. Positions and half-open ranges use
UTF-8 byte offsets.

A request begins with a `u8` operation tag:

| Tag | Operation | Fields |
| --- | --- | --- |
| 1 | Update | document: text, version: i64, source: text |
| 2 | Close | document: text |
| 3 | Replace project | list of (document: text, canonical module path: text) |
| 4 | Check | none |
| 5 | Hover | document: text, offset: u32 |
| 6 | Definition | document: text, offset: u32 |
| 7 | References | document: text, offset: u32 |
| 8 | Stop | none |

A response contains a `u8` result tag, a list of (document: text, version: i64),
then the result fields. Query version lists describe available selected inputs;
acknowledgements and errors have empty lists.

| Tag | Result | Fields |
| --- | --- | --- |
| 0 | Error | code: text, message: text |
| 1 | Acknowledgement | none |
| 2 | Check | published: bool, diagnostics: list, captured output: list |
| 3 | Hover | optional (location, type text: text) |
| 4 | Definition | optional location |
| 5 | References | optional list of locations, including declarations |

A location is (document: text, version: i64, start: u32, end: u32).
A diagnostic is (severity: text, code: text, message: text, primary: optional label,
related: list of labels, notes: list of notes, helps: list of text). A label is
(location, message: text); a note is (optional location, message: text).
Captured output is (stream: text, bytes: text), where stream is `stdout` or `stderr`.
Severity is `error` or `warning`.
