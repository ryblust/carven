# UTF-8 and text processing

This page defines UTF-8 encoding, decoding, validation, and text processing in
`std::utf`.

## Encoding, decoding, and validation

Import types and functions from `std::utf`.

Use `char::from_u32(value)` to construct a Unicode scalar and
`str::from_utf8(bytes)` to construct a borrowed text view. These associated
entries select ordinary source functions from `std::utf`. Import `char_from_u32`
or `from_utf8` from that module to call the same declaration directly.
`char_from_u32` rejects surrogates and values above U+10FFFF with
`UnicodeScalarError { value }`. `from_utf8` validates the complete byte slice,
throws `UTF8Error` on invalid input, and otherwise borrows the unchanged storage.
Empty input is valid; keep the backing alive while using the view.

| Operation | Result |
| --- | --- |
| `validate_utf8(bytes: [u8]) throw UTF8Error` | Validate the entire input without allocation |
| `to_string(bytes: [u8]) -> String throw UTF8Error` | Validate and copy into independent storage |
| `decode_utf8_prefix(bytes: [u8]) -> UTF8DecodeResult throw UTF8Error` | `.End` for empty input; otherwise `.Scalar(char, usize)` for the first scalar and its width |
| `encode_utf8(character: char) -> UTF8Encoded` | Four-byte array `bytes` and valid `width` |
| `encoded_bytes(encoded: UTF8Encoded) -> [u8]` | Borrow exactly the valid encoding prefix |
| `UTF8Validator::new() -> UTF8Validator` | Start an incremental validation attempt |
| `state.push(byte: u8) throw UTF8Error` | Accept one byte |
| `state.feed(bytes: [u8]) throw UTF8Error` | Accept one block; its end is not EOF |
| `state.check_complete() throw UTF8Error` | Check that no partial scalar remains; does not end the stream |

Array arguments create byte views implicitly; text exposes them through `.bytes`.
Use `str::from_utf8` when the caller controls the backing lifetime and a copy is
unnecessary.

```carven
import std::utf using { to_string, UTF8Error };

fn example() -> String throw UTF8Error {
    let bytes: [u8; 4] = [0xf0, 0x9f, 0x98, 0x80];
    return to_string(bytes)?;
}
```

## Text processing

`std::utf` reuses `str`, `String`, slices, and ordinary typed failures.
All operations below are `const fn` and also run at runtime.

| Operation | Result and ownership |
| --- | --- |
| `substring(text, start, end) -> str throw TextError` | Borrow `[start, end)` UTF-8 bytes; both offsets must be scalar boundaries |
| `starts_with(text, prefix) -> bool` | Compare the prefix without allocation |
| `split_once(text, separator) -> Split throw TextError` | Borrow `before` and `after`; `found` distinguishes a separator from an absent one |
| `split(text, separator) -> Vector<String> throw TextError` | Independent owning fields, including leading, adjacent, and trailing empty fields |
| `lines(text) -> Vector<String>` | Independent lines delimited by LF or CRLF, including empty lines; no extra line after a final delimiter |

Substring offsets count bytes, not Unicode scalars or grapheme clusters. A
reversed or out-of-range selection reports `TextError::OutOfBounds`; an offset
inside a scalar reports `NotCharBoundary`. Splitting uses an exact, nonempty
UTF-8 separator and reports `EmptySeparator` for an empty separator. It performs
no whitespace normalization. An absent separator returns `found: false`, the
entire input as `before`, and an empty `after`.

`Split` and substring results borrow the original input, including results
returned through helper functions. Their lifetimes and mutations follow ordinary
ownership checks. `split` and `lines` return owners and copy the fields, so callers
can retain the collection independently of the input. Empty text yields one empty
split field and zero lines. A lone CR is content; only CR immediately before LF is
stripped.

```carven
import std::utf using { split_once, TextError };

fn setting(text: str) -> String throw TextError {
    let pair = split_once(text, "=")?;
    return if pair.found { pair.after as String } else { "" };
}
```

## Static execution

The source functions and methods on this page are `const fn` declarations. They
share values, failures, and ownership rules across execution stages. Static calls
use the language's execution budgets and result-retention checks.
Encoded arrays can become frozen constant slices:

```carven
import std::utf using { encode_utf8, encoded_bytes, validate_utf8 };

const encoded = encode_utf8('😀');
const bytes = encoded_bytes(encoded);
const { validate_utf8(bytes)?; }
```

`UTF8Encoded.bytes` contains four bytes; only the first `width` bytes belong to
the encoding, and the remainder is zero. `encoded_bytes(encoded)` borrows that
prefix. Bind the encoding before retaining its view; the usual backing lifetime
and mutation checks apply.

## Error and streaming contract

`UTF8Error` contains `kind`, `offset`, and `sequence_start`. Positions are
zero-based byte offsets in the logical stream. `offset` identifies the rejected
byte; EOF truncation points at the input end. `sequence_start` identifies the
start of the invalid sequence, so bytes before it form a valid prefix.
`UTF8Error` and `UTF8ErrorKind` are ordinary source declarations from `std::utf`.
Text construction, library algorithms, and standard input use the same error
values.

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

Copies of a validator are independent validation attempts.
Initialize `UTF8Validator` through `UTF8Validator::new()` and change it through
`push` or `feed`. It stores no input. `processed_bytes()` returns the accepted byte count,
and `is_complete()` reports whether no partial scalar is pending. Both queries
and `check_complete()` use Read access; checking completeness does not consume
or close the validator. Call it at logical EOF to detect truncation; subsequent
input is still permitted.

## Borrowed splitting cursor

`SplitCursor::new()` creates an offset-only cursor. Call
`cursor.next(text, separator)?` explicitly; `SplitField::Value(str)` borrows the
original input, while `SplitField::End` marks exhaustion. Empty input yields one
empty field, and leading, adjacent, and trailing empty fields are preserved.
Separators may contain multiple UTF-8 bytes; an empty separator reports
`TextError::EmptySeparator`, including after exhaustion.

Keep the same text and separator for a traversal. The cursor owns no text and
cannot identify a replacement input. Returned fields require the original backing
to remain alive and valid. Copies of a cursor traverse independently from the
same offset. The cursor and its methods support static execution.
