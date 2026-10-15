# Input and output

[Language](README.md) · [Standard capabilities](standard.md)

`std::io` provides printing, asynchronous standard input, and byte framing.
Printing and `stdin` are available by default. Import other declarations when
naming them.

| Capability | API |
| --- | --- |
| Standard output and error | `print`, `println`, `eprint`, `eprintln` |
| Asynchronous standard input | `stdin`, `Stdin`, `Read<T>`, `IOError` |
| Line boundaries in a complete byte buffer | `ByteLines`, `LineBoundary` |
| Incremental bounded byte records | `LineFramer`, `FrameError` |

## Printing

`print`, `println`, `eprint`, and `eprintln` are builtin
callables; they need no import and follow ordinary name lookup and shadowing.
They accept one or more Read values and return `void`. `void` and
character-iteration views are not printable. Arguments are evaluated once, from
left to right. Values are separated by one space; `println` and `eprintln` append
one newline.
`println()` and `eprintln()` also accept no argument to write a newline. Output
goes to stdout or stderr respectively. No typed failure or `?` is required.
As with interpolation, scalar Read values are saved and String Read values alias
their owners. Text views retain their backing throughout argument evaluation
and printing.

Direct printing is admitted in functions executed in the static stage, `const` blocks and
`const test` for the types supported by static execution. Static execution delivers
output synchronously to the compiler host; runtime calls use the runtime streams. Declaring
a function does not itself execute it. Optional precomputation does not produce
static-stage output or remove required runtime printing. Printing follows the
same argument, separator, newline, and text rules in both stages. Output bytes
consume the root's cumulative text-work budget. Completed output remains observable
if later execution fails.

Text is printed verbatim, including embedded NUL bytes. Scalars use their default
C++ format representation; a Carven `char` prints its UTF-8 text. Formatting is
explicit through existing interpolation, for example `println(f"{value:04}")`.
Text arguments are not interpreted as format strings: `println("{value}", 3)`
prints `{value} 3` followed by a newline.

Direct printing displays logical data structure independently of interpolation's
formatting protocol. Structures show their source type name and fields in
declaration order. Enums show `Type::Case` and parenthesized payload values.
Arrays and slices show bracketed elements; integer ranges show their bounds
and `..` or `..=`.

Nonempty structures, sequences, and enum payloads use multiline layout. Each
field or element starts on a new line, indented four spaces per level, and ends
with a comma. Closing delimiters occupy their own line at the enclosing level.
Empty structures and sequences remain `Type {}` and `[]`; cases without payloads,
scalars, and ranges remain on one line without a trailing comma. Layout is
independent of line width and content length.

```text
Order {
    price: Money {
        cents: 1250,
    },
    names: [
        "a",
        "b",
    ],
}
```

Nested text is double-quoted, escaping quotes, backslashes, newline, carriage
return, tab, and NUL. Nested characters use single quotes and escape a single
quote. Top-level text retains the verbatim behavior above.

Native results use C++ type classification. Arithmetic values display their
scalar values; `char32_t` uses character display, while `char8_t`, `char16_t`, and
`wchar_t` display numeric code units. `std::string_view` and the runtime `String`
type use quoted text. `char*` and `const char*` use the text rules above;
null values display `nullptr`, and non-null values require readable
NUL-terminated storage. Other pointers convertible to `const void*` display an
address or `nullptr`. Other external types, function pointers, and callable values
display `<opaque>`.
Class values display their type name, including when nested in another value.

Structural display never invokes a custom formatter, stream insertion operator,
or getter, including for nested fields. `println(value)` selects structural
display; `println(f"{value}")` first performs explicit formatting. Read access and
backing requirements apply to the complete printed value; display adds no owning
copy or transfer.

Structural output expands at most eight levels (the root has depth zero), shows
at most 64 elements per array or slice, and retains at most 16,384 UTF-8 bytes
before a truncation marker, including layout whitespace. Omitted content is
marked `...`; a sequence omission occupies an element line ending in a comma.
These bounds also apply to structural values in assertion explanations, but not to verbatim
top-level text. Display is diagnostic text, not a serialization format.

Buffering and flushing follow the selected C++ standard-library facilities,
with no extra flush per call. Native formatting or output failure terminates
through the runtime's non-throwing contract.

Printing values can use an expected signature, for example
`let output: fn(str, i32) -> void = println;`, and pass through ordinary callable
views. The expected signature fixes the parameter count and types.

## Standard input

`stdin()` is available without an import and follows ordinary name lookup.
Import `Stdin`, `Read`, and `IOError` from `std::io` when naming their types.
`Stdin`, `Read<T>`, and `IOError` are declared by the standard source module.
Construction, copying, matching, ownership, and failure
handling use ordinary language rules.

`Stdin::new()` and `stdin()` construct equivalent input handles.
`Read<T>` is a generic enum with `Data(T)` and nullary `End` cases.
An input handle owns configuration; completed reads own their returned data.

| Operation | Success | Nominal failure |
| --- | --- | --- |
| `input.read(max_bytes: usize)` | `Read<Vector<u8>>`: a chunk of at most `max_bytes`, or `.End` | `IOError` |
| `input.read_all(limit: usize)` | `Vector<u8>` through EOF, including empty input | `IOError` |
| `input.read_text(limit: usize)` | Owned, validated `String` through EOF | `IOError` |
| `input.read_line()` | `Read<String>`: an owned line, or `.End` | `IOError` |
| `input.with_line_limit(limit: usize)` | A new handle with the selected line content byte limit | None |

Reads return cold operations and run under the same driver,
cancellation, child closure, and timeout rules as other operations. Constructing
or discarding an unstarted read does not consume input. Await or start a child
to execute it. The read uses the process input of its selected execution stage.

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

Line reads strip LF and an immediately preceding CR. Empty lines are `.Data("")`,
distinct from EOF. A final line without LF is returned once before `.End`; a lone
CR remains content. Lines count bytes excluding the delimiter. The default line
limit is the representable byte range. `with_line_limit` selects a maximum line
length; `read_all(limit)` and `read_text(limit)` require an explicit maximum byte
count. A zero-sized chunk returns `.Data([])` without reading. Empty input read
through EOF returns empty owning data.

Byte reads preserve arbitrary bytes, including internal NUL and malformed UTF-8.
Text reads validate the completed byte sequence and report the
[shared UTF error detail](utf.md#error-and-streaming-contract).
Completed text reads consume their bytes before validation, including when
validation fails.

All reads use the `IOError` enum from `std::io`:

- `.System(i32)` carries the platform error code.
- `.InputTooLarge(usize)` carries the rejected maximum byte count.
- `.InvalidUTF8(UTF8Error)` carries the encoding error and byte positions.

Catch `IOError(error)` and match these cases to recover. Byte reads cannot fail
with `.InvalidUTF8`; chunk reads cannot fail with `.InputTooLarge`. Printing
retains its `void` contract and non-throwing failure behavior described above.

All handles share one process input cursor and buffer. Started requests are served
in FIFO registration order. Prefetched bytes remain available across read modes.
Cancellation removes its request and preserves buffered incomplete input. A limit
failure preserves input so the caller may retry with a larger limit or recover
with raw chunk reads. Buffering may prefetch one native block beyond the selected
limit before rejecting it.

### Input execution capabilities

Input factories and read operations have `const` execution capability. That
permits explicit static execution using the compiler process's input.
Interpreted and native reads use their executing process's input. Constructing
an input handle configures future reads without consuming input; ordinary calls
remain at runtime.

All input operations use the same successful values, typed failures, cancellation,
and cold-operation contracts in these stages. Prefetched bytes, EOF, and system
errors belong to a process cursor shared across static roots and subsequent
interpretation in that process.

A read's explicit limit produces `IOError::InputTooLarge`. Static and interpreted
value-size or aggregate-budget exhaustion is an execution limit and cannot be
caught as `IOError`. Input waits have no implicit elapsed-time limit.

## Byte framing

Byte framing processes bytes without UTF-8 validation. Decode complete records
with `str::from_utf8` to preserve per-record recovery and multibyte characters
across chunks. The framing APIs accept byte slices from any source.

Create an offset cursor with `ByteLines::new()`, then call `cursor.next(bytes)`
to traverse a complete buffer. `LineBoundary::Line(start, end, terminated)` identifies bytes excluding LF;
`terminated` says whether LF was present. CR remains part of that boundary. `End`
marks exhaustion. Empty input and a final LF do not create phantom records. Keep
the same buffer for a traversal.

`LineFramer::new(limit, strip_cr)` owns an unfinished record. `feed(bytes)?` returns
owning byte vectors for every completed LF record, including empty records. The
limit counts bytes before LF, including a possible CR. If `strip_cr` is true,
remove CR immediately before LF from the delivered record. Arbitrary block splits
are allowed. A bare CR at EOF is content.

`finish(allow_partial)?` seals the attempt and returns `Read::End` or one remaining
`Read::Data` record. Disallowing an unfinished record reports `FrameError::Truncated`.
An oversized record reports `TooLong(limit)` and seals the attempt; already
returned records remain independent, while records from the failing feed call
are not delivered. Further feed or finish calls report `Finished`. A zero limit
accepts empty LF records. All framing methods also support static execution.
