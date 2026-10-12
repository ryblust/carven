# Numeric text parsing

`std::number.parse` provides `parse<T>(str)` for every integer type, including
`isize` and `usize`, and both `f32` and `f64`. It returns the requested scalar or
throws `ParseError::Invalid` when the entire input cannot be consumed or the
result is out of range.

```carven
import std::number.parse using { parse, ParseError };

fn parse_port(text: str) -> u16 throw ParseError => parse<u16>(text)?;
```

Parsing checks the range of `T` during conversion; for example,
`parse<u8>("256")` fails. Application constraints, such as excluding port zero,
remain with the caller.

Integer parsing uses base ten. A signed integer may begin with `-`; an unsigned
integer may not. Leading whitespace and a leading `+` are rejected. Floating
parsing uses the standard general format, including decimal exponents, infinity,
and NaN spellings. Call `is_finite()` on a floating result when finite values are
required. Syntax policies such as JSON's number grammar belong to their callers.

Parsing requires runtime execution. The native support delegates conversion to
`std::from_chars`; the Carven API owns its scalar types and failure domain. Other
target types are rejected by the native provider contract.
