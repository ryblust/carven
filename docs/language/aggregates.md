# Aggregates

[Language](README.md)

This page defines classes, structures, arrays, owning sequences, enums, and their construction.

- [Ordinary value classes](#ordinary-value-classes)
- [Contextual construction](#contextual-construction)
- [Structures and arrays](#structures-and-arrays)
- [Enums](#enums)
- [Owning sequences](#owning-sequences)

## Ordinary value classes

An ordinary `class` is an encapsulated nominal value. Declaring one adds no heap
allocation, reference identity, inheritance, virtual dispatch, or custom
copy/move/destruction hooks.
Field types determine copying, ownership, stored borrows, and destruction through
the nominal product rules.

```carven
class Counter {
    value: i32,
    fn create(value: i32) -> Counter => { value: value };
    fn read(self) -> i32 => self.value;
    fn increment(&self) { self.value += 1; }
}
var counter = Counter::create(3);
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

Associated operations use `Type::name(...)`; `Type::name` also selects their
ordinary callable value. Instance operations use
`expression.name(...)`. The receiver is evaluated once, before explicit arguments,
and binds through the ordinary Read/Write/Take rules according to untyped
`self`, `&self`, or `&&self`. The fixed name is contextual to the first class
operation parameter; `self` remains an ordinary identifier elsewhere. An
operation without this receiver is associated; there is no `static` keyword.
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
It can construct and use class values inside `const fn` bodies, `const` blocks,
and static tests, with the same representation access rules. Direct class-operation
calls in module constant initializers, local `const` initializers outside a static block or test, and type-forming constant expressions
require a `const fn` wrapper. Class representation patterns, nested class
declarations, and C++ import/export methods are invalid.

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
| `Sequence<T>` | Empty owning sequence; no element default is required |
| `range<T>` | Empty exclusive range from zero to zero |
| `[T; N]` | N independently default-initialized elements |
| Structure | All fields recursively default-initialized |
| External C++ type | Native value initialization, validated by C++ |

A zero-length array requires no element default. Ordinary classes, numeric and payload enums,
callable values and views, `void`, and entry or iteration-only opaque types have
no default value. A structure or nonempty array containing such a type also has
no default value and must be constructed explicitly. Carven does not select an enum
case or invent a callable target. `CV-TYPE-DEFAULT-INITIALIZATION` identifies a
requested default that is unavailable. External constructors and their effects
follow the native boundary's requirements.

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
expected array or slice type, it must be nonempty; its element type is inferred
from an unambiguous element, and every element must be compatible. An expected
`[T; N]` supplies the element type and requires exactly N elements. An expected
`[T]` supplies the element type and borrows the resulting array under ordinary
lifetime rules. Empty `[]` is valid with either an expected `[T; 0]` or `[T]`.

Arrays provide `len() -> usize` and `is_empty() -> bool` directly. These queries
use the fixed extent while evaluating the receiver once, including its effects
and failures; no slice conversion is required.

`[expression; N]` constructs an array with N elements. N follows the same
constant nonnegative extent rules as `[T; N]`. An expected array or slice supplies
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

Structures do not support `==` or `!=`. Array equality is available only when its
element type supports equality, including for zero-length arrays. Nominal
declarations may not form a by-value storage cycle through structure fields,
enum payloads, or arrays; a zero-length array still contributes its element
edge. Function parameter and result types do not contribute storage edges.
Declaration-surface visibility recursively follows the
[audience rules](modules.md#declarations-and-names).

## Owning sequences

`Sequence<T>` owns an ordered collection of initialized values. It supports
deep copying, whole-owner transfer, checked Read and Write indexing, and
iteration. Taking an indexed element is invalid; the collection keeps every
element initialized. Recursive products and enums can contain sequences of
their own values.

```carven
var names = Sequence<String> {};
names.push("first" as String);
names[0] = "updated" as String;
for name in names { check(name.as_str() == "updated"); }
```

`len()` and `is_empty()` read the sequence. `push(value)` copies an element;
`push(&&value)` transfers it. `remove(index)` removes one checked position and
`clear()` removes all elements. Structural mutation requires Write access and
cannot run while element storage is borrowed, including through nested fields.
Write iteration updates elements while retaining the sequence owner.

Elements must have Carven-defined owning value semantics. Borrowed text, slices,
callable values and views, external C++ values, and products containing them are
invalid elements. Raw pointers keep their ordinary pointer contracts. The
sequence exposes neither contiguous slices nor uninitialized storage.

Sequences and payload borrowing are admitted in runtime code. They are not
admitted in constant or interpreted execution. Sequence storage is a runtime
facility; it does not extend the rules for operations on generic type parameters.

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
