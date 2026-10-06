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
`erase`. `len` reports the member or element count. `key_at` and `member_at`
traverse object members in order; `append_member` appends a member, including
a duplicate key.

Objects retain member order and duplicate keys. `get` selects the last matching
member, `set` replaces that member or appends a new one, and `erase` removes
all matching members. Child access returns an independent value. Copies can
be modified independently; modifying a child copy requires assigning it back
to its parent.

Numbers retain their validated spelling as their sole representation. Reading
and writing preserves large magnitudes such as `1E400`, integer precision,
negative zero, and exponent spelling. `as_i64` requires integer spelling and a
representable result; `as_u64`
additionally requires nonnegative spelling. `as_f64` permits rounding but rejects
out-of-range results. `number_text` returns the original spelling.
`as_bool` and `as_string` read the corresponding scalar types. Wrong kinds,
missing keys, invalid indices, and unsuccessful numeric conversions produce
`ValueError`; they have no input byte offset.

Validation and parsing share one bounded syntax machine. String events carry
ordinary byte spans and decoded escape scalars, so parsing builds owned text
during the same scan. SIMD classifies string stops, whitespace, and digit runs;
loads remain within the supplied input. The owning representation uses C++
standard value containers; grammar and writing remain Carven operations.

Whitespace is space, tab, LF, or CR. Escaped surrogate pairs decode to Unicode
scalars; unpaired surrogates are rejected. Input nesting and writing are limited
to 64 containers. Writing escapes quotes, backslashes, and control characters;
other Unicode scalars are written as UTF-8. Writing preserves member order and
number spelling and does not normalize or sort them.

`JSONError.offset` is a zero-based input byte offset. `UnexpectedEnd` points at
the input length; `UnpairedSurrogate` points at the offending backslash.
Validation is available in `const fn` under ordinary execution budgets.
`decode_string` accepts exactly one quoted string without surrounding whitespace
and returns independently owned text. Parsing, value storage, decoding, and
writing require runtime execution.

The contracts are in `tests/crafts/carven/std/json/`; the executable example is
in `examples/json/`.
