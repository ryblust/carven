# UTF standard library

The `utf` library provides Unicode and UTF encoding support. It currently
implements Unicode scalar conversion and UTF-8 encoding, decoding, validation,
and text construction.

Import a capability module, such as `import std::utf.text using to_string;`.
The `carven`, `check`, `compile`, and `interpret` commands automatically collect
the official package sources.

| Module | Responsibility |
| --- | --- |
| `std::utf.scalar` | Unicode scalar conversion and `UnicodeScalarError` |
| `std::utf.codec` | UTF-8 encoding and prefix decoding; `UTF8Encoded` and `UTF8DecodeResult` |
| `std::utf.error` | Shared `UTF8ErrorKind` and `UTF8Error` contracts |
| `std::utf.validation` | Whole-buffer and incremental validation; `UTF8Validator` |
| `std::utf.text` | Borrowed and owning text construction |

| Operation | Result |
| --- | --- |
| `validate_utf8(bytes: [u8]) throw UTF8Error` | Validate the entire input without allocation |
| `from_utf8(bytes: [u8]) -> str throw UTF8Error` | Validate and borrow the same byte storage |
| `to_string(bytes: [u8]) -> String throw UTF8Error` | Validate and copy into independent storage |
| `decode_utf8_prefix(bytes: [u8]) -> UTF8DecodeResult throw UTF8Error` | `.End` for empty input; otherwise `.Scalar(char, usize)` for the first scalar and its width |
| `encode_utf8(character: char) -> UTF8Encoded` | Four-byte array `bytes` and valid `width` |
| `encoded_bytes(encoded: UTF8Encoded) -> [u8]` | Borrow exactly the valid encoding prefix |
| `char_from_u32(value: u32) -> char throw UnicodeScalarError` | Reject surrogates and values above U+10FFFF |
| `character as u32` | Unicode scalar number |
| `UTF8Validator::create() -> UTF8Validator` | Start an incremental validation attempt |
| `state.push(byte: u8) throw UTF8Error` | Accept one byte |
| `state.feed(bytes: [u8]) throw UTF8Error` | Accept one block; its end is not EOF |
| `state.check_complete() throw UTF8Error` | Check that no partial scalar remains; does not end the stream |

Array arguments create byte views implicitly; `as_slice()` is also available. Text exposes `[u8]`
through `.bytes`. `from_utf8` returns a view into the input: the caller must keep
the backing array or String alive and unchanged while using that view. Carven
tracks the returned view and rejects conflicting mutation and escaping borrows.

Prefer `to_string` when storing or returning text with an independent lifetime.
It validates and copies the bytes. Use `from_utf8` to avoid the copy when the
caller controls the backing lifetime.

```carven
import std::utf.text using to_string;
import std::utf.error using UTF8Error;

fn example() -> String throw UTF8Error {
    let bytes: [u8; 4] = [0xf0, 0x9f, 0x98, 0x80];
    return to_string(bytes)?;
}
```

## Compile-time use

`char_from_u32`, `encode_utf8`, `encoded_bytes`, `decode_utf8_prefix`, and `validate_utf8` are
`const fn`: the same implementation runs in static roots and
ordinary runtime calls.
Encoded arrays can also become frozen constant slices:

```carven
import std::utf.codec using { encode_utf8, encoded_bytes };
import std::utf.validation using validate_utf8;

const encoded = encode_utf8('😀');
const bytes = encoded_bytes(encoded);
const { validate_utf8(bytes)?; }
```

`UTF8Encoded.bytes` always contains four bytes; only the first `width` bytes
belong to the encoding, and the remaining bytes are zero.
`encoded_bytes(encoded)` borrows that valid prefix. Bind the encoding before
retaining its view; Carven tracks the backing storage's lifetime and mutation.

`UTF8Validator` operations are also `const fn`. Incremental state can be
created and updated inside `const fn` bodies, `const` blocks, and static tests.
See the [class execution rules](../../../../docs/language/aggregates.md#ordinary-value-classes)
for constant-expression entry boundaries.
Static execution is subject to the language's ordinary resource budgets.

`from_utf8` and `to_string` currently require runtime execution: the executor
does not admit unchecked borrowed text construction.

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
is accepted, and only `check_complete` reports truncation. Logical stream length
must fit `usize`.

Initialize `UTF8Validator` through `UTF8Validator::create()` and change it through
`push` or `feed`. Its private fields record byte positions and the pending
sequence; it stores no input. `processed_bytes()` returns the accepted byte count,
and `is_complete()` reports whether no partial scalar is pending. Both queries
and `check_complete()` use Read access; checking completeness does not consume
or close the validator. Call it at logical EOF to detect truncation; subsequent
input is still permitted.

## Implementation boundary

The craft owns scalar conversion, encoding, decoding, validation, and their
typed errors. Its internal `scan` module supplies constant-capable byte
transitions shared by `codec` and `validation`; `text` composes validation
with text construction.

The C++ runtime checks UTF-8 at native text ingress and traps on invalid text.
That check has no streaming state or craft error and does not depend on the
craft. Both layers use the selected runtime SIMD backend for native block
operations.

`UTF8Validator` stores positions and pending sequence state, without retaining
input. Copies are independent validation attempts. Byte transitions commit only
after validation; block errors retain progress up to the rejected byte. Pending
state from an earlier feed is resolved without rereading that feed's input.

Unchecked character and borrowed-text constructors require validated input.
Constant and interpreted character construction check the scalar precondition;
native unchecked construction relies on it. Borrowed text retains its ordinary
backing lifetime checks.

The contracts in `tests/crafts/carven/std/utf/` cover scalar boundaries, invalid
byte classes, error positions, streaming transitions, and text storage. Static
and native execution, including portable SIMD, exercise the applicable APIs.
