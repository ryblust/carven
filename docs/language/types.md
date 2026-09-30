# Types and context

[Language](README.md)

This page defines type compatibility, inference, numeric operations, and borrowed
slices. User-defined data and construction are covered by [Aggregates](aggregates.md).

- [Types and compatibility](#types-and-compatibility)
- [Type context and inference](#type-context-and-inference)
- [Numeric types and conversions](#numeric-types-and-conversions)
- [Read-only slices](#read-only-slices)

## Types and compatibility

The source-spellable builtin types are:

```text
bool  char  str  String  void
i8 i16 i32 i64 isize
u8 u16 u32 u64 usize
f32 f64
u8x16 mask16 f32x4 mask4 u8x32 mask32 f32x8 mask8
```

Builtin type names are reserved. Module declarations cannot reuse them.

`u8x16`, `mask16`, `f32x4`, `mask4`, `u8x32`, `mask32`, `f32x8`, and `mask8`
are explicit fixed-width logical SIMD types.
Their operators, memory contracts, and native backends are documented in the
[SIMD craft](../../crafts/carven/std/simd/README.md).

Structures and enums are nominal: identity comes from the declaration, not
from structural similarity. Arrays are identified by both element type and
extent. Slices are identified by their element type. Function-view types include parameter access, parameter types, success
result, and failure set.

For Carven types, ordinary compatibility requires the same canonical type.
Contextual conversions are defined for [numbers](#numeric-types-and-conversions),
[text](text.md#owning-string-and-text-borrowing), [slices](#read-only-slices),
[callable views](functions.md#signatures-and-views), and [pointers](pointers.md).
There are no general implicit numeric promotions, structural conversions,
truthiness conversions, or opaque dynamically typed values.

`void` is the absence of a value. It may describe a callable's success result,
but cannot be a parameter type, structure field, array element, runtime binding
value, or match subject. Calling a `void` function as a statement is valid;
binding its nonexistent result is not.

Operations involving external C++ types have delegated construction and
conversion rules, specified under [C++ interoperation](interop.md#external-operations-and-conversions).

## Type context and inference

An expected type is supplied by a surrounding source operation. It guides
literal typing and the defined value conversions. An explicit annotation fixes
the required type; an incompatible initializer is rejected.
The following are supported sources of context:

| Position | Source of expected type |
| --- | --- |
| Annotated binding or constant initializer | Declared type |
| Assignment value | Destination type |
| Call argument | Selected parameter type |
| Return value | Declared or context-supplied callable result |
| Structure field or enum payload initializer | Declared field or payload type |
| Array element with an expected array type | Expected element type |
| Value-control result branch | Expected result type, when supplied |
| Lambda parameter and result | Expected callable view, subject to explicit annotations |

Grouping passes an existing expected type to its operand. Contextual
[numeric literals](#numeric-types-and-conversions),
[string literals](text.md#owning-string-and-text-borrowing), and
[enum cases](aggregates.md#enums) use that context as specified in those rules.
Context does not change a binding's declared type. An admitted value conversion
can produce the expected type. Context does not supply an omitted call access
marker or insert a capture.

For ordinary binary operands, a direct unsuffixed numeric literal on the left
obtains context from a right operand that is not a direct unsuffixed numeric
literal. Otherwise the left operand receives the surrounding context and
supplies context to the right operand. For equality, a direct contextual
`.Case` or `.Case(...)` obtains its enum context from the other operand when
that operand is not itself a direct contextual case. This selection precedes
the numeric-literal rule. Logical operators instead require `bool` operands.

Integer range bounds use the same numeric sibling-selection rule when there is
no expected `range<T>` type. An expected `range<T>` supplies `T` to the bounds.
Thus `0..text.len()` has type `range<usize>`: the literal is checked directly as
`usize`, without converting an `i32` value. Two unsuffixed bounds without an
expected type still default to `i32`. Suffixed literals and existing bindings
keep their types; incompatible bounds and literals outside the selected type's
range are rejected. This applies to stored ranges, calls, returns, and loops.

These sibling-selection rules inspect the direct operand form: they do not
search inside grouping, unary operations, or composite expressions to discover
a literal or case. Thus grouping can receive context without itself acting as
a direct contextual operand for sibling selection. Type-context selection
never changes runtime left-to-right evaluation order.

Without an annotation, a runtime binding takes its initializer's inferred
type; later uses do not revise it. [Unsuffixed numeric defaults](#numeric-types-and-conversions),
[array element inference](aggregates.md#structures-and-arrays),
[lambda signature inference](functions.md#signatures-and-views), and
[private callable failure inference](failures.md#sets-and-callable-contracts)
are defined with those operations. There is no general search for a
type that would make all uses succeed. A contextual form without a determining
context is invalid; spell the type or enum owner explicitly.

## Numeric types and conversions

The integer types are the fixed-width signed and unsigned types plus `isize`
and `usize`; the floating types are `f32` and `f64`. `char` is not numeric.
Unsuffixed integer literals default to `i32` and unsuffixed floating literals
default to `f64`. In an expected numeric context, an unsuffixed literal may
instead adopt a representable type from the same integer or floating family.
Floating literal text is converted directly to the selected precision using the
host's native parsing, without an intermediate floating type. A literal outside
the selected type's conversion range is rejected.

Numeric types are otherwise compatible only with the identical
canonical type. There is no implicit integer promotion, signedness conversion,
or integer-to-floating conversion.

When neither type is external C++, `expression as Type` accepts exactly:

- identity conversion between the same canonical type;
- every integer-to-integer conversion;
- conversions in either direction between an integer and `bool`;
- an integer to `f32` or `f64`;
- `f32` to `f64`;
- a numeric enum value to any integer type;
- `char` to `u32`;
- `str` to `String`, producing an independent owning copy.

It rejects narrowing `f64` to `f32`, floating-point to integer or `bool`,
`bool` to floating-point, integer to enum, other conversions between `char` and a
numeric type, and `String` to `str` through `as`.

Integer-to-`bool` yields `false` for zero and `true` for every nonzero value,
including negative values. `bool`-to-integer yields zero for `false` and one for
`true` in the requested integer type. These conversions require `as`; they do
not make integer conditions valid.

Integer negate, add, subtract, multiply, and left shift wrap at the
Carven type width; compound assignment and increment/decrement inherit the same
rule. Signed `MIN / -1` returns `MIN`, with remainder zero. Division or remainder
by zero and a negative or out-of-width shift count terminate at runtime with a
[trap report](execution.md) and diagnose during static execution. Signed right shift is arithmetic.

`isize` and `usize` are the signed and unsigned native-model integers. Their
width is fixed by the supported compilation data model and participates in the
ordinary integer rules above. Analysis uses the host pointer-sized integer
widths; the target must use the same data model.

`f32` and `f64` use IEEE 754 binary32 and binary64 storage. Runtime floating
arithmetic and integer-to-floating conversion use the corresponding native C++
operations. The language does not supply a rounding-mode control or a separate
floating exception mechanism. Native floating results depend on the selected
C++ compiler and floating environment. Floating division follows those native
operations, including their handling of zero divisors.

Static execution and interpretation perform each floating operation
using the compiler host's native `float` or `double` environment. They support
negation, addition, subtraction, multiplication, division, comparisons, and the
ordinary admitted numeric casts. Signed zero, infinities, and NaNs are values;
floating division by zero does not use the integer divide-by-zero diagnostic.
Frozen results preserve their representation when reconstructed in generated C++.
Results can differ from runtime execution under different target settings,
rounding modes, or optimization choices. Target-specific evaluation is not
implemented.

## Read-only slices

`[T]` is a copyable, nonowning, read-only view of contiguous elements. `[T; N]`
denotes a fixed array. An array creates a view explicitly through
`array.as_slice()`. When a destination requires `[T]`, an array `[T; N]`
implicitly creates the same borrowed view. This applies to bindings, assignments,
arguments, returns, and aggregate elements. Element types remain invariant;
Write arguments require an actual slice slot. Array literals use the expected
slice element type, including empty literals. Without a slice destination,
an array remains an array. Conversion does not extend backing lifetimes.

Slices provide `len() -> usize`, `is_empty() -> bool`, read-only integer
indexing, Read iteration, and `slice(start: usize, end: usize) -> [T]` with a
half-open range. Empty ranges, including `slice(len, len)`, are valid. Invalid
indices and ranges terminate, following array bounds handling. Slices do not
support element writes, Write iteration, pointer extraction, or comparison.
Their element representation is invariant; creating a view never converts or
copies its elements. Reading an element follows the ordinary Read/value rules.

Slice and text views share storage-borrow analysis. A view with known Carven
backing protects its entire array or String against mutation, replacement, Take, and destruction.
Disjoint subranges are not analyzed separately. Copies, arguments, returned
values, aggregate fields, closures, and failure values retain backing relations.
Extracting an element also retains any borrows inside that element. Temporary
backing follows existing expression and retained-loop rules; storing a view
does not extend its lifetime.

The UTF standard library supplies validation and text
construction from `[u8]`. Borrowed text retains the input storage relationship
and follows the same lifetime and mutation checks as slices.

Constant initializers can instead create slices with program-lifetime backing; see
[Frozen constant slices](constants.md#frozen-constant-slices).
