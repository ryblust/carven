# Aggregates

This page defines classes, structures, arrays, tuples, owning vectors, enums, and their construction.

- [Ordinary value classes](#ordinary-value-classes)
- [Contextual construction](#contextual-construction)
- [Structures and arrays](#structures-and-arrays)
- [Tuples](#tuples)
- [Enums](#enums)
- [Owning vectors](#owning-vectors)

## Ordinary value classes

The conventional associated factory is `Type::new(...)`. It is an ordinary
function returning a completed value; its spelling supplies no allocation,
implicit invocation, or lifecycle behavior. Factories with a specific role retain
names such as `from_str`, `parse`, and `open`.

An ordinary `class` is an encapsulated nominal value. Declaring one adds no heap
allocation, reference identity, inheritance, virtual dispatch, or custom
copy/move/destruction hooks.
Field types determine copying, ownership, stored borrows, and destruction through
the nominal product rules.

```carven
class Counter {
    value: i32,
    fn new(value: i32) -> Counter => { value: value };
    fn read(self) -> i32 => self.value;
    fn increment(&self) { self.value += 1; }
}
var counter = Counter::new(3);
counter.increment();
check(counter.read() == 4);
```

Fields can be selected and the representation can be constructed only within the
lexical body of the defining class. This authority applies to other values of
that same class and to lambdas written in its operations. It does not extend to
module peers or called free functions. Ordinary `fn` follows the class audience;
`private fn` is accessible only within that exact class body. Operations do not
enter the module namespace, and fields and operations share one unique-name
namespace. There is no member overloading or implicit `self` lookup.

Associated operations use `Type::name(...)`; synchronous associated operations
also permit `Type::name` to select their ordinary callable value. Instance operations use
`expression.name(...)`. The receiver is evaluated once, before explicit arguments,
and binds through the ordinary Read/Write/Take rules according to untyped
`self`, `&self`, or `&&self`. The fixed name is contextual to the first class
operation parameter; `self` remains an ordinary identifier elsewhere. An
operation without this receiver is associated; there is no `static` keyword.
An associated operation may be `async fn` and uses ordinary cold-operation and
await rules with its explicit inputs. Async operations with an instance receiver
are not admitted.
Write preserves the original updatable place. Take consumes the complete owner.
Explicit arguments still require matching access markers. An instance operation
cannot be selected as a standalone value.

In-body representation construction supplies every field explicitly; an empty
class permits `ClassName {}` within its body. There is no automatic default or
implicit factory invocation, including through a containing struct or nonempty
array. Empty arrays and default pointers/slices do not construct an element.
Valid existing class values remain copyable or transferable under their field
contracts. A type name is not callable.

The compiler uses private fields for type contents, ownership, and cleanup. A
class does not support `==` or `!=`; its operations can expose named comparisons.
Structural display prints the class name without expanding its fields. Generated
C++ represents checked operations as ordinary functions. External C++
implementations follow their explicit interoperation contracts.

A class operation may be `const fn` under ordinary execution capability rules.
Const operations can be called in module and local constant initializers,
type-forming constant expressions, `const fn` bodies, `const` blocks, and static
tests. Calls retain private-operation visibility and declared argument access.
Local constant expressions retain their defining class's representation access;
constant results remain subject to ordinary freezing rules. Class representation
patterns, nested class declarations, and C++ import/export methods are invalid.

## Contextual construction

A construction can omit its type when the expression already has a known
expected type: `return { value: 1 };` in a function returning a record is checked
as construction of that record. Named fields and empty initialization are
supported; positional construction retains its explicit type. Empty initialization
follows the [default-initialization rules](#structures-and-arrays), including
in-class construction of an empty class. Contextual construction checks class
representation access, fields, ownership, borrowing, and static-execution admission.

Expected types flow from declared function and callable results, annotated
bindings, assignment destinations, resolved Carven parameters, record fields,
and known array element types. Value-control branches receive their surrounding
expected type. An already established type from forward analysis
of array elements or branches may also supply context; no later expression or
use is searched to infer an earlier construction. Missing context is an error.
There is no structural field-name search, failure-set selection, or native
constructor/overload inference. Native construction keeps its explicit type.
For `-> T throw E`, a return construction uses `T`, not the failure carrier.

In expression positions, `{}` means empty initialization. At the start of a
match or catch arm body, `{ field: value }` is a construction but `{}` remains
an empty branch block; write `({})` for an empty construction arm. A function,
if, or try body retains its required block, whose final expression may itself
be a contextual construction.

## Structures and arrays

A structure is a nominal product with ordered, uniquely named fields. Field
access selects by name. Positional construction maps supplied values to fields
in declaration order; named construction maps each initializer to its declared
field. Nonempty construction requires every field exactly once. Supplied
expressions execute once in source order. An empty `T {}` requests default
initialization of the whole value, with fields initialized in declaration order.
Duplicate, unknown, extra, or incompatible initializers are invalid.

```carven
struct Config { attempts: i32, enabled: bool, label: String }
let empty = Config {};                    // 0, false, empty String
let named = Config { attempts: 3, enabled: true, label: {} };
let positional = Config { 3, true, String {} };
```

Default initialization is a type operation with these values:

| Type | Default |
| --- | --- |
| Integer or floating-point | Zero (positive floating-point zero) |
| `bool` | `false` |
| `char` | Unicode scalar U+0000 |
| `str` or `String` | Empty text; String owns its independent storage |
| `ptr<T>` or `ptr<&T>` | Null pointer, subject to ordinary non-null checks |
| `[T]` | Empty read-only slice |
| `Vector<T>` | Empty owning sequence; no element default is required |
| `Range<T>` | Empty exclusive range from zero to zero |
| `[T; N]` | N independently default-initialized elements |
| `void` | The unit value `()` |
| Tuple | Each element recursively default-initialized |
| Structure | All fields recursively default-initialized |
| External C++ type | Native value initialization, validated by C++ |

A zero-length array requires no element default. Ordinary classes, numeric and payload enums,
callable values and views, and entry or iteration-only opaque types have
no default value. A structure or nonempty array containing such a type also has
no default value and must be constructed explicitly. Carven does not select an enum
case or invent a callable target. `CV-TYPE-DEFAULT-INITIALIZATION` identifies a
requested default that is unavailable. External constructors and their effects
follow the native boundary's requirements.

Generic definitions use the same structural rules: `Vector<T>` and `[T; 0]`
have defaults without requiring a default for the unknown `T`. Structures derive
this capability from their fields.

An empty `T {}` requests this operation for types accepted by construction
syntax, including builtin types such as `i32 {}` and `String {}`. Nonempty Carven
construction requires a structure or authorized class representation; enum-case
construction and callable adoption retain their separate forms. Every completed
structure construction still initializes every field exactly once. Local binding
declarations continue to require an initializer, and array literals retain their
exact element-count rules.

The same defaults apply during runtime, interpretation and static
execution, within each execution mode's admitted type and operation subset.
Default construction does not extend borrowed lifetimes or relax access rules.
Constant results follow the freezing rules; default owning text remains
an owning value during execution. Native defaults remain delegated to C++ and
are outside Carven's interpreter and constant executor.

An array type has one element type and a constant nonnegative extent. A
zero-length array type is valid and still carries its element type. A
comma-separated literal has an extent equal to its element count. Without an
expected array, slice, or Vector type, it must be nonempty; its element type is inferred
from an unambiguous element, and every element must be compatible. An expected
`[T; N]` supplies the element type and requires exactly N elements. An expected
`[T]` supplies the element type and borrows the resulting array under ordinary
lifetime rules. An expected `Vector<T>` constructs an owning vector. Empty `[]` is valid with
an expected `[T; 0]`, `[T]`, or `Vector<T>`.
An empty slice constructed from a zero-length array retains no backing storage.

Arrays provide `length() -> usize` and `is_empty() -> bool` directly. These queries
use the fixed extent while evaluating the receiver once, including its effects
and failures; no slice conversion is required.

`[expression; N]` constructs an array with N elements. N follows the same
constant nonnegative extent rules as `[T; N]`. An expected array, slice, or Vector supplies
the element type; otherwise it is inferred from the expression. An expected
array extent must equal N. Each element evaluates the expression afresh in
index order, with ordinary access and lifetime rules. For N = 0, the expression
is checked and determines the element type but is not executed. No element
default is required. If an element fails, construction stops and already
constructed elements follow ordinary cleanup rules. Literal expansion is bounded
by the compiler's construction budgets.

Array indexing accepts an integer index. A constant negative index or one
greater than or equal to the extent is diagnosed before lowering. A dynamic
out-of-bounds index terminates deterministically. Receiver and index are each
evaluated once, receiver first. Element mutation requires a mutable receiver.
Indexing a consumed complete array transfers the selected element into an
independent value. Indexing existing array storage reads the element under its
ordinary copying rules. A slice always selects its borrowed element storage,
including after the slice descriptor itself is consumed.

Structures do not support `==` or `!=`. Array equality is available only when its
element type supports equality, including for zero-length arrays. Nominal
declarations may not form a by-value storage cycle through structure fields,
enum payloads, or arrays; a zero-length array still contributes its element
edge. Function parameter and result types do not contribute storage edges.
Every nominal type exposed by a declaration surface must be visible to that
surface's audience, including types nested in fields and payloads.

## Tuples

A tuple is an ordered product with a type for each position. Both values and
types use parentheses: `(20, "answer")` and `(i32, str)`. Nesting is preserved;
`(i32, (bool, str))` differs from `(i32, bool, str)`. Elements are evaluated from
left to right, once each. A known tuple type supplies an expected type to each
element.

`(value)` groups an expression; `(value,)` constructs a one-element tuple.
The same distinction applies to types and binding targets. The empty product
`()` is the unique `void` value and also spells the `void` type. A `void`
element keeps its logical position in a nonempty tuple.

Local `let`, `var`, and `const` declarations accept recursive tuple targets:

```carven
var (number, _, (text,)): (i32, void, (str,)) = (42, (), ("ready",));
number += 1;
check(text == "ready");
```

The target must match the initializer's arity and nesting. Each named runtime
element becomes a complete owner with the declaration's mutability. Taking one
element binding makes that binding unavailable and leaves its siblings
available. `let (a, b) = owner;` follows ordinary owning-copy rules;
`let (a, b) = &&owner;` transfers the complete source owner. The initializer is
evaluated once, and its element storage remains stable for the bindings' scope.
Discarded elements remain alive until that scope ends. A local `const` freezes
the source once and binds the selected constant elements.

Tuples support elementwise equality when all elements do, and default
initialization when all elements have defaults. Positional fields use zero-based
decimal names: `pair.0`, `nested.1.0`, or `pair.0.method()`. A position has no
leading zeros, underscores, suffix, or exponent. Out-of-bounds positions are
rejected during analysis, including any position on `()`. Arrays and slices
retain expression indexing with `[]`; tuples do not accept dynamic indexing.
Pointer access `pointer->0` abbreviates `(*pointer).0` under the ordinary
pointer validity and access rules.

A positional field follows the source's Read or Write access, evaluates its
receiver once, and has the selected element's type. Taking `&&pair.0` from a
still-available whole owner is invalid, just like taking a structure field.
`(&&pair).0` instead consumes the complete product before selecting its result;
the remaining elements retain ordinary full-expression cleanup.

Function and lambda parameters accept tuple targets while retaining one parameter
with the complete product type. Read and Write targets project the parameter's
existing storage under its ordinary snapshot or borrowing policy. Take targets
give their named slots independent owner identities without relocating the
complete product. Static parameter targets retain static value semantics.

Range loops accept recursive tuple targets for tuple-valued array or slice
elements. Their names follow the current element's Read or Write access and
cannot be taken. Tuple patterns in `match` and enum payloads recursively match
each position; `()` matches unit, `(pattern)` groups, and `(pattern,)` matches a
singleton. Pattern names retain ordinary match binding and guard rules.

## Owning vectors

`Vector<T>` owns a growable ordered collection of initialized contiguous elements.
It supports deep copying, whole-owner Take, checked Read and Write indexing, and
iteration. Recursive products and enums may contain vectors of their own values.
Copying creates independent owned contents; raw pointer elements still copy their
addresses. Taking a complete vector makes the source unavailable.

```carven
var numbers: Vector<i32> = [10, 20, 30];
numbers.push(40);
let removed = numbers.remove(1);
check(removed == 20);
let tail: [i32] = numbers.slice(1, 3);
check(tail[0] == 30);
```

An expected Vector type supplies the element type for comma-separated and repeat
literals, including `[]` and `[value; 0]`. Each repeat element evaluates its
expression independently, under the array literal evaluation rules. Without
Vector context, literals retain their fixed-array type. Existing arrays and
slices require `Vector<T>::from_slice(values)` to copy their elements into an owner.

| Operation | Access and result |
| --- | --- |
| `Vector<T>::new()` | Empty owner, zero capacity |
| `Vector<T>::from_slice(values: [T])` | Independent owning copy of the elements |
| `Vector<T>::with_capacity(minimum: usize)` | Empty owner with at least the requested capacity |
| `v.length()` / `v.is_empty()` | Read; `usize` / `bool` |
| `v.capacity()` | Read; capacity in elements |
| `v.reserve(minimum: usize)` | Write; ensure minimum total capacity, preserving contents |
| `v.push(value)` / `v.push(&&value)` | Write; copy or transfer an element into the end |
| `v.remove(index)` | Write; return the removed owner and preserve remaining order |
| `v.clear()` | Write; destroy all elements and retain capacity |
| `v.slice()` | Read; borrowed `[T]` |
| `v.slice(start: usize, end: usize)` | Read; borrowed half-open subrange |

`Vector<T> {}` and contextual empty construction also produce the empty default;
no element default is required. Capacity growth factors are unspecified. Appending
within capacity does not reallocate. Removal and clear retain capacity; copying
preserves contents without promising the same spare capacity. Allocation failure
and capacity overflow terminate without a nominal throw effect.

A Vector supplies `[T]` directly in a slice destination context, including Read
arguments. Borrowing copies no elements and extends no lifetime. A live slice
protects the whole owner from mutation, replacement, Take, and destruction.
Structural mutation is rejected while element storage is borrowed, even when
reserved capacity would avoid reallocation. Read and Write iteration retain the
owner and prohibit structural mutation; Write iteration can update elements.
Taking an indexed element is invalid; `remove` extracts it without leaving a hole.
An admitted self-element push copies the argument before modifying storage.

Elements require Carven-defined owning value semantics. Borrowed text, slices,
callable values and views, external C++ values, async operations, and products
containing them are invalid elements. Raw pointers keep their ordinary target
contracts. `Vector<bool>` uses ordinary elements and supports `[bool]` views.
There is no raw pointer extraction or uninitialized-storage API.

Logical construction, copying, transfer, mutation, extraction, and slicing work
in native, interpreted, and constant execution, including capacity queries,
reservation, and capacity construction. Completed constants preserve Vector
ownership, element types, contents, and capacity. A slice retained at initializer
completion has immutable static backing; ordinary runtime slices still borrow.
Copying a retained Vector creates independent owned contents before mutation.
Execution limits bound aggregate storage, reserved capacity, and work.

## Enums

Every enum is a closed nominal sum and has at least one case. An enum whose
cases are all nullary has the numeric profile. Its omitted underlying type is
`i32`; an explicit underlying type must be an integer. Every numeric case has a
normalized integer value. Explicit initializers must be representable, and
omitted initializers start at zero or use checked increment from the preceding
case. Duplicate normalized case values are invalid. Numeric cases are constant
facts and may be cast to integers; integer-to-enum conversion is unavailable.

If any case declares positional payload types, the enum has the payload
profile. It may mix payload and nullary cases, but cannot declare an underlying
type, numeric initializer, or integer cast. It has no default value. A payload
case is a first-class constructor function; a nullary case is a value.

Contextual `.Case(arguments)` and `.Case` forms require an expected enum type
that identifies the owner. Such context may come from a typed binding, return,
assignment, call argument, aggregate position, or an unambiguous sibling or
equality operand. Cases are never found by global case-name search and cannot
be imported independently. Calling a nullary case or using a payload case
without its exact payload arity is invalid.
