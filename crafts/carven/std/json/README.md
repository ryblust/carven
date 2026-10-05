# JSON standard craft

The JSON craft validates complete documents and decodes quoted strings.

| Module | Public contract |
| --- | --- |
| `std::json.validation` | `validate(text: str) throw JSONError`; `validate_bytes(bytes: [u8]) throw JSONError + UTF8Error` |
| `std::json.string` | `decode_string(text: str) -> String throw JSONError` |
| `std::json.error` | `JSONErrorKind`; `JSONError { kind, offset }` |

Both validators check every value, string, escape, number, container delimiter,
and trailing byte. Any JSON value may be the root. Whitespace is space, tab,
LF, or CR. Duplicate object keys are accepted. Numbers are checked syntactically
without conversion, including magnitudes such as `1E400`. Nesting is limited to
64 containers.

`str` establishes valid UTF-8. `validate_bytes` first validates the entire input's
encoding and preserves `UTF8Error`; an encoding error takes precedence over a
JSON syntax error. Escaped UTF-16 surrogate pairs become Unicode scalars.
Unpaired surrogates are rejected so decoded text satisfies Carven's scalar
contract. This is a stricter requirement than JSON's escape grammar.

`JSONError.offset` is a zero-based input byte offset. Truncation points at the
input length; an unpaired surrogate points at its escape's backslash. The
validators are `const fn`, subject to ordinary execution budgets.

`decode_string` requires exactly one quoted string, without surrounding
whitespace or trailing input. It validates and decodes in one pass, returning an
independent `String`. Empty strings, escaped NUL, and all Unicode scalars are
supported. Decoding requires runtime execution.

```carven
import std::json.validation using validate;
import std::json.string using decode_string;
import std::json.error using JSONError;

fn example() -> String throw JSONError {
    validate(r#"{"message": "hello"}"#)?;
    return decode_string(r#""hello\u0020world""#)?;
}
```

Validation uses a bounded stack of grammar expectations, without recursive calls
or input-dependent allocation. String validation and decoding share one scanner
that advances across ordinary byte runs, escapes, and the closing quote. SIMD
classifies string stops, whitespace runs, and digit runs through the selected
backend; loads remain inside the supplied input.

Tests in `tests/crafts/carven/std/json/` establish accepted and rejected syntax,
error positions, Unicode decoding, depth limits, and independent output storage.
Run them with `./xmakew test -g crafts`. The executable example is in
`examples/json/`.
