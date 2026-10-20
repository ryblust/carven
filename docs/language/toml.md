# TOML

This page defines configuration parsing, owned values, and borrowed traversal in `std::toml`.
The parser is written in Carven and uses `std::simd.scan` for byte runs.
It supports constant execution, interpretation, and native compilation.

## Parsing

```carven
import std::toml using { parse, TOMLError, TOMLValueError }

fn project_name(text: str) -> String throw TOMLError + TOMLValueError {
    let document = parse(text)?;
    let project = document.get("project")?;
    return project.get("name")?.as_str()? as String;
}
```

`parse(text: str)` and `TOMLValue::parse(text)` return an independent `TOMLValue` tree
whose root is a table. Empty documents produce empty tables. `validate(text)`
checks the same grammar and discards the tree. All three operations are `const fn`.

`parse_bytes(bytes: [u8])`, `TOMLValue::parse_bytes(bytes)`, and
`validate_bytes(bytes)` first validate the entire input as UTF-8. They throw
`UTF8Error` before parsing when encoding is invalid; text entry points throw
`TOMLError`. The parser implements TOML 1.0 syntax, including comments, quoted and
dotted keys, basic and literal strings, multiline strings, integers, floats,
booleans, date-times, arrays, inline tables, tables, and arrays of tables.
CRLF and LF in multiline strings become LF. Duplicate keys, conflicting table
definitions, and extensions to closed inline tables are rejected.

Project fields such as `project`, `version`, and `dependencies` have no special
meaning to this module. It performs neither file access nor dependency resolution.

## Reading values

`kind()` returns `TOMLValueKind`: `String`, `Integer`, `Float`, `Boolean`,
`OffsetDateTime`, `LocalDateTime`, `LocalDate`, `LocalTime`, `Array`, or `Table`.

| Operation | Result |
| --- | --- |
| `as_str()` | Borrowed decoded string |
| `as_i64()` | Signed 64-bit integer, including hexadecimal, octal, and binary inputs |
| `as_f64()` | Parsed floating value, including explicit `inf` and `nan` |
| `as_bool()` | Boolean |
| `date_time_text()` | Validated original date-time spelling, preserving fractional precision and offset |
| `length()` | Table member or array element count |
| `elements()` | Borrowed array elements as `[TOMLValue]` |
| `members()` | Borrowed table members as `[(String, TOMLValue)]` in insertion order |
| `get(key)` / `at(index)` | Independent child value |
| `has(key)` | Whether a table contains this exact decoded key |
| `key_at(index)` / `member_value_at(index)` | Borrowed key or independent child in insertion order |

All queries are `const fn`. Except `kind`, incompatible queries throw
`TOMLValueError::WrongKind`. Missing keys throw `MissingKey`; invalid indices throw
`IndexOutOfBounds`. Numeric queries require the corresponding value kind.
Key queries interpret their arguments literally; use successive `get` calls to
traverse a dotted path. Arrays of tables use ordinary array and table queries.

Values own their complete trees and can outlive the input text. `elements` and
`members` borrow the stored collections without copying child trees. Their
read-only slices use ordinary indexing and iteration:

```carven
let document = parse("name='hello'\nversions=[1, 2]")?;
for (key, value) in document.members()? {
    if key == "versions" {
        for version in value.elements()? {
            println(version.as_i64()?);
        }
    }
}
```

Text and collection views borrow their receiver. Keep that owner alive and do not
replace or move it while using a view; escaping borrows are rejected. Reading a
child into an owned `TOMLValue`, including `get`, `at`, and `member_value_at`,
copies that subtree. Copy text with `as String` when an independent lifetime is
needed. The API provides parsing and queries; it does not rewrite or serialize
TOML documents.

## Failures and limits

`TOMLError` contains a `kind` and a zero-based UTF-8 byte `offset` in the original
input. An incomplete input may point at EOF. Kinds distinguish unexpected input,
invalid keys, escapes, Unicode, control characters, numbers, date-times, duplicate
keys, table redefinitions, and the depth limit. Integer overflow is an invalid
number. Table paths and nested values have a maximum depth of 64.
