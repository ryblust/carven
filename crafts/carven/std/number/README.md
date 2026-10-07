# Numeric text parsing

`std::number.parse` provides `parse_i64(str)`, `parse_u64(str)`, and
`parse_f64(str)`. Each returns the requested scalar or throws `ParseError::Invalid`
when the entire input cannot be consumed or the result is out of range.

Integer parsing uses base ten. A signed integer may begin with `-`; an unsigned
integer may not. Leading whitespace and a leading `+` are rejected. Floating
parsing uses the standard general format, including decimal exponents, infinity,
and NaN spellings. Call `is_finite()` on a floating result when finite values are
required. Syntax policies such as JSON's number grammar belong to their callers.

Parsing requires runtime execution. The native support delegates conversion to
`std::from_chars`; the Carven API owns its scalar types and failure domain.
