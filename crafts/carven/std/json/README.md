# JSON standard craft

The JSON craft validates, reads, constructs, modifies, and writes JSON values.

| Module | Public contract |
| --- | --- |
| `std::json.value` | Owning `Value` and `ValueKind` |
| `std::json.validation` | `validate(str)`; `validate_bytes([u8])` |
| `std::json.string` | `decode_string(str) -> String` |
| `std::json.error` | `JSONError` with input byte offsets; `ValueError` for value operations |

`Value::parse(text)` returns an independent tree. `Value::parse_bytes(bytes)`
first checks the entire input's UTF-8 encoding; an encoding error takes
precedence over a JSON syntax error. Any JSON value may be the root.
`value.to_json()` returns an independent, compact JSON string.

```carven
import std::json.value using Value;
import std::json.error using { JSONError, ValueError };

fn update(text: str) -> String throw JSONError + ValueError {
    var config = Value::parse(text)?;
    config.set("enabled", &&Value::boolean(true))?;
    config.set("attempts", &&Value::integer(3))?;
    return config.to_json()?;
}
```

`Value::null`, `boolean`, `string`, `number`, `integer`, `unsigned_integer`,
`real`, `array`, and `object` construct values. `number` accepts exactly one
JSON number spelling; `real` requires a finite value. Arrays support `push`,
`at`, `set_at`, and `remove_at`. Objects support `get`, `has`, `set`, and
`erase`. `len` reports the member or element count. `key_at` and
`member_value_at` return the key and value at an object's member position;
`append_member` appends a member, including a duplicate key.

Objects retain member order and duplicate keys. `get` selects the last matching
member, `set` replaces that member or appends a new one, and `erase` removes
all matching members. Child access returns an independent value. Copies can
be modified independently; modifying a child copy requires assigning it back
to its parent.

`as_str`, `number_text`, and `key_at` return borrowed `str` views. Keep the value
alive and unchanged while using a view; conflicting mutation and escaping borrows
are rejected. Copy with `as String` when the text needs an independent lifetime.
Bind a child returned by `get` or `at` before retaining a view into that child:

```carven
let child = config.get("name")?;
let name = child.as_str()?;
let saved = name as String;
```

Numbers retain their validated spelling as their sole representation. Reading
and writing preserves large magnitudes such as `1E400`, integer precision,
negative zero, and exponent spelling. `as_i64` requires integer spelling and a
representable result; `as_u64`
additionally requires nonnegative spelling. `as_f64` permits rounding but rejects
out-of-range results. `number_text` returns the original spelling.
`as_bool` and `as_str` read the corresponding scalar types. Wrong kinds,
missing keys, invalid indices, and unsuccessful numeric conversions produce
`ValueError`; they have no input byte offset.

Validation and parsing share one bounded syntax machine. String events carry
ordinary byte spans and decoded escape scalars, so parsing builds owned text
during the same scan. SIMD classifies string stops, whitespace, and digit runs;
loads remain within the supplied input. A Carven enum represents each value;
`Sequence` owns array elements and ordered object members. Parsing transfers
constructed children into their containers. Writing borrows enum payloads and
sequence elements while traversing the tree. Numeric conversions use
`std::number.parse`; its native support delegates to standard character conversion.

Whitespace is space, tab, LF, or CR. Escaped surrogate pairs decode to Unicode
scalars; unpaired surrogates are rejected. Input nesting and writing are limited
to 64 containers. Writing escapes quotes, backslashes, and control characters;
other Unicode scalars are written as UTF-8. Writing preserves member order and
number spelling and does not normalize or sort them.

`JSONError.offset` is a zero-based input byte offset. `UnexpectedEnd` points at
the input length; `UnpairedSurrogate` points at the offending backslash.
`decode_string` accepts exactly one quoted string without surrounding whitespace
and returns independently owned text.

Validation, parsing, value construction, text queries, mutation, decoding, and
writing are `const fn` operations. They use ordinary execution budgets and the
same ownership and failure rules as runtime calls. For example:

```carven
const compact = Value::parse(r#"{ "enabled": true }"#)?.to_json()?;
```

The completed `String` freezes to `str`. JSON trees contain `Sequence` storage
and remain execution-local; they cannot be retained as module constants. Return
text or a supported scalar from a constant computation that builds a tree.
`as_i64`, `as_u64`, and `as_f64` require runtime execution because their
`std::number.parse` conversions use native C++ character conversion.
