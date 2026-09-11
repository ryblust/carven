# Language semantics

This document defines the validity and observable behavior of Carven programs.

## Contents

- [Modules and names](#modules-and-names)
  - [Compilations, crafts, and modules](#compilations-crafts-and-modules)
  - [Declarations and names](#declarations-and-names)
- [Types and context](#types-and-context)
  - [Types and compatibility](#types-and-compatibility)
  - [Type context and inference](#type-context-and-inference)
- [Values and constants](#values-and-constants)
  - [Module constants](#module-constants)
  - [Constant expressions](#constant-expressions)
  - [Numeric types and conversions](#numeric-types-and-conversions)
  - [Read-only slices](#read-only-slices)
  - [Unicode text](#unicode-text)
  - [Owning String and text borrowing](#owning-string-and-text-borrowing)
  - [String interpolation](#string-interpolation)
- [Pointer values](#pointer-values)
- [Bindings, access, and mutation](#bindings-access-and-mutation)
- [Functions and callable values](#functions-and-callable-values)
  - [Functions and calls](#functions-and-calls)
  - [Lambdas and callable views](#lambdas-and-callable-views)
- [Aggregates](#aggregates)
  - [Structures and arrays](#structures-and-arrays)
  - [Enums](#enums)
- [Evaluation and control flow](#evaluation-and-control-flow)
  - [Operators and evaluation](#operators-and-evaluation)
  - [Control flow and loops](#control-flow-and-loops)
  - [Patterns and matches](#patterns-and-matches)
- [Failure contracts](#failure-contracts)
- [C++ interoperation](#c-interoperation)
- [Entry points, tests, and diagnostics](#entry-points-tests-and-diagnostics)
  - [Entry points and tests](#entry-points-and-tests)
  - [Diagnostics](#diagnostics)

## Modules and names

### Compilations, crafts, and modules

A compilation is one closed compiler boundary supplied by the driver or build
system. Its module catalog is exactly the explicit source-input batch. Every
source input has one canonical module path, a nonempty sequence of components matching
`[A-Za-z_][A-Za-z0-9_]*`, including keyword spellings.
Path derivation from host filenames is driver policy rather than language
semantics.

A leading canonical path of `crafts.<name>.<path>` belongs to the module domain
anchored by `crafts.<name>`. All other canonical paths belong to the unprefixed
module domain. The reserved leading form requires both `<name>` and a nonempty
`<path>`; `crafts` and `crafts.<name>` alone are not complete module paths. A
non-leading `crafts` component is ordinary. The `crafts.<name>` prefix gives
modules a craft-qualified domain for name resolution and bare declaration
visibility within the compilation.

The official craft is `carven`, stored in `crafts/carven/`. Its standard-library
modules live under `std/`. The reserved import prefix `std::` selects that
standard library, so `std::utf.text` names `crafts.carven.std.utf.text`.

Imports form a prefix at the start of a module. Module imports use one of three
structured references:

- `model.user` starts at the importing module's domain root;
- `.value` starts in the importing module's logical module directory;
- `json::parser` starts at the external craft `json` and names `crafts.json.parser`.

A module's logical directory is its canonical path without the final module
name; removing that name does not change the module domain. The reference
components after `.` are appended to that directory. For example, `.value` in
`a.b.main` denotes `a.b.value`, and `.value` in `crafts.json.a.b.main` denotes
`crafts.json.a.b.value`.

Unprefixed and leading-`.` references stay inside the importer's module domain.
Craft-qualified references select the named craft, with `std::` selecting
`crafts.carven.std`. Resolution succeeds only when the resulting path names
another module in the supplied input batch; an absent module or self-import is
an error.

For an importer at `crafts/foo/models/user.cv`, the three forms select:

| Import | Input module path |
| --- | --- |
| `std::utf.text` | `crafts/carven/std/utf/text.cv` |
| `std.utf` | `crafts/foo/std/utf.cv` |
| `.std.utf` | `crafts/foo/models/std/utf.cv` |

Cross-module selection always requires an explicit import. A selected name
must be visible to the importer under the declaration rules below. Explicit
selections cannot collide with a local declaration or bind one name to different
symbols, and they deterministically shadow same-name wildcard candidates.
Multiple wildcard providers are ambiguous only when an actual use requires that
name. Import declarations have no runtime side effects. A declaration is used
when one of its selected bindings uniquely resolves an actual reference; an
otherwise unused declaration produces one `CV-LINT-UNUSED-IMPORT` warning.

The same prefix may contain C++ header imports. These supply native
declarations; a `using` clause also supplies external name lookup. They do not
resolve Carven modules.

### Declarations and names

Modules may declare functions, structures, enums, constants, tests, and C++
source fragments. Function, structure, enum, and constant names share one module
namespace. Duplicate module declarations are invalid; function overloading is
not supported.

Nominal identities and declaration signatures are elaborated across the closed
compilation before function and test bodies are checked. Required constant
facts participate in the same dependency graph. Declaration order
therefore does not control whether a module declaration can be named; valid
forward constant dependencies and forward or mutually recursive function calls
are supported. A cycle among constant-required facts is invalid.

Unqualified lookup checks the innermost lexical scope first, then enclosing
scopes, module declarations, and imports. A declaration in an inner lexical
scope may shadow an outer binding. Declaring the same name twice in one lexical
scope is invalid. Lambda bodies add a capture boundary: a free runtime binding
must be captured explicitly before ordinary lexical lookup may cross it.

Named functions, structures, enums, and module constants use one visibility
model:

| Declaration form | Audience |
| --- | --- |
| `private` | Only its defining module |
| bare (no visibility modifier) | All modules in the same craft (module domain) |
| `export` | All modules in the compilation, across crafts |

Import resolution selects a module; visibility determines which of its
declarations the importer may select.

Visibility follows the defining craft, not the spelling used to import it.
Official standard-library modules belong to the `carven` craft, including those
selected through `std::`. Ordinary application modules share the unprefixed
module domain and can import each other's bare declarations.

Every declaration is visible in its defining module. Qualified lookup selects
the named module or nominal owner.

A declaration surface may refer only to nominal declarations whose audience
contains the surface's audience. This structural check recursively covers
function parameters, results, failure sets, nested callable types, structure
fields, enum payloads and underlying types, arrays, and a module constant's
type and normalized semantic value. Function bodies and constant-evaluation
proofs are implementation; identities eliminated by normalization do not enter
the published surface. A violation uses `CV-TYPE-VISIBILITY-LEAK`.


## Types and context

### Types and compatibility

The source-spellable builtin types are:

```text
bool  char  str  String  void
i8 i16 i32 i64 isize
u8 u16 u32 u64 usize
f32 f64
```

Structures and enums are nominal: identity comes from the declaration, not
from structural similarity. Arrays are identified by both element type and
extent. Slices are identified by their element type. Function-view types include
parameter access, parameter types, success result, and failure set.

For Carven types, ordinary compatibility requires the same canonical type.
The defined exceptions are contextual numeric literals, compatible callable-to-view
adoption, and narrowing `ptr<&T>` to `ptr<T>`. There are no general implicit numeric promotions, structural
conversions, truthiness conversions, or opaque dynamically typed values.

`void` is the absence of a value. It may describe a callable's success result,
but cannot be a parameter type, structure field, array element, runtime binding
value, or match subject. Calling a `void` function as a statement is valid;
binding its nonexistent result is not.

Operations involving external C++ types have delegated construction and
conversion rules, specified under C++ interoperation.

### Type context and inference

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

Grouping passes an existing expected type to its operand. Contextual numeric
literals and enum cases use that context as specified in their sections.
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

These sibling-selection rules inspect the direct operand form: they do not
search inside grouping, unary operations, or composite expressions to discover
a literal or case. Thus grouping can receive context without itself acting as
a direct contextual operand for sibling selection. Type-context selection
never changes runtime left-to-right evaluation order.

Without an annotation, a runtime binding takes its initializer's inferred
type; later uses do not revise it. Unsuffixed numeric defaults, array element
inference, lambda signature inference, and private callable failure inference
are defined in their respective sections. There is no general search for a
type that would make all uses succeed. A contextual form without a determining
context is invalid; spell the type or enum owner explicitly.


## Values and constants

### Module constants

A top-level `const` creates a module-scope name for one typed compile-time
value:

```carven
private const radix = 10;
const retry_limit: i32 = 3;
export const protocol_version: u32 = 1;
```

Private and bare module constants may infer a unique concrete type or state it
explicitly. An exported constant must state its type; a literal suffix in the
initializer is not a declaration annotation. Omitting that type produces
`CV-CONST-EXPORTED-TYPE`. The annotation supplies the initializer's expected
type and uses ordinary compatibility rules. The target must be a named
identifier, so top-level `const _ = ...;` is invalid.

Module-constant initialization is elaborated from resolved declaration
dependencies rather than source order. Every successful use selects the same
normalized value; a dependency cycle produces `CV-CONST-CYCLE`. A module
constant has Read access only and is compile-time-only, with no runtime storage,
address, or linkage.

### Constant expressions

A `const` initializer must be proven by Carven; target acceptance is not enough.
Constant facts include scalar and string literals, numeric enum cases,
resolved local and module constants, grouping, supported casts, supported pure
unary and binary operations, constant `str.len()` and `str.is_empty()`, and
payload-case construction whose payloads are all constant. General calls,
ordinary aggregate construction, and control-flow expressions are not Carven
constant expressions. Local and module constant declarations are
compile-time-only; their later uses denote the selected normalized value without
creating a runtime binding or lambda capture.

A constant integer cast to an `N`-bit integer reduces the mathematical value
modulo `2^N`. An unsigned target is that residue; a signed target interprets the
same bits as an `N`-bit two's-complement value. Accepted constant casts remain
constant facts.

Constant integer arithmetic is checked. Literal range errors, integer overflow,
division by zero, and invalid shift counts are Carven diagnostics and are never
evaluated through undefined host arithmetic. A runtime binding does not disable
constant checking of its initializer: a provably overflowing literal operation
is rejected even in a `let` initializer.

Floating literals, supported casts, and equality can supply constant facts.
General floating arithmetic and ordering do not supply constant facts; for
example, `const sum = 1.0 + 2.0;` is rejected although the same operation may
initialize a runtime binding. Negation of a numeric literal is normalized with
its sign during literal checking, including through grouping parentheses.
Thus `-(2147483648)` is a valid `i32` minimum even though the positive literal
alone is out of range.

A known runtime result does not make an expression admissible in a `const`
initializer. For example, `(source() == 1) && false` still contains a general
call and is not a constant expression. In a runtime expression, knowing the
result does not remove evaluation of operands that execute under the ordinary
short-circuit rules.

### Numeric types and conversions

The integer types are the fixed-width signed and unsigned types plus `isize`
and `usize`; the floating types are `f32` and `f64`. `char` is not numeric.
Unsuffixed integer literals default to `i32` and unsuffixed floating literals
default to `f64`. In an expected numeric context, an unsuffixed literal may
instead adopt a representable type from the same integer or floating family.

Numeric types are otherwise compatible only with the identical
canonical type. There is no implicit integer promotion, signedness conversion,
or integer-to-floating conversion.

When neither type is external C++, `expression as Type` accepts exactly:

- identity conversion between the same canonical type;
- every integer-to-integer conversion;
- conversions in either direction between an integer and `bool`;
- an integer to `f32` or `f64`;
- `f32` to `f64`;
- a numeric enum value to any integer type.

It rejects narrowing `f64` to `f32`, floating-point to integer or `bool`,
`bool` to floating-point, integer to enum, conversions between `char` and a
numeric type, and non-identity `str` conversions.

Integer-to-`bool` yields `false` for zero and `true` for every nonzero value,
including negative values. `bool`-to-integer yields zero for `false` and one for
`true` in the requested integer type. These conversions require `as`; they do
not make integer conditions valid.

Runtime integer negate, add, subtract, multiply, and left shift wrap at the
Carven type width; compound assignment and increment/decrement inherit the same
rule. Signed `MIN / -1` returns `MIN`, with remainder zero. Division or remainder
by zero and a negative or out-of-width shift count terminate. Signed right shift
is arithmetic.

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

### Read-only slices

`[T]` is a copyable, nonowning, read-only view of contiguous elements. `[T; N]`
denotes a fixed array. An array creates a view explicitly through
`array.as_slice()`; there is no implicit array-to-slice conversion.

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

The [UTF library](../crafts/carven/std/utf/README.md) supplies validation and text
construction from `[u8]`. Its borrowed results follow the
[native access and lifetime contract](#external-access-and-lifetime).

### Unicode text

`char` is one immutable, copyable Unicode scalar value. It supports equality,
inequality, and matching, but not numeric arithmetic, ordering, truthiness, or
numeric conversion.

`str` is an immutable, copyable, value-passed UTF-8 view represented by a pointer
and byte length. Length defines its contents, including internal NUL bytes;
the view does not promise a trailing NUL. It has no owning
storage, `&str` type, or source lifetime syntax. Its backing can be static
literal storage, a checked borrow of a `String`, or externally supplied storage. Copying a view preserves its known backing relationship.

Decoded string and character literal values are not Unicode-normalized.

Both `str` and `String` provide:

- `text.len() -> usize` returns the UTF-8 byte count.
- `text.is_empty() -> bool` tests that byte count.
- `text.bytes` returns the read-only slice `[u8]`.
- `text.chars` is a copyable read-only range that decodes `char` values.

`bytes` and `chars` are computed projections, not general properties.
The `chars` view type cannot be spelled and supports only Read range iteration
and inferred value bindings. Byte views support all slice operations. User structures may declare same-named fields because member
resolution depends on the receiver type.


### Owning String and text borrowing

`String` is a builtin owning UTF-8 value, available without imports and distinct
from `str`, user declarations, and C++ types. Unqualified `String` in type
position or a factory qualifier selects the builtin before ordinary declarations.
Ordinary value lookup is unchanged; `::String` selects an external C++ name.

A String owns contiguous, valid UTF-8 bytes, including internal NUL. It performs
no normalization, case folding, or BOM removal. Equality and inequality between
two Strings compare their bytes. Length counts bytes. Storage layout, capacity,
address stability, trailing NUL, and allocation count are unspecified.

| Operation | Access and result |
| --- | --- |
| `String::new()` | Empty owning `String` |
| `String::from_str(text: str)` | Read text; independent owning copy |
| `s.len()` / `s.is_empty()` | Read receiver; `usize` / `bool` |
| `s.as_str()` | Read receiver; borrowed `str` |
| `s.bytes` / `s.chars` | Read receiver; borrowed byte/scalar range |
| `s.append(text: str)` | Write receiver, Read text; `void` |
| `s.push(value: char)` | Write receiver, Read scalar; `void` |
| `s.clear()` | Write receiver; `void` |

The queries `len`, `is_empty`, and `as_str` are O(1); `as_str` does not allocate
or transcode. `push` encodes one Unicode scalar into UTF-8. Mutable fields and
array elements can be Write receivers; temporaries and Read parameters cannot.
The dot receiver supplies its access, while ordinary arguments follow the usual
explicit-marker rules. Factories and methods require direct calls, including
grouped direct calls; they do not produce first-class method values.

Literals remain `str`. Construction and borrowing are explicit; there is no
implicit allocation or String-to-str conversion, mixed String/str equality,
String constant, String literal pattern, indexing, ordering, truthiness,
concatenation, direct iteration, or access to C++ container members. `String(...)`,
`String { ... }`, and non-identity `as` conversions between String and str are
invalid. Iterate `s.bytes` or `s.chars` instead.

Copying String creates independently owned content. Whole-owner Take transfers
the value and makes its source unavailable. Read String parameters alias the
caller's storage. Fields, elements, captures, results, and cleanup use ordinary
Carven value rules. Borrowing does not extend a String owner's lifetime.

A borrowed view keeps referring to its original storage when copied or passed
through aggregates, captures, functions, Write outputs, branches, loops, and
failure payloads. Owning copies made by `from_str` and extracted `u8`/`char`
values are independent of the source. A named value holding a view keeps the
borrow until that value is replaced, taken, or leaves scope; its last use does
not end the borrow.

While a view borrows a String, that String cannot be modified, replaced, or
taken, including by taking a containing owner. The check distinguishes fields
and elements; an unknown index may overlap any possible element. Write access
is nonexclusive: creating a Write alias or capture is allowed, but actual
writes through it must respect live borrows. Native Write access counts as a
possible write. This also applies to `append` and `clear` when contents would
not change. Self-append `s.append(s.as_str())` is rejected; copy to an independent
owner first.

Temporary views keep their backing borrowed until the consuming operation
finishes, including evaluation of its remaining operands. An independent result
ends that borrow when no other view remains. For example:

```carven
var text = String::from_str("hello");
text = String::from_str(text.as_str());
let snapshot = text;
text.append(snapshot.as_str());
```

The RHS copy finishes before assignment writes. Similarly,
`f(String::from_str(s.as_str()), &&s)` can be valid, while
`f(s.as_str(), &&s)` is rejected. Each receiver/operand is evaluated once in
source order, with the receiver or assignment target selected first. A selected
Read receiver observes its content when the operation executes.

A view may be returned from a Read String parameter when the caller backing
outlives the returned view. Views into callee-local Strings or Take parameters
cannot escape. Saving `String::from_str("x").as_str()` into a named view is
invalid; immediate consumption is valid. A range loop retains String owners
created while evaluating its header, including those passed through view-returning
functions; `for c in String::from_str("x").chars` is valid. The borrow lasts
through the loop; retained temporaries are destroyed on every loop exit.

An aggregate can contain both String and str fields, but cannot store a view
into its own String storage. Copying a String field creates independent content;
copying a view field keeps referring to its original storage.
A view may borrow an owning String stored in a live closure.

Nominal failure values can contain Strings under the ordinary copyable failure
contract; builtin String itself is not a failure type. Throw copies unless Take
is explicit. Borrowed text must remain valid through throw, propagation, catch
selection and guards, and rethrow. The original failure payload retains its
borrows independently of copied catch bindings. Unwinding must not destroy
their backing. A handler can copy borrowed content into an independent String result.
Completed effects are retained on failure; mutation is not rolled back.

Builtin String construction accepts valid text and scalars. Allocation failure
and unrepresentable lengths terminate.


### String interpolation

`f"..."` always produces an independent owning `String`, including `f""` and
text without holes. Ordinary string literals remain `str`.

```carven
f"Hello, {name}!"
f"Total: {price * count:.2f}"
f"ID: {id:08x}"
f"{{value}} = {value}"
f"{value:{width}.{precision}f}"
```

A hole contains an ordinary expression and an optional `:` format specification.
Formatting follows `std::format` rules for escaped braces, alignment, width,
precision, and type options. Dynamic width and precision holes contain Carven
expressions. The C++ compiler
checks the resulting format string and formatter availability. Custom C++
formatters receive their format specifications through the same mechanism.

Hole expressions, including dynamic format arguments, are evaluated once in
source order before formatting. Each hole has Read access. Direct `&` and `&&`
argument markers are rejected; nested calls retain their declared access rules.
Scalar Read values are saved, while String Read values alias their owners.
String formatting reads the contents after all holes finish evaluating. Text
views keep their backing borrowed throughout hole evaluation and formatting.
The result is independent of its inputs; named views keep their usual lifetimes.

`String` uses standard string formatting. A `char` is encoded as UTF-8 and also
uses standard string formatting, including width and precision. Other values use
their C++ representation and corresponding `std::formatter`; Carven supplies no
aggregate, enum, or callable formatting protocol.

Interpolation supports ordinary expression composition and failure propagation.
An early failure skips later holes and formatting, retaining completed effects
and destroying temporary values. Discarding the result still executes formatting.
Interpolation is excluded from constant
declarations, literal patterns, and positions requiring an ordinary literal.

Formatting preserves internal NUL and validates the completed output as UTF-8.
Invalid UTF-8, allocation failure, and runtime formatting errors terminate;
they do not introduce typed failures. C++ formatter retention and reentry remain
provider/caller responsibilities. Callable-view and Write-capture boundary
restrictions still apply.

## Pointer values

`ptr<T>` stores an address that grants Read access to T; `ptr<&T>` grants Write
access. Both are ordinary nullable, copyable address values. `let` and `var`
control reassignment of the address slot. For example, `let p: ptr<&T>` cannot
be reassigned but can modify its target. A Read aggregate preserves the complete
types of its pointer fields, including their target permissions.

Targets use ordinary type resolution and can be Carven types, external types,
or nested pointers. Carven does not classify the underlying kind of a native
alias. A pointer does not contain or own its target, so pointer fields permit
recursive structures. `ptr<void>` can be stored and passed but cannot be
dereferenced. Native aliases and operations remain subject to C++ legality.

`*p` accesses the target as a place; `p->member` is `(*p).member`. The address
must be available and locally proven non-null. Target access comes from the
pointer type, independently of the slot's binding access. `let value = *p`
performs ordinary value initialization; C++ checks native copyability. `&*p`
passes a writable target to an ordinary Write parameter. `&&*p` is rejected:
the target is not an owned Carven binding. Existing callable-borrow boundaries
remain in force; an indirect address does not establish a tracked borrow.

For the same target type, `ptr<&T>` can become `ptr<T>` in a value context:
initialization, assignment, field or element construction, Read arguments,
constant initialization, and return. The reverse conversion is invalid. This
does not introduce array, aggregate, nested-target, or function-type covariance.
Each pointer layer has its own target/access pair. Copying an inner pointer
through an outer Read pointer retains the inner pointer's target permissions.
Context-free mixed pointer modes in array or branch results require a type
annotation. A type inferred from a sibling can type `nullptr`, but cannot
authorize narrowing inside nested arrays or branches. Inferred same-type copies
retain their full type.

Read pointer arguments save an address snapshot. Write parameters alias the
caller's slot and require the complete pointer type to match. Take parameters
also require an exact type, transfer the address value, and make its source
owner unavailable. Take does not zero other aliases or release the target.

`nullptr` needs a concrete pointer type context, including in an explicitly
typed `const`. There is no independent null type. Pointers support `==` and
`!=` with `nullptr` and with pointers to the same target type. They have no
implicit boolean conversion, ordering, arithmetic, direct indexing, integer
conversion, or Carven address-of operation.

### Local non-null checks

Non-null checking is local to each function and closure. It tracks null,
non-null, and unknown facts for local names, fixed Carven field paths, and constant
array indices. Tests against `nullptr` refine branches,
including negation, short-circuit expressions, and early returns. Branch joins
keep only common facts. A bool helper, API success code, or test assertion is
not a proof. Functions and closures establish their own conditions.

Address copies and permission narrowing carry the current fact to the new
slot without creating a lasting equality relationship. Assignment replaces a
fact; Take removes the source fact. Write calls clear facts for overlapping
storage. A slot passed to a Write parameter may escape through that callee.
Later calls clear facts for such slots, Write parameters, and Write captures.
Loops clear potentially written facts before checking the condition and body;
the checker merges normal exits without computing cross-iteration relations.

Native projections, dynamic indices, and memory reached through a pointer do
not retain cross-expression facts. Save such a pointer in a local handle and
check that handle. An unproven dereference reports `CV-PTR-NONNULL`; the compiler
does not insert a runtime trap. Passing a nullable address to an API is allowed
without a dereference proof. Non-nullness never proves that a target is alive.

### Native representation and responsibility

Read and Write target access lower to `const T*` and `T*`, composed by layer.
Read parameters pass the address by value; Write uses the corresponding pointer
reference. Generated code selects the dereference address before evaluating
later operands that could replace its slot. Saving or passing a pointer needs
only the target's declaration, so incomplete native types remain usable.
Forming the target's C++ type expression still follows its own requirements.
For example, `ptr<fn(T) -> void>` requires T to be complete for the callable's Read
parameter representation; a self-dependent representation is rejected by C++.

Copying, passing, and taking a pointer perform no allocation, reference counting,
or automatic release. Owners and adapters implement the external resource protocol.
Native `T**` output protocols and buffer traversal remain in `#[cpp]`; `&p` passes a
pointer slot by reference and does not implicitly compute a `T**`. Pointers are
not admitted in scalar `import(cpp)` and `export(cpp)` signatures.

## Bindings, access, and mutation

Carven separates value type, binding role, and access mode. Binding and argument
markers `&` and `&&` describe access; they do not construct reference types.
Inside `ptr<&T>`, `&` is the pointer type's explicit target-access parameter.

- `let` creates an immutable runtime owner.
- `var` creates a mutable runtime owner.
- A local `const` creates a compile-time lexical binding.
- An unmarked parameter has `Read` access.
- A parameter marked `&` has `Write` access.
- A parameter marked `&&` has `Take` access.
- An unmarked range binding has `Read` element access.
- A range binding marked `&` has `Write` element access when the source permits
  mutation.

Every binding declaration has an initializer. An optional declared type must be
compatible with that initializer. The exact target `_` creates no symbol, may
be repeated, and is never an unused-warning candidate. `_name` is an ordinary
identifier. A discarded runtime initializer is still evaluated and its value
lives until the enclosing scope ends; local `const _` still requires a constant
fact. Module constants always require a named target.

A local declaration publishes its name only after its declared type,
initializer, and any required constant proof are complete. Its own name is not
visible in those inputs, so an initializer may select an outer binding with the
same spelling. The completed binding is visible to following statements in its
scope.

A call repeats every declared parameter access exactly: `read(value)`,
`write(&value)`, or `take(&&value)`. `Read` grants non-owning read-only access,
`Write` grants non-owning mutable access, and `Take` transfers ownership. Write
access permits but does not require mutation. It is non-owning and nonexclusive:
the same mutable owner may be passed to multiple `Write` parameters in one call.
Arguments are evaluated left to right, and mutations take effect in the
function body's execution order. Read prevents writes to the parameter's current
storage. Contained pointers and Write captures retain their separate target access.

### Read values and aliases

Read parameters and Read range bindings preserve Carven array, String, and
closure storage through const references, including storage nested in Carven
aggregates. Native C++ types and other types use a const value when their C++
copy construction and destruction are trivial, and a const reference otherwise.
Explicit scalar `import(cpp)` and `export(cpp)` parameters always cross by value.

A by-value Read argument saves its value when that argument is evaluated. A
by-reference Read argument retains the selected storage, so writes through
another alias can affect subsequent reads. An explicit owning copy establishes
a separate value before the call; non-owning contents in that copy retain their
referents. Both representations obey the same access markers and Take-conflict
checks.

### Ownership transfer and mutation

Runtime `let`, `var`, ordinary pattern bindings, and `Take` parameters are
owners. A `Take` parameter is an immutable owner and may itself be taken.
`Read` and `Write` parameters, range bindings, closure state, and `const` are
not Take sources. The initial Take operand must be a complete owner or a
temporary; member and element Take are invalid.

`&&expression` is the ownership-transfer expression and has its operand's value
type. Taking a complete owner makes that binding unavailable, including for a
copyable type. This is a static state transition, not a runtime wrapper or a
promise of a particular C++ move operation.

Every use of an unavailable binding is invalid. A complete plain assignment to
a `var` is the only operation that may restore it, and restoration happens only
after the right-hand side completes normally. Partial assignment, compound
assignment, and update operators require the prior value. At a control-flow
join, a binding is available only when it is available on every normally
continuing path. Loops include their zero-iteration path and all backedges.

Member and element mutation inherit eligibility from their receiver. Plain
assignment requires a compatible value and uses the target C++ assignment
operation; it does not end the destination object's lifetime and reconstruct it.
A complete writable owner may be consumed by its RHS and restored by a normally
completed assignment, as in `x = relay(&&x)`. If the RHS fails, the owner remains
unavailable. Direct self-transfer assignment `x = &&x` is invalid, including
parenthesized forms. Member and compound assignment still require the old value.
Compound `+`, `-`, `*`, and `/`
assignment require numeric operands; `%`, bitwise, and shift assignment require
integer operands. Increment and decrement require an integer target. Assignment
evaluates the target before the new value, once each.

Take cannot conflict with a place access retained by an unfinished call,
including its callee and outer call arguments. A completed independent result
retains no read access to its inputs: `f(x + 1, &&x)` and
`f(identity(x), &&x)` are valid when their results are independent values.
Direct `f(x, &&x)` is invalid regardless of the target Read parameter policy.
Value-capture snapshots are
independent after creation, but capture creation requires the source to be
available. Lambda captures and range bindings do not support Take access.

A Write capture remains active with every live value that directly or
transitively contains its closure. Moving the closure into an aggregate,
projecting or reading it back out, and carrying it through a control-flow
result preserve that association. The capture ends with its actual holders,
not with the expression that created it; its source cannot be taken while any
such holder remains live.

### Value ownership and lifetime

Initializing an owner from an existing value without `&&` copies that value;
the source remains available. Initializing from `&&owner` transfers ownership
and changes source availability. An independently produced temporary can be
delivered to its destination without creating another source binding. These
are value and availability rules, not a fixed count of native constructor calls.

A copy owns its immediate value, including structure fields, array elements,
and enum payloads. Non-owning contents retain their backing: copying `str`, a
callable view, or a closure containing Write captures does not clone or prolong
the referenced storage. The recursive callable-view restrictions and closure
holder rules apply to these copies as well.

Local owners live in their enclosing lexical scope. Exiting that scope by
normal completion or a control transfer ends its local lifetimes. Temporary
storage belongs to the full expression that evaluates it unless an explicit
construct retains it, such as an owned match subject or a temporary range
source. Delivering a result into a longer-lived owner does not prolong the
lifetimes of that result's borrowed backing. Carven has no source destructor
or general lifetime-extension syntax.


## Functions and callable values

### Functions and calls

Every ordinary function parameter requires an explicit type. In a block-bodied
function, omitted result syntax means `void`. In an expression-bodied function
(`fn increment(a: i32) => a + 1;`), it infers the result from the body expression,
without using the caller's expected type. An explicit result annotation supplies
the expected type and is checked against the expression. Parameter names must be
unique. A call requires the exact
arity, access marker, and compatible argument type declared by the callable.
The callee is evaluated first, then arguments are evaluated once from left to
right. A concrete closure selects its object identity; a callable view selects
its target description. Invocation reads that target's current captures after
argument evaluation. An explicit closure copy requests a capture-value snapshot.

A bare `return;` is valid only for `void`. A return operand must have a successful
result compatible with the callable result, including `void`: `return action();`
evaluates a void expression and returns on normal completion. Return operands
follow ordinary failure-consumption rules; explicit propagation uses
`return action()?;`. A value return is required for every other ordinary result
type. Every reachable path of a non-`void` function or lambda must return a value.
The diagnostic is `CV-FLOW-MISSING-RETURN`.

Functions and lambdas may use `=> expression` instead of a block. This implicitly
returns the expression using the same evaluation, access, lifetime, and failure
rules as `return expression;`. A void expression is valid and infers `void`.
Failure propagation requires `?`. Block-bodied functions return values through
explicit `return` statements.

Function declarations may refer to later declarations because function identities
and heads are collected before bodies. Expression-body results are completed on
demand before a dependent function reference is used. A cycle that requires an
unfinished result is rejected with `CV-TYPE-RESULT-INFERENCE-CYCLE`; add an
explicit `-> T` to break the signature dependency. Operator constraints, constant
branches, and caller context do not solve such cycles. Typed failure contracts
are part of callable compatibility.

### Lambdas and callable views

#### Creation and captures

A lambda expression creates a closure value; it does not execute its body.
The capture list is mandatory, including `[]` for no captures. Capture entries
name runtime bindings visible at the creation site, and each name may occur
only once. A module declaration or compile-time constant cannot be an explicit
capture. Captures are established in list order when the expression runs.

| Source form | Stored state | Access inside the body | Effect on the source |
| --- | --- | --- | --- |
| `[value]` | An owned copy of the selected value | Read | Source remains available; later replacement of the source does not replace the copy |
| `[&value]` | An alias to writable source storage | Write | Writes reach the same storage; source ownership is not transferred |
| `[&&value]` | Unsupported | None | Rejected |

Write capture requires writable storage. A value capture cannot be assigned
through, and neither capture mode permits taking the captured binding. A nested
lambda must explicitly capture any runtime state it uses across its own
capture boundary; capturing it in an outer lambda does not implicitly capture
it in the inner one. Closure creation does not propagate failures from the
body; invocation uses the body's callable failure contract.

#### Closure identity, copies, and aliases

Every lambda expression has a unique concrete closure type. Assignment between
values of the same closure type copies value captures and rebinds Write
captures to the source closure's targets; it does not assign through those
captures. Unused explicit captures produce `CV-LAMBDA-CAPTURE-UNUSED`.

Repeated evaluation of one lambda expression produces values of the same
closure type. Separate lambda expressions have distinct types even when their
parameter lists, bodies, and captures look identical. A concrete closure type
has no source type spelling; an inferred binding preserves it.

`let copy = closure` creates another closure owner. Copying value captures
copies their values; copying Write captures preserves their referents. Copying
a closure does not recursively clone storage reached through aliases. In
particular, a value capture of a closure that itself has a Write capture still
permits writes to that original referent. An immutable closure owner may invoke
such writes: immutability of the owner does not revoke its stored Write access.

`let moved = &&closure` transfers the closure and makes the source owner
unavailable. It preserves the destination's Write-capture associations. Copies,
transfers, arrays of closures, and returned closures do not extend a captured
owner's lifetime. A closure may not escape into storage that outlives its Write
referent. Returning a closure that aliases a caller's Write parameter is
possible when the actual caller-owned storage outlives the resulting holder;
returning a closure that aliases a callee-local owner is invalid.

#### Signatures and views

A lambda parameter may omit its type only when an expected callable view
supplies the parameter type at that position. Without such an expected view,
every parameter requires an explicit type. An explicit lambda result type fixes
the result. Otherwise an expected callable view supplies it; with no expected
view, return operands determine the result, including `void`. The inferred or
expected signature is checked against the body before the closure type is completed.
These rules apply equally to block and expression bodies.

Source `fn(...) -> R throw E + F` denotes a non-owning callable view. Parameter
access, parameter types, and success result match exactly. A source callable
may have a smaller failure set than the expected view. Direct closure calls
retain their concrete closure type.

A callable view may be a parameter or local value, including local aggregate
storage, but it cannot be stored in a structure or enum, returned from a
function or lambda, or captured by a lambda. These restrictions apply
recursively through arrays. A capturing lambda temporary may form a view only
as a direct call argument and remains valid for that call. Noncapturing closures
form views without borrowing closure storage; the closure expression is
evaluated once. A named capturing closure may initialize a local view while its owner remains in an enclosing
scope.

Adopting a capturing closure as a callable view borrows the closure object; it
does not copy its captures or acquire ownership. Copying a view copies its
target description and preserves its backing requirement. While the view's
loan remains active, its closure owner cannot be taken. Ending an inner scope
containing the borrowers permits a later Take of the still-live owner.
Assigning a capturing target through a Write parameter of callable-view type
is rejected; the parameter does not establish a sufficient backing lifetime.

#### Invocation and snapshots

Calling a concrete closure first selects its object identity, then evaluates
arguments, then runs the body using that object's current captures. Assigning
new contents to the same closure object during argument evaluation therefore
affects this invocation. Calling a view first saves its target description:
reassigning the view variable during argument evaluation affects later calls,
but replacing contents of the already-selected closure object affects this
call. Neither form creates an implicit capture snapshot.

For example, the following uses one closure type produced by `factory`:

```carven
let factory = [](value: i32) {
    return [value](_: i32) { return value; };
};
var selected = factory(1);
let replacement = factory(10);
let rebind = [&selected, replacement]() {
    selected = replacement;
    return 0;
};
let snapshot = selected;
let current = selected(rebind()); // 10: selected object now holds 10
let saved = snapshot(0);          // 1: separate closure owner
```

Use an inferred closure copy when an independent capture-value snapshot is
required. An annotated `fn(...) -> R` binding requests a view instead.


## Aggregates

### Structures and arrays

A structure is a nominal product with ordered, uniquely named fields. Field
access selects by name. Positional construction maps values to fields in
declaration order; named construction maps each initializer to its declared
field. Both forms must initialize every field exactly once. Duplicate, unknown,
missing, or extra initializers and incompatible field values are invalid. Empty
construction is valid only for a structure with no fields. Initializer
expressions run once in source order, including when named initializers are
written out of declaration order.

For Carven types, `T { ... }` constructs structures only. It is invalid for
builtin, enum, and function-view types; literal and cast expressions, enum-case construction, and
callable adoption are separate forms.

An array type has one element type and a constant nonnegative extent. A
zero-length array type is valid and still carries its element type. Without an
expected array type, an array literal must be nonempty; its element type is
inferred from an unambiguous element, its extent is the element count, and every
element must be compatible. With an expected array type, each element is
checked in that element context and the literal must have exactly the declared
extent. Consequently `[]` is valid only with an expected `[T; 0]` type; it does
not request target-language value initialization.

Array indexing accepts an integer index. A constant negative index or one
greater than or equal to the extent is diagnosed before lowering. A dynamic
out-of-bounds index terminates deterministically. Receiver and index are each
evaluated once, receiver first. Element mutation requires a mutable receiver.

Structure equality is available only when every field supports equality. Array
equality is available only when its element type supports equality. Nominal
declarations may not form a by-value storage cycle through structure fields,
enum payloads, or arrays; a zero-length array still contributes its element
edge. Function parameter and result types do not contribute storage edges.
Declaration-surface visibility recursively follows the audience rules above.

### Enums

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


## Evaluation and control flow

### Operators and evaluation

The operand domains below apply to Carven operations. External operands
use the delegated operation and conversion rules.

Logical negation requires `bool`; numeric negation requires a numeric operand;
bitwise complement requires an integer. Arithmetic operators require identical
numeric operands. Remainder, bitwise, and shift operators require integers.
Ordering requires identical numeric operands. Logical `&&` and `||` require
`bool` and short-circuit from left to right.

Equality requires compatible operands and an equality-capable type. It is
available for `bool`, `char`, integers, floating-point values, `str`, `String`, arrays
whose elements support equality, structures whose fields all support equality,
numeric enums, payload enums whose payloads all support equality, and pointers
with identical target types. Callable
types, process-entry arguments, slices, and `str.chars` iteration views do
not support equality.

Payload enum values with different cases compare unequal. Values of the same
case compare payloads in position order with short-circuiting. Floating-point
equality follows IEEE `==`; `!=` is its negation.

All Carven expression evaluation is left to right and exactly once. This
includes callee before arguments, binary left before right, receiver before
index, assignment target before value, and initializer clauses in source order.
Short-circuiting operators and control expressions evaluate only the selected
operands or branches.

Every source operand and branch receives operation, result-compatibility, and
failure-consumption checks, including after a terminal statement or when a
constant proves it cannot execute. A proven inactive path contributes
no runtime evaluation, outward failure, ownership transition, or reachable-use
evidence. A nonreturning expression can occupy a position whose type is already
known. If its type cannot be determined, the enclosing operation is rejected;
for example, a call still needs a callable type and match still needs a subject
type. Nonreturning control does not exempt later source from type checking.

### Control flow and loops

Conditions and guards require `bool`. Value-form `if` requires an `else` and
all result branches must be compatible. Statement-form conditionals do not
produce a value.

`while` evaluates its condition before each iteration. A C-style `for`
creates one loop scope, evaluates its initializer once, tests its condition
before each iteration, executes the body, then evaluates step clauses in source
order. An omitted condition is true. `continue` in a C-style `for` proceeds
to its step clauses; `break` exits the loop.

An integer range evaluates its begin and end once, left to right. Both bounds
must have one compatible integer type. It visits the half-open ascending
sequence from begin through end-exclusive; begin greater than or equal to end
produces no iterations. Integer-range bindings cannot use Write access.

Arrays support Read and, for a mutable source, Write range bindings. Slices
and `str.chars` support Read bindings only. A range binding is scoped to the
loop and cannot be taken. Its name is not visible in its declared type or range
source; it is published only after those inputs complete and is then visible
throughout the loop body. Array iteration retains access to its source owner
until the loop exits, so the source cannot be taken during traversal. Writing
an element does not restore an unavailable whole-array owner.

Array and text-range loops access an element only after confirming that the
cursor is within the range. The terminating check does not access an element.

`break` and `continue` are valid only inside a loop. `return` targets the
current function or lambda. A value-form `if`, `match`, or `try` is a control
boundary: its result branches cannot return from an enclosing callable or
break/continue an enclosing loop, though a transfer may target a loop nested
within that branch. An invalid crossing uses
`CV-FLOW-TRANSFER-VALUE-BRANCH`.

### Patterns and matches

Both value and statement matches must be exhaustive. A value match also
requires compatible arm results. The subject expression is evaluated exactly
once. An rvalue subject is retained across guard rejection. When the subject
denotes a place, its receiver and index expressions identify that place once,
and selection requires that storage to remain stable. During a guard, obtaining
Write or Take access to overlapping subject storage is invalid, including
through aliases or callable captures. Guards may modify other storage, call
functions, and produce failures. A selected arm's body may modify the subject.
The first arm whose pattern matches and whose optional guard succeeds is selected.

Patterns are recursive. Enum payload positions admit case, literal, binding,
wildcard, `is`, and or-patterns. Structure and array destructuring are not
supported. Payload arity must be exact. A bare identifier creates an immutable
owning binding and never pins a constant. The selected payload is copied once
after its case matches and before the guard runs.

`is T` constrains the subject to `T`. The subject must already have a compatible
canonical type, so the constraint covers that type.

Or-pattern alternatives must bind the same names with the same types; an
alternative cannot bind one name twice. Guards run after pattern bindings are
available. They affect arm selection but do not contribute exhaustiveness
coverage.

Repeated alternatives and alternatives subsumed by another alternative in the
same or-pattern are errors. An arm whose complete pattern is covered by
preceding unguarded arms is unreachable. Missing-case diagnostics use a shortest
deterministic witness. Literal identity uses normalized language equality,
including treating `0.0` and `-0.0` as one floating pattern.

An unreachable match arm produces `CV-FLOW-UNREACHABLE-MATCH-ARM` at the arm's
pattern span. This warning does not prevent target artifacts from being
generated. The arm remains part of parsing and Carven semantic checking, but
it does not participate in runtime arm selection.


## Failure contracts

Carven models recoverable failure as a typed control effect represented to
source users by callable failure contracts. Failure values are copyable nominal
structures or enums. A failure contract denotes one exact closed mathematical
set: member spelling order and declaration order are not observable. An
explicit clause may name each failure type only once; `throw E + E` is invalid,
not a request to deduplicate entries. An explicit `throw` clause is an upper bound on a callable body. Module-private
non-entry functions and lambdas that omit it infer the least fixed-point failure set
across forward calls, direct recursion, and mutual recursion. A published
function—bare or exported—with a nonempty actual set must state an explicit
`throw` contract; omitting it produces
`CV-EFFECT-THROW-PUBLISHED`. Entry functions also require an explicit `throw`
contract for outward failures, regardless of declaration visibility. Tests must
handle every failure and cannot expose a failure contract. Failure types in a
published contract must be visible to that contract's audience.

A value with pending failures cannot be consumed where an ordinary completed
value is required. Postfix `?` consumes the pending failures of its operand at
that lexical position and transfers them to the nearest enclosing failure
target. The operand may be a call or a composite expression whose selected
evaluation path carries pending failures. Applying `?` to an infallible operand
is invalid. `throw` transfers the supplied failure and does not complete
normally.

Failure propagation does not roll back completed mutations or external effects.

Carven failures are independent of C++ exceptions. Native exceptions must be
handled in C++ before they escape a generated `noexcept` boundary to continue
execution.

`try` handles failures produced by its protected body. Catch arms may select a
failure type, wildcard, alternatives, payload patterns, and guards; they must
cover the protected body's actual failure set. Failures produced by a guard or
handler propagate to the enclosing failure target and are never caught again by
the same `try`. Alternatives in one arm form one or-pattern. The first matching
alternative establishes its bindings, then the arm guard runs once. A false
guard continues with the next arm. Catch-arm order remains observable even
though failure-set member order does not. A non-exhaustive catch diagnostic
identifies each failure type that is not fully covered, including partial payload
patterns and guards that may reject. A `try` around an infallible body is valid
and produces no diagnostic.

`rethrow` is valid only within a catch handler and transfers the caught failure
identity selected for that handler. Closure bodies are separate callable
boundaries: their failures and control transfers do not belong to evaluation of
the expression that creates the closure.


## C++ interoperation

`import <...>` and `import "..."` include C++ headers. Top-level `#[cpp]`
contributes an implementation source fragment. `import(cpp)` declares a
C++-implemented Carven function; `export(cpp)` publishes a Carven function to
C++ consumers. Source fragments are implementation-only.

### Names and lookup

A header import makes its header available through C++ inclusion; Carven does
not read its contents or associate declarations with individual headers.
A name with a leading `::` delegates lookup directly to the global C++ namespace:
`::calculate`, `::vendor::calculate`, and the type `::Point` bypass Carven local,
module, and builtin name lookup. Global paths work in expressions, type
annotations, casts, and construction, including external type applications such
as `::std::vector<i32>`. No header import is required for
Carven to admit such a reference; C++ checks whether the declaration is available.
Global references do not mark explicit `using` selections as used.

A header import without `using` creates no Carven names. A `using` selection
introduces external names or a namespace lookup environment in the importing
module:

```carven
import <vector> using std::vector;
import "provider.hpp" using vendor::{ Widget, create };
import "legacy.hpp" using { Point, calculate };
import <vector> using std::*;
```

Explicit selections bind their final name component. Selection paths are rooted
in the global C++ namespace: a single-component selection such as `calculate`
selects `::calculate`, while `vendor::Widget` selects `::vendor::Widget`.
A local explicit name selects one complete external path. Repeated selections
of that same path are allowed; different paths with the same final component
are a catalog error. An explicit selection takes precedence over opened
namespaces. All equivalent selection origins are marked used together.
Further qualified components append to the selected path. Overloads at that
one path remain C++'s responsibility. Cross-path overload merging is not supported;
use explicit global references when selecting different same-named providers.
Namespace selections open a C++ lookup environment without enumerating
declarations.

Local bindings and resolved Carven declarations retain their ordinary lookup
rules. Explicit external imports cannot collide with local module declarations.
Otherwise unresolved ordinary names can use a module's explicitly opened C++
namespaces. External declarations, overloads, template arguments, members and
conversions are checked by C++. External imports are module-local and are not
re-exported through Carven module imports. Explicit selections have unused-import diagnostics;
namespace selections do not.

### External types and construction

Type arguments are accepted only for external C++ names and must be types;
nested external type applications are supported. External construction uses
`T { ... }` with positional initializers, which may be empty. External types in
function signatures and field declarations must be named explicitly; local
owners may infer their type from an external expression. Such results are
not Carven compile-time constants and do not participate in pattern coverage or
failure-set construction. Native compilation checks constructor availability.
Intermediate storage can add native construction requirements; the
[toolchain construction limitation](toolchain.md#construction-limitation) identifies
the affected aggregate initializers.

### C string literals

`c"text"` produces an external `const char*` value pointing to immutable static
storage containing the decoded UTF-8 bytes and a trailing NUL. Empty text is
valid. Internal NUL, including `\0` and `\u{0}`, is rejected. Its type is always
a pointer, including direct native calls and template deduction; it is not a
character array or a Carven `str`. Copies retain access to static storage.

C strings support runtime type inference and external operations. They are
excluded from Carven constant declarations and literal patterns. Contextual
typing preserves their fixed external pointer type; conversions follow the
external conversion rules below.

```carven
import <cstdio> using std::printf;
fn main() { printf(c"Hello World\n"); }
```

### External operations and conversions

When a value must match a destination type, differing types are delegated to
C++ conversion if either is an external C++ type. This applies to annotated
runtime initializers, assignment values, Read and Take call arguments, return
values, aggregate elements, and value-control results with a selected result type.
Carven generates construction of the destination type from the source value;
C++ checks that construction, including narrowing restrictions. No `as` is
required to request this conversion. Matching external type descriptions need
no additional conversion.

Write arguments retain their writable storage instead of constructing a
converted value. When either type is external, C++ checks whether that storage
can bind to the parameter reference. The explicit `&` marker and Carven
writability checks still apply.

An external value used as a condition or an operand of `&&` or `||` is
converted to `bool`. This does not permit an ordinary Carven integer as a condition.
For `value as T`, if either type is external, C++ checks an explicit
`static_cast<T>`. It follows C++ conversion rules rather than the closed
Carven numeric conversion set.

Unary operations and non-logical binary operations involving an external
operand are checked by C++, including overload selection and result typing.
Logical `&&` and `||` retain Carven short-circuit evaluation. A result inferred from
an external expression remains an external type description even when C++
eventually determines a builtin type; Carven does not infer that equivalence
from the provider's declaration.

For example, given a C++ `values.size()` result:

```carven
let count: usize = values.size();
let narrow = values.size() as i32;
```

The first binding requests destination construction; the second requests an
explicit cast. C++ decides whether each conversion is valid.

### External access and lifetime

Carven access rules still apply. Ordinary external-call arguments provide Read
access, `&value` provides Write, and `&&value` takes an owner. Receivers inherit
their storage's access. Local bindings remain owners: initializing one from a
C++ reference result initializes an owned value, subject to C++ construction
rules. External member and index places inherit the root's access; external
indexing has the provider's bounds behavior, not Carven array bounds checks.
External calls do not expose typed failures. An exception escaping a generated
`noexcept` boundary terminates under C++ rules.

Carven checks its own storage availability and explicit access conflicts but
does not infer C++ reference retention, pointer validity or iterator invalidation.
Known callable borrows and Write captures cannot cross an undeclared external
contract. The `ptr` model checks target access and local non-null facts without
proving target liveness. External reference bindings, pointer arithmetic and
external iteration protocols are unsupported.

Explicit C++ calls accept dynamic text views and aggregates containing them.
Carven protects known backing during argument evaluation and the call. The
provider and caller are responsible for retention, returned aliases, reentry,
and indirect pointer lifetimes. Passing a view slot with Write access does not
establish that it stops borrowing its previous backing. Callable-view and
Write-capture escape restrictions also apply to values containing text views.
String has no implicit conversion to a native string container.

Native calls, construction, and representation conversions do not infer borrowed
storage for their results. C++ checks whether delegated operations are valid;
the provider and caller own the result's storage and lifetime contract. Returning
to a Carven type does not establish a previously unknown backing relationship.
Known input borrows remain checked while the operation evaluates.

### Source fragments

Each top-level `#[cpp]` payload remains an independent, byte-opaque C++ source
fragment. Carven does not parse or type-check it, interpolate Carven values, or
bind same-spelled Carven names. C++ owns macros, overloads, templates, linkage,
exceptions, lifetime, ODR, and undefined behavior inside each fragment.

### Scalar function boundaries

An `import(cpp)` declaration is private or bare and has no Carven body:

```carven
private import(cpp) fn native_value(value: i32) -> i32;
```

Calling it invokes a global C++ function with the same unqualified name. Carven
does not generate the provider declaration or parse, import, or compare its C++
signature.

Provider conformance, definition, and link satisfaction are author/toolchain
responsibilities. Same-spelled imports in different modules remain distinct
Carven capabilities even when C++ linkage makes their providers identical.
Names beginning with `_` or `__` receive no additional Carven restriction;
whether such a spelling is reserved in its downstream C++ context is the
author/toolchain's responsibility. `main`, `std`, and `carven` are not valid
provider names.

An `export(cpp)` declaration is an ordinary, complete-compilation-visible
Carven function with a Carven body. It is also declared in the generated C++
API for its module:

```carven
export(cpp) fn value() -> i32 {
    return 42;
}
```

An `import(cpp)` declaration cannot be exported directly in any syntactic
combination; re-export is not a language feature. A wider capability requires
an explicit ordinary Carven wrapper. Both boundary directions accept only
concrete, fixed-arity, infallible top-level functions. Every parameter uses
unmarked Read access and crosses by value. Write/Take access and nonempty
failure sets are unsupported.

The closed boundary type mapping is:

| Carven type | C++ type |
| --- | --- |
| `bool` | `bool` |
| `i8/i16/i32/i64` | `std::int8_t/std::int16_t/std::int32_t/std::int64_t` |
| `u8/u16/u32/u64` | `std::uint8_t/std::uint16_t/std::uint32_t/std::uint64_t` |
| `isize/usize` | `std::ptrdiff_t/std::size_t` |
| `f32/f64` | `float/double` |
| `char` | `char32_t` |
| `void` | `void`, result only |

`str`, `String`, arrays, structures, enums, callables, process arguments, and iteration
views are unsupported. An inbound `char32_t` is validated before it becomes a
Carven `char`; an invalid scalar terminates the program.

Public module components and exported function names use one deterministic
encoding. Safe C++ identifiers retain their spelling unless they start with
`cv_escaped_`. Other spellings become `cv_escaped_` followed by the lowercase
hexadecimal encoding of their original bytes. Safety excludes the project's
C++ keyword set, double underscores, and an initial underscore followed by an
uppercase letter. Encoding introduces no source-name reservation and depends
on neither compilation order nor other names. Native `import(cpp)` provider
names still denote existing C++ names and are validated without encoding.
A function/namespace prefix collision anywhere in the compilation's public API
tree is invalid.

### Native exception boundary

C++ exceptions are outside Carven failure sets. Carven does not translate them
into failures or catch them with `try`.

Generated Carven functions, closure call operators, import bridges, and export
façades establish `noexcept` boundaries. External operations need not declare
`noexcept`. An exception escaping one of these boundaries invokes
`std::terminate` under C++ rules. This applies to explicit `import(cpp)` calls
and to header-imported operations, including construction, member and operator
calls, and object lifetime operations.

The integration author owns native exception recovery. To continue Carven
execution after a native exception, C++ code must handle it before it escapes a
`noexcept` boundary. The handler may recover internally or communicate an
application-defined result through the chosen interface. An `import(cpp)`
adapter obeys the scalar, infallible signature restrictions above.

Native handlers may be defined in headers, linked C++ sources, or top-level
`#[cpp]` fragments under their C++ contracts. Fragments retain their
implementation-only placement and do not enclose generated Carven bodies.


## Entry points, tests, and diagnostics

### Entry points and tests

At most one function named `main` may exist in a compilation. Its module path,
module domain, and declaration visibility do not affect entry selection. It
accepts no parameters or one untyped Read parameter representing command-line
arguments; ordinary function parameter rules apply elsewhere. An entry with
outward failures must declare an explicit `throw` contract, including a
`private` entry. Its body must stay within that declared failure set.

Normal completion produces process status zero; a declared Carven result, if
present, is not a process exit status. A typed failure that escapes `main`
produces the host C++ `EXIT_FAILURE` status. The entry wrapper neither prints the
failure payload nor converts it to a C++ exception. Locals, returned values, and
failure payloads follow their ordinary cleanup rules before process completion.
Catching a failure and completing normally still produces status zero.

```carven
struct ConfigError {}
fn load_config() throw ConfigError { throw ConfigError {}; }
fn main() throw ConfigError { load_config()?; }
```

This program completes with a failure process status and no automatic output.

The command-line parameter is an opaque entry-only value. Its C++
runtime representation is not a Carven sequence contract and does not make the
parameter indexable or iterable.

A test is a module-local named body with no parameters or result. Test names
must be unique within their module and cannot be `main`. Tests participate in
parsing and semantic analysis regardless of whether the compiler is asked to
emit test artifacts. A test must handle every failure.

`check`, `require`, and `fail` are contextual test-operation statements, not
module symbols or callable values:

```text
check(condition);
check(condition, message);
require(condition);
require(condition, message);
fail();
fail(message);
```

The `condition` must have the exact Carven type `bool`; the optional `message`
must have the exact Carven type `str`. Invalid counts use
`CV-TEST-ARGUMENT-COUNT`, invalid conditions use
`CV-TEST-CONDITION-TYPE`, and invalid messages use `CV-TEST-MESSAGE-TYPE`.

The contextual form is available throughout a test body and its structurally
nested loop, `if`, `match`, `try`, and catch blocks. A source function or lambda
body is a separate callable boundary and receives no implicit testing names
or test transfer. Outside the exact standalone contextual form, the same
spelling participates in ordinary name lookup. Within a test, the exact
standalone form is always the test operation.

Arguments are evaluated eagerly, exactly once, and from left to right:
condition first, then message. The message is evaluated even when the condition
succeeds. A failed `check` reports and falls through. A failed `require`
reports and exits the whole current test, while a successful `require` falls
through. `fail` reports and exits the whole current test. Test exit is distinct
from return, loop transfer, and failure transfer, and is not caught by `try`.

Each failed operation reports the original `.cv` display origin, the 1-based
line of its operation name, its operation kind, and an optional runtime
message. `check` and `require` additionally report the complete condition
source: the original UTF-8 byte slice of the condition expression span,
including parentheses, whitespace, line breaks, and comments. `fail` reports
without a condition. The runtime reporter controls the presentation of these
records.

### Diagnostics

The following table lists selected semantic diagnostics in the current compiler:

| Code | Severity | Condition |
| --- | --- | --- |
| `CV-CONST-CYCLE` | Error | Required constant facts form a dependency cycle |
| `CV-CONST-EXPORTED-TYPE` | Error | An exported module constant omits its explicit type |
| `CV-CPP-BOUNDARY` | Error | An `import(cpp)` or `export(cpp)` function violates the supported declaration shape |
| `CV-CPP-CARRIER` | Error | A C++ boundary parameter or result has no supported scalar boundary type |
| `CV-CPP-IDENTIFIER` | Error | A C++ API path or global provider name cannot be represented by generated C++ |
| `CV-CPP-API-PATH-COLLISION` | Error | A function and namespace require the same prefix in the public C++ API tree |
| `CV-EFFECT-CATCH-ALTERNATIVE-UNREACHABLE` | Warning | A catch alternative cannot match a remaining protected failure |
| `CV-EFFECT-CATCH-ARM-UNREACHABLE` | Warning | A catch arm cannot match a remaining protected failure |
| `CV-EFFECT-CATCH-NON-EXHAUSTIVE` | Error | A catch leaves a protected failure unhandled |
| `CV-EFFECT-THROW-PUBLISHED` | Error | An entry or published callable has failures without an explicit `throw` contract |
| `CV-FLOW-MISSING-RETURN` | Error | A reachable path of a value-returning callable omits its result |
| `CV-FLOW-TRANSFER-VALUE-BRANCH` | Error | `return`, `break`, or `continue` crosses a value-control boundary |
| `CV-FLOW-UNREACHABLE-MATCH-ARM` | Warning | A match arm pattern is fully covered by preceding unguarded arms; the primary location is that pattern span |
| `CV-LAMBDA-CAPTURE-UNUSED` | Warning | An explicit lambda capture is unused |
| `CV-LINT-UNUSED-IMPORT` | Warning | An import selects no uniquely referenced binding |
| `CV-LINT-UNUSED-LOCAL` | Warning | A named local binding is unused |
| `CV-LINT-UNUSED-PARAMETER` | Warning | A named parameter is unused |
| `CV-MATCH-DUPLICATE-ALTERNATIVE` | Error | An or-pattern contains a repeated or subsumed alternative |
| `CV-TEST-ARGUMENT-COUNT` | Error | A contextual test operation has the wrong argument count |
| `CV-TEST-CONDITION-TYPE` | Error | A `check` or `require` condition is not exactly `bool` |
| `CV-TEST-MESSAGE-TYPE` | Error | A test message is not exactly `str` |
| `CV-TYPE-VISIBILITY-LEAK` | Error | A declaration surface exposes a narrower nominal identity |

The table is a partial lookup for current diagnostics. Human-readable messages,
notes, formatting, colors, and incidental ordering are presentation.
Warnings do not make an otherwise valid program fail.

For unused diagnostics, a reachable reference counts as a use and `_` is never
an unused candidate. A list import is used when any selected binding is
uniquely referenced.
