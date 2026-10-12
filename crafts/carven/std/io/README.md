# Standard IO

`stdin()` is available by default alongside the printing functions. Import
`Stdin`, `Read`, and `IOError` directly from `std::io` when naming their types.

```carven
import std::io using IOError;

async fn main() throw IOError {
    let input = stdin().with_line_limit(4096);
    while {
        match await input.read_line()? {
            .Data(line) => println(line),
            .End => break,
        }
    }
}
```

| Operation | Success | Failure |
| --- | --- | --- |
| `input.read(max_bytes)` | `Read<Vector<u8>>`: a chunk of at most `max_bytes`, or `.End` | `IOError` |
| `input.read_all(limit)` | `Vector<u8>` through EOF, including empty input | `IOError` |
| `input.read_text(limit)` | Owned, validated `String` through EOF | `IOError` |
| `input.read_line()` | `Read<String>`: an owned line, or `.End` | `IOError` |
| `input.with_line_limit(limit)` | A handle carrying a maximum line content byte count | None |

Reads return cold `std::async.Operation` values. Constructing or discarding an
unstarted read does not consume input. Await or start a child to execute it.
Reads use the ordinary timeout, composition, cancellation, and child closure
rules. Input requires runtime execution.

Line reads strip LF and an immediately preceding CR. Empty lines are
`.Data("")`, distinct from EOF. A final line without LF is returned once before
`.End`; a lone CR remains content. Lines count bytes excluding the delimiter.
The default line limit is the representable byte range. `with_line_limit` selects
a maximum line length, and bulk reads require an explicit limit.
A zero-sized chunk returns `.Data([])` without reading;
an empty stream read through EOF returns empty owning data.

The byte operations retain arbitrary bytes, including internal NUL and malformed
UTF-8. Text operations reuse `std::utf.text` validation and carry its `UTF8Error`
detail in `IOError::InvalidUTF8`. A completed text read has consumed its bytes
even if validation fails.

All handles share one process input cursor and buffer. Started requests are
served in FIFO registration order. Prefetched bytes are preserved across modes:
a line read followed by a chunk reads the remaining bytes. Cancellation removes
its request and preserves buffered, incomplete input. A limit failure preserves
input so the caller may retry with a larger limit or recover with raw chunk reads.
Buffering may prefetch one native block beyond the requested limit before
rejecting it.

All reads use one `IOError` enum with typed payloads:

- `.System(code)` carries the platform error code.
- `.InputTooLarge(limit)` carries the rejected maximum byte count.
- `.InvalidUTF8(detail)` carries the UTF validation error, including byte offset.

Catch `IOError(error)` and match these cases to recover. Byte reads cannot fail
with `.InvalidUTF8`; chunk reads cannot fail with `.InputTooLarge`. The handle
owns configuration, and read successes own their data.

`std::io` resolves input APIs to ordinary Carven declarations. The craft composes
byte reads, UTF validation, text ownership, and typed failures. Native support
owns the process cursor and supplies an `EventSource` while requests are pending.
The driver services it alongside timers and host event sources. POSIX uses `poll`;
Windows handles console, file, and pipe input on the driver thread.
