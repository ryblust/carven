# Types and context

This page defines type compatibility, inference, numeric operations, and borrowed
slices.

- [Types and compatibility](#types-and-compatibility)
- [Nominal type parameters](#nominal-type-parameters)
- [Type context and inference](#type-context-and-inference)
- [Numeric types and conversions](#numeric-types-and-conversions)
- [Read-only slices](#read-only-slices)
- [Floating methods](#floating-methods)
- [Numeric text parsing](#numeric-text-parsing)

## Types and compatibility

The source-spellable builtin types are:

```text
bool  char  str  String  void
i8 i16 i32 i64 isize
u8 u16 u32 u64 usize
f32 f64
u8x16 mask16 f32x4 mask4 u8x32 mask32 f32x8 mask8
```

Scalar and text names, `Vector`, and `Range` are defaults from `std::core`.
SIMD vector and mask names are defaults from `std::simd`; explicit imports and
aliases remain available. Import `Operation` from `std::async`. These are
ordinary bindings: declarations, imports, and type parameters can shadow them,
and aliases preserve their canonical type identities. Runtime locals do not
occupy type positions, but do shadow expression qualifiers. Only structural
`ptr` retains its contextual reservation.

`u8x16`, `mask16`, `f32x4`, `mask4`, `u8x32`, `mask32`, `f32x8`, and `mask8`
are explicit fixed-width logical SIMD types.

Structures and enums are nominal: identity comes from the declaration, not
from structural similarity. Arrays are identified by both element type and
extent. Nonempty tuples are identified by their ordered element types, arity,
and nesting: `(i32, bool)` differs from `(bool, i32)` and `((i32,), bool)`.
`(T,)` is a one-element tuple; `(T)` groups a type. Slices are identified by their
element type. Function-view types include parameter access, parameter types,
success result, and failure set.

For Carven types, ordinary compatibility requires the same canonical type.
Numbers, slices, text, callable views, and pointers have operation-specific
contextual conversions.
There are no general implicit numeric promotions, structural conversions,
truthiness conversions, or opaque dynamically typed values.

`void` is the zero-information unit type, with the single value `()`. The empty
tuple type `()` denotes the same canonical type as `void`. A completed call with
a `void` success result produces that value. It can be bound, passed as an
argument, stored in a field, array or tuple, returned, and matched with a wildcard
or binding pattern. Its default value is `()`, and two unit values compare equal.

```carven
fn finish() {}
fn accept(value: void) {}
fn example() {
    let completed: void = finish();
    let product: (i32, void) = (7, completed);
    let units: [void; 2] = [(), ()];
    accept(completed);
}
```

Generated C++ represents unit values in ordinary value positions with
`carven::runtime::Void`, including parameters and aggregate elements. An
infallible synchronous callable with a unit result returns native C++ `void`,
including native imports and exports. Async and fallible completion carriers
likewise retain `void` as their success type. The source call still produces
`()` when it completes successfully.

C++ checks construction and conversion operations involving external types.

## Nominal type parameters

Structs and payload enums can declare type and failure-set parameters:

```carven
struct Box<T> { value: T }
enum Maybe<T> { None, Some(T) }

let boxed = Box<i32> { value: 7 };
let present = Maybe<i32>::Some(7);
let absent: Maybe<String> = .None;
```

A parameter is scoped to its declaration. Field and payload types can use type
parameters, nested nominal applications, arrays, tuples, slices, pointers, and
Operations. `throw E` declares a failure-set parameter, which may appear in
failure contracts but cannot be used as an object type:

```carven
import std::async using Operation;

struct Pending<T, throw E> { operation: Operation<T, throw E> }
```

An application supplies every argument explicitly and in its declared kind,
for example `Pending<i32, throw Error>` or `Pending<i32, throw ()>`.
Failure sets contain copyable nominal payload types; `throw ()` supplies the empty set.
The declaration and its ordered canonical type and failure-set arguments
determine nominal identity;
repeated applications share that identity.
Array extents and nominal failure payload types must be independent of type
parameters. Callable-view signatures may use type and failure-set parameters.
Named external C++ types may use type parameters as template arguments; native
operations are checked by C++ after concrete substitution.

Definitions are checked even when unused. Recursive applications may forward
or permute parameters; a recursive cycle that constructs larger arguments is
rejected. By-value layout cycles are rejected independently of argument growth.
Pointers and slices break by-value storage cycles.

Concrete instances use the existing construction, copying, Take, borrowing,
matching, default-initialization, and equality rules for their actual types.
An enum still has no implicit default case. Visibility checks consider both the
generic declaration, every type argument, and every member of a failure-set
argument.

Class operations inherit their owner's type and failure-set parameters and lexical
access to its private representation. An operation may declare additional parameters;
those follow the owner parameters in its checked generic environment and are inferred
or supplied at the operation call. Receiver Read, Write, Take, failure, and static
execution rules are the same as for an ordinary class. Parameterized numeric enums
are not supported.

Functions support the same parameter kinds and infer omitted arguments from their
signature, actual argument types, and immediate expected result.

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
| Tuple element with an expected tuple type | Corresponding element type |
| Value-control result branch | Expected result type, when supplied |
| Lambda parameter and result | Expected callable view, subject to explicit annotations |

Grouping passes an existing expected type to its operand. Numeric and string
literals and contextual enum cases use that expected type.
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
no expected `Range<T>` type. An expected `Range<T>` supplies `T` to the bounds.
Thus `0..text.byte_length()` has type `Range<usize>`: the literal is checked directly as
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
type; later uses do not revise it. There is no general search for a
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

Integer literals with `ns`, `us`, `ms`, or `s` suffixes produce the ordinary
`Duration` struct declared in `std::time` without a type import. Naming `Duration` in an
annotation or constructor requires its ordinary import from `std::time`.
`100ms` has the same value as
`Duration { nanoseconds: 100000000 }`; it is not a distinct numeric type.
Scaling is checked at compilation and the result must fit signed `i64`
nanoseconds. `-9223372036854775808ns` is admitted. Floating duration literals
are not supported. Dynamic values use the ordinary checked constructors such
as `milliseconds(value)?`.

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
trap report and diagnose during static execution. Signed right shift is arithmetic.

`isize` and `usize` are the signed and unsigned native-model integers. Their
width is fixed by the supported compilation data model and participates in the
ordinary integer rules above. Analysis uses the host pointer-sized integer
widths; the target must use the same data model.

`f32` and `f64` use IEEE 754 binary32 and binary64 storage. Both provide
`is_finite() -> bool`, which is true for zeros, subnormal values, and normal
values, and false for infinities and NaNs. The query evaluates its receiver once
and is available in constant, interpreted, and native execution. Runtime floating
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
denotes a fixed array. Arrays and `Vector<T>` create views explicitly through
`slice()`. When a destination requires `[T]`, either owner implicitly creates
the same borrowed view. This applies to bindings, assignments, arguments,
returns, and aggregate elements. Both owners also support `slice(start, end)`
directly. Element types remain invariant; Write arguments require an actual slice
slot. Array literals use the expected slice element type, including empty
literals. Without a slice destination, an array remains an array. Conversion does
not extend backing lifetimes.

Calling `slice()` on an existing slice preserves its backing and range.
Slices provide `length() -> usize`, `is_empty() -> bool`, read-only integer
indexing, Read iteration, and `slice(start: usize, end: usize) -> [T]` with a
half-open range. Empty ranges, including `slice(length, length)`, are valid. Invalid
indices and ranges terminate, following array bounds handling. Slices do not
support element writes, Write iteration, pointer extraction, or comparison.
Their element representation is invariant; creating a view never converts or
copies its elements. Reading an element follows the ordinary Read/value rules.

Slice and text views share storage-borrow analysis. A view with known Carven
backing protects its entire array, Vector, or String against mutation, replacement, Take, and destruction.
Disjoint subranges are not analyzed separately. Copies, arguments, returned
values, aggregate fields, closures, and failure values retain backing relations.
Extracting an element also retains any borrows inside that element. Temporary
backing follows existing expression and retained-loop rules; storing a view
does not extend its lifetime.

`str::from_utf8(bytes)` validates `[u8]` and borrows its storage.
The returned text follows the same lifetime and mutation
checks as slices.

Constant initializers can retain completed array or Vector contents as slices
with immutable program-lifetime backing.

## Floating methods

`f32` and `f64` provide builtin value methods without imports. All methods use
Read access and support constant initializers, `const fn`, interpretation, and
native execution. Numeric arguments must have the receiver's precision;
unsuffixed literals receive that expected type. Numeric results retain the
receiver's precision.

| Method | Contract |
| --- | --- |
| `value.is_finite() -> bool` | False for NaN and either infinity |
| `value.abs()` | Positive magnitude; either zero becomes +0; NaN remains NaN |
| `value.min(other)` / `value.max(other)` | Propagate either NaN; equal numbers retain the receiver, including its zero sign |
| `value.clamp(lower, upper)` | Assert `lower <= upper`, including ordered bounds; preserve a NaN value and return the selected bound or value |
| `value.sqrt()` | Preserve signed zero and +infinity; negative nonzero inputs, -infinity, and NaN produce NaN |

Equal clamp bounds are valid. Reversed or NaN bounds are assertion failures,
including when the result is discarded; they are not typed failures. The receiver
and explicit arguments evaluate once in source order before the operation.
Methods require direct calls and do not produce first-class method values.

Floating results follow the selected execution host's floating environment.
Cross-target bit identity, NaN payloads, and non-default rounding modes are not
guaranteed.

## Numeric text parsing

Every scalar integer type, including `isize` and `usize`, and `f32` and `f64`
provide a builtin associated function `T::parse(text: str) -> T throw NumberParseError`.
Calling the function and propagating its failure do not require importing the
error type.
`NumberParseError` is a standard-source nominal enum with one nullary case,
`Invalid`. Import it from `std::core` when naming it in source.
Aliases of numeric types provide the same factory. Parsing supports constant
initializers, `const fn`, interpretation, and native execution.

Parsing consumes the entire input and checks the requested type's range.
Malformed, incomplete, empty, or unrepresentable input throws
`NumberParseError::Invalid`. Use `?` to propagate the failure or `try/catch` to
handle it, including during static execution. Unhandled failures leaving a static
root follow the ordinary static execution diagnostic rules.

Integer parsing uses base ten. Signed types accept an initial `-`; unsigned types
reject it, including `-0`. Leading whitespace, a leading `+`, and trailing content
are rejected. Floating parsing uses general decimal format with exponents and
accepts infinity and NaN spellings. It preserves signed zero and parses directly
in the requested precision. Call `is_finite()` when finite values are required.
Parsing is locale-independent; application policies such as JSON number grammar
and nonzero port numbers belong to their callers.

```carven
const port = u16::parse("8080")?;
```

An explicit failure contract names the imported type:

```carven
import std::core using NumberParseError;

const fn parse_port(text: str) -> u16 throw NumberParseError => u16::parse(text)?;
```

The text input evaluates once with Read access. The result and error values do
not borrow the input storage. Factories require direct calls.
