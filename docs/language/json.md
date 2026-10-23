# JSON

This page defines JSON values, parsing, construction, modification, validation,
and writing in `std::json`.

## Parsing and writing

Import value types, errors, and functions from `std::json`.

| Operation | Result | Failure |
| --- | --- | --- |
| `JSONValue::parse(text: str)` | Independent value tree | `JSONError` |
| `JSONValue::parse_bytes(bytes: [u8])` | Validate UTF-8, then return an independent tree | `UTF8Error`, `JSONError` |
| `value.to_json()` | Independent, compact JSON `String` | `JSONValueError` |

Any JSON value may be the root. Byte parsing checks the entire input's UTF-8
encoding first, so an encoding error takes precedence over a JSON syntax error.

```carven
import std::json using { JSONValue, JSONError, JSONValueError }

fn update(text: str) -> String throw JSONError + JSONValueError {
    var config = JSONValue::parse(text)?;
    config.set("enabled", &&JSONValue::boolean(true))?;
    config.set("attempts", &&JSONValue::integer(3))?;
    return config.to_json()?;
}
```

## Values and collections

All factories return `JSONValue`:

| Factory | Input and failure |
| --- | --- |
| `null()`, `array()`, `object()` | Empty value of the selected kind |
| `boolean(value: bool)`, `string(text: str)` | Store a boolean or an independent text copy |
| `number(text: str)` | Exactly one JSON number spelling; throws `JSONError` |
| `integer(value: i64)`, `unsigned_integer(value: u64)` | Exact integer value |
| `real(value: f64)` | Finite floating value; throws `JSONValueError` otherwise |

`value.kind()` returns `JSONValueKind`: `Null`, `Boolean`, `Number`, `String`, `Array`,
or `Object`. `as_bool()` and `as_str()` read the corresponding scalar kinds.
Scalar queries and collection operations throw `JSONValueError` for an incompatible
kind; collection operations also check keys and indices. Mutating methods require a Write receiver;
`&&value` transfers a child into its container.

| Operation | Result |
| --- | --- |
| `length()` | Array element or object member count |
| `elements()` | Borrowed array elements as `[JSONValue]` |
| `members()` | Borrowed object members as `[(String, JSONValue)]` |
| `at(index)` / `get(key)` | Independent child value |
| `push(&&value)` / `set_at(index, &&value)` / `remove_at(index)` | Append, replace, or remove an array element |
| `has(key)` | Whether an object contains the key |
| `set(key, &&value)` / `append_member(key, &&value)` | Replace or append an object member |
| `erase(key)` | Remove every matching member and return the removed count |

Objects retain member order and duplicate keys. `get` selects the last matching
member, `set` replaces that member or appends a new one, and `erase` removes
all matching members. `get` and `at` return independent values.
Copies can be modified independently; modifying a child copy requires assigning
it back to its parent.

`elements` and `members` borrow their collections without copying child trees.
Iteration such as `for (key, value) in object.members()?` preserves member order
and reads the original children. Assigning an element to `let` or `var` creates
an independent copy.

`as_str` and `number_text` return borrowed `str` views.
Keys are borrowed through `members()?[index].0.as_str()`. Keep the value
alive and unchanged while using text or collection views; conflicting mutation
and escaping borrows are rejected. Copy with `as String` when the text needs an
independent lifetime.
Bind a child returned by `get` or `at` before retaining a view into that child:

```carven
let child = config.get("name")?;
let name = child.as_str()?;
let saved = name as String;
```

## Numbers

Numbers retain their validated spelling. Reading
and writing preserves large magnitudes such as `1E400`, integer precision,
negative zero, and exponent spelling. `as_i64` requires integer spelling and a
representable result; `as_u64`
additionally requires nonnegative spelling. `as_f64` permits rounding but rejects
out-of-range results. `number_text` returns the original spelling.
Numeric conversion failures report `JSONValueError::NumberConversion` without an
input byte offset. Conversions use the entire number spelling.

## Validation and failures

| Function | Result and failures |
| --- | --- |
| `validate(text: str)` | Check one complete JSON value; throws `JSONError` |
| `validate_bytes(bytes: [u8])` | Check UTF-8, then JSON; throws `UTF8Error` or `JSONError` |
| `decode_string(text: str)` | Decode exactly one quoted string without surrounding whitespace into an owning `String`; throws `JSONError` |

`UTF8Error` records the error kind, rejected byte offset, and invalid sequence's
starting offset. Offsets are zero-based bytes in the input; EOF truncation points
at the input end.

Whitespace is space, tab, LF, or CR. Escaped surrogate pairs decode to Unicode
scalars; unpaired surrogates are rejected. Input nesting and writing are limited
to 64 containers. Writing escapes quotes, backslashes, and control characters;
other Unicode scalars are written as UTF-8. Writing preserves member order and
number spelling and does not normalize or sort them.

`JSONError.offset` is a zero-based input byte offset. `UnexpectedEnd` points at
the input length; `UnpairedSurrogate` points at the offending backslash.
`JSONError.kind` is one of `UnexpectedEnd`, `UnexpectedByte`, `InvalidEscape`,
`InvalidHex`, `UnpairedSurrogate`, `ControlCharacter`, `InvalidNumber`,
`TrailingInput`, or `DepthLimit`. Value operations use `JSONValueError::WrongKind`,
`MissingKey`, `IndexOutOfBounds`, `NumberConversion`, or `DepthLimit`.

## Static execution

Validation, parsing, value construction, text queries, mutation, decoding, and
writing are `const fn` operations. They use ordinary execution budgets and the
same ownership and failure rules as runtime calls. For example:

```carven
const compact = JSONValue::parse(r#"{ "enabled": true }"#)?.to_json()?;
```

Completed `String` results retain their owning type. JSON values can also be
retained as constants, including their nested strings, arrays, and object members.
Copying a retained value creates independent mutable contents.
