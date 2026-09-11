# UTF-8 standard library

Import a capability module, such as `import std::utf.text using to_string;`.
The [Xmake integration](../../../../docs/toolchain.md#build-integration) supplies
the official package sources and native include path.

| Module | Responsibility |
| --- | --- |
| `std::utf.scalar` | Unicode scalar conversion and `UnicodeScalarError` |
| `std::utf.codec` | UTF-8 encoding and prefix decoding; `UTF8Encoded` and `UTF8Decode` |
| `std::utf.validation` | Whole-buffer and incremental validation; `UTF8ErrorKind`, `UTF8Error`, and `UTF8Validator` |
| `std::utf.text` | Borrowed and owning text construction |

| Operation | Result |
| --- | --- |
| `validate_utf8(bytes: [u8]) throw UTF8Error` | Validate the entire input without allocation |
| `from_utf8(bytes: [u8]) -> str throw UTF8Error` | Validate and borrow the same byte storage |
| `to_string(bytes: [u8]) -> String throw UTF8Error` | Validate and copy into independent storage |
| `decode_utf8(bytes: [u8]) -> UTF8Decode throw UTF8Error` | `.End` for empty input; otherwise `.Scalar(char, usize)` for the first scalar and its width |
| `encode_utf8(character: char) -> UTF8Encoded` | Four-byte array `bytes` and valid `width` |
| `char_from_u32(value: u32) -> char throw UnicodeScalarError` | Reject surrogates and values above U+10FFFF |
| `char_to_u32(value: char) -> u32` | Unicode scalar number |
| `validator() -> UTF8Validator` | Start an incremental validation attempt |
| `push(&state: UTF8Validator, byte: u8) throw UTF8Error` | Accept one byte |
| `feed(&state: UTF8Validator, bytes: [u8]) throw UTF8Error` | Accept one block; its end is not EOF |
| `finish(state: UTF8Validator) throw UTF8Error` | Declare logical EOF |

Arrays create byte views explicitly with `as_slice()`. Text exposes `[u8]`
through `.bytes`. `from_utf8` returns a view into the input: the caller must keep
the backing array or String alive and unchanged while using that view. Its native
representation conversion does not establish a compiler-tracked return borrow.
Carven still checks known input borrows during the call.

Prefer `to_string` when storing or returning text with an independent lifetime.
It validates and copies the bytes. Use `from_utf8` to avoid the copy when the
caller controls the backing lifetime.

```carven
import std::utf.text using to_string;
import std::utf.validation using UTF8Error;

fn example() -> String throw UTF8Error {
    let bytes: [u8; 4] = [0xf0, 0x9f, 0x98, 0x80];
    return to_string(bytes.as_slice())?;
}
```

## Error and streaming contract

`UTF8Error` contains `kind`, `offset`, and `sequence_start`. Positions are
zero-based byte offsets in the logical stream. `offset` identifies the rejected
byte; EOF truncation points at the input end. `sequence_start` identifies the
start of the invalid sequence, so bytes before it form a valid prefix.

Kinds are `InvalidLead`, `InvalidContinuation`, `Overlong`, `Surrogate`,
`TooLarge`, and `Truncated`. Illegal lead bytes C0/C1 and F5..FF report
`InvalidLead`. Empty input, embedded NUL, and Unicode noncharacters are valid.
There is no normalization or replacement decoding. Prefix decoding does not
inspect bytes after the first complete scalar.

A rejected `push` leaves the state unchanged. A rejected `feed` preserves the
progress of accepted bytes before the rejection. Stop that validation attempt on error;
retrying with different bytes describes a different stream. An incomplete block
is accepted, and only `finish` reports truncation. Logical stream length must
fit `usize`.

Initialize `UTF8Validator` through `validator()` and change it through `push`
or `feed`. Its fields record byte positions and the pending sequence; it stores
no input.

## Implementation and tests

The modules in this directory own the public types and UTF algorithms.
Whole-buffer validation, incremental validation, and prefix decoding share the
byte transition in `push`. [runtime/utf.hpp](../../runtime/utf.hpp) provides UTF
primitives and checked representation conversions at the C++ boundary. Runtime
text and String facilities use the same primitives.

`tests/crafts/carven/std/utf/` checks public results, every valid Unicode scalar,
invalid byte classes, and chunk boundaries. Run it with
`./xmakew test -g crafts`; one C++20 binary uses the generated default test entry.
Compiler diagnostic tests check known Carven input borrows at the public API.
