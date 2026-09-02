# Semantics

This document defines the observable semantics of supported Carven
programs. It owns name resolution, types, values, evaluation, control flow,
semantic validity, the explicitly named stable diagnostic identities, and the
C++ interoperation contract. It does not define lexical or syntactic validity,
compiler representations, host filesystem policy, or generated C++ forms.

## Contents

- [Compilations, crafts, and modules](#compilations-crafts-and-modules)
- [Declarations and names](#declarations-and-names)
- [Module constants](#module-constants)
- [Types and compatibility](#types-and-compatibility)
- [C++ interoperation](#c-interoperation)
- [Bindings, access, and mutation](#bindings-access-and-mutation)
- [Functions and calls](#functions-and-calls)
- [Structures and arrays](#structures-and-arrays)
- [Lambdas and callable views](#lambdas-and-callable-views)
- [Control flow and loops](#control-flow-and-loops)
- [Failure contracts](#failure-contracts)
- [Numeric types and conversions](#numeric-types-and-conversions)
- [Operators and evaluation](#operators-and-evaluation)
- [Enums](#enums)
- [Unicode text](#unicode-text)
- [Patterns and matches](#patterns-and-matches)
- [Entry points and tests](#entry-points-and-tests)
- [Diagnostics](#diagnostics)

## Compilations, crafts, and modules

A compilation is one closed compiler boundary supplied by the driver or build
system. Its module catalog is exactly the explicit source-input batch;
compilation does not discover files while resolving imports. Every source input
has one canonical module path, a nonempty sequence of identifier components.
Path derivation from host filenames is driver policy rather than language
semantics.

A leading canonical path of `crafts.<name>.<path>` belongs to the module domain
anchored by `crafts.<name>`. All other canonical paths belong to the unprefixed
module domain. The reserved leading form requires both `<name>` and a nonempty
`<path>`; `crafts` and `crafts.<name>` alone are not complete module paths. A
non-leading `crafts` component is ordinary. The `crafts.<name>` prefix gives
modules a craft-qualified domain and affects name resolution only; it does not
create another compilation boundary.

Imports form a prefix at the start of a module. Module imports use one of three
structured references:

- `model.user` starts at the importing module's domain root;
- `.value` starts in the importing module's logical module directory;
- `json::parser` names `crafts.json.parser` directly.

A module's logical directory is its canonical path without the final module
name; removing that name does not change the module domain. The reference
components after `.` are appended to that directory. For example, `.value` in
`a.b.main` denotes `a.b.value`, and `.value` in `crafts.json.a.b.main` denotes
`crafts.json.a.b.value`.

Both unprefixed and leading-`.` references stay inside the importer's module
domain. A craft-qualified reference selects its named craft domain. Each
resolved path must exactly match one module in the closed module catalog.
Resolution has no fallback root, does not search the filesystem, and does not
discover sources implicitly. A module cannot import itself. No module-reference
form resolves from a craft domain into the unprefixed domain. `std::path` follows
the same craft-qualified rule and denotes `crafts.std.path` when that module is
supplied by the compilation producer.

Cross-module selection always requires an explicit import. A selected name
must be visible to the importer under the declaration rules below. Explicit
selections cannot collide with a local declaration or bind one name to different
symbols, and they deterministically shadow same-name wildcard candidates.
Multiple wildcard providers are ambiguous only when an actual use requires that
name. Import declarations have no runtime side effects. A declaration is used
when one of its selected bindings uniquely resolves an actual reference; an
otherwise unused declaration produces one `CV-LINT-UNUSED-IMPORT` warning.

The same prefix may contain C++ header dependencies. Their behavior is defined
under [C++ interoperation](#c-interoperation); they are not module references
and do not participate in Carven lookup.

## Declarations and names

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
| `private` | Its defining module |
| bare | Every module in its defining module domain |
| `export` | The complete compilation |

Every declaration is visible in its defining module. A cross-module use still
requires an explicit import even when the importer belongs to the declaration's
audience. The same rules apply in the unprefixed domain and every craft domain.
Qualified lookup selects the named module or nominal owner and does not fall
back to an unrelated unqualified declaration.

A declaration surface may refer only to nominal declarations whose audience
contains the surface's audience. This structural check recursively covers
function parameters, results, failure sets, nested callable types, structure
fields, enum payloads and underlying types, arrays, and a module constant's
type and normalized semantic value. Function bodies and constant-evaluation
proofs are implementation; identities eliminated by normalization do not enter
the published surface. A violation uses `CV-TYPE-VISIBILITY-LEAK`.

## Module constants

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

## Types and compatibility

The source-spellable builtin types are:

```text
bool  char  str  void
i8 i16 i32 i64 isize
u8 u16 u32 u64 usize
f32 f64
```

Structures and enums are nominal: identity comes from the declaration, not
from structural similarity. Arrays are identified by both element type and
extent. Function-view types include parameter access, parameter types, success
result, and failure set.

Ordinary compatibility requires the same canonical type. The defined
exceptions are contextual numeric literals and compatible callable-to-view
adoption. There are no general implicit numeric promotions, structural
conversions, truthiness conversions, or opaque dynamically typed values.

`void` is the absence of a value.

## C++ interoperation

The grammar defines four C++ forms. `import <...>` and `import "..."` are C++
header imports, top-level `#[cpp]` contributes an implementation source
fragment, `import(cpp)` declares a C++-implemented Carven function, and
`export(cpp)` publishes a Carven function to C++ consumers. A source fragment
has no public API placement.

Header imports create no Carven names, resolve no files, expose no external C++
symbols to semantic analysis, and link no library. Header search is the
downstream preprocessor's responsibility.

Each top-level `#[cpp]` payload remains an independent, byte-opaque C++ source
fragment. Carven does not parse or type-check it, interpolate Carven values, or
bind same-spelled Carven names. C++ owns macros, overloads, templates, linkage,
exceptions, lifetime, ODR, and undefined behavior inside each fragment.
Generated placement and preservation requirements belong to
[compatibility.md](compatibility.md#c-interoperation-artifacts).

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

`str`, arrays, structures, enums, callables, process arguments, and iteration
views are unsupported. An inbound `char32_t` is validated before it becomes a
Carven `char`; violation terminates with
`carven runtime contract error: invalid Unicode scalar at C++ boundary`.
Generated import bridges and export façades are `noexcept`. Carven does not
catch a provider exception or map it to a Carven failure; an exception escaping
an import bridge terminates under ordinary C++ rules.

Module components and public function names must be supported C++ identifiers.
A function/namespace prefix collision anywhere in the compilation's public API
tree is invalid. Public header paths, namespaces, declarations, and stability
belong to [compatibility.md](compatibility.md#c-interoperation-artifacts).

## Bindings, access, and mutation

Carven separates value type, binding role, and access mode. `&` and `&&` are
access markers; they are neither part of a Carven type nor source-level C++
reference types.

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
function body's execution order.

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
assignment requires a compatible value. Compound `+`, `-`, `*`, and `/`
assignment require numeric operands; `%`, bitwise, and shift assignment require
integer operands. Increment and decrement require an integer target. Assignment
evaluates the target before the new value, once each.

One operation cannot both Take and otherwise access the same owner, including
through a callee or nested argument. An outer binding cannot be both a Write
capture source and a Take source in one callable. Value-capture snapshots are
independent after creation, but capture creation requires the source to be
available. Lambda captures and range bindings do not support Take access.

A `const` initializer must be proven by Carven; target acceptance is not enough.
Constant facts include scalar and string literals, numeric enum cases,
resolved local and module constants, grouping, supported casts, supported pure
unary and binary operations, constant `str.len()` and `str.is_empty()`, and
payload-case construction whose payloads are all constant. General calls,
ordinary aggregate construction, and control-flow expressions are not Carven
constant expressions. Local and module constant declarations are
compile-time-only; their later uses denote the selected normalized value without
creating a runtime binding or lambda capture.

## Functions and calls

Every ordinary function parameter requires an explicit type. Omitted result
syntax means `void`. Parameter names must be unique. A call requires the exact
arity, access marker, and compatible argument type declared by the callable.
The callee is evaluated first, then arguments are evaluated once from left to
right.

A `return` without a value is valid only for `void`; a value return is required
for every other ordinary result type and must be compatible with it. Every
reachable path of a non-`void` function or lambda must return a
value. Violation is identified by `CV-FLOW-MISSING-RETURN`; the explanatory
message is not part of the language contract.

Function declarations may refer to later declarations because signatures are
collected before bodies. Direct and mutual recursion are supported. Typed
failure contracts are described below and remain part of callable
compatibility.

## Structures and arrays

A structure is a nominal product with ordered, uniquely named fields. Field
access selects by name. Positional construction maps values to fields in
declaration order; named construction maps each initializer to its declared
field. Both forms must initialize every field exactly once. Duplicate, unknown,
missing, or extra initializers and incompatible field values are invalid. Empty
construction is valid only for a structure with no fields. Initializer
expressions run once in source order, including when named initializers are
written out of declaration order.

`T { ... }` constructs structures only. It is invalid for builtin, enum, and
function-view types; literal and cast expressions, enum-case construction, and
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

## Lambdas and callable views

Every lambda expression has a unique concrete closure type. Value captures own
immutable snapshots; Write captures alias mutable outer bindings. Free runtime
bindings require explicit capture, while module functions, types, enum cases,
and compile-time constants do not. Unused explicit captures produce
`CV-LAMBDA-CAPTURE-UNUSED`.

A lambda parameter may omit its type only when an expected callable view
supplies the parameter type at that position. Without such an expected view,
every parameter requires an explicit type. An explicit lambda result type fixes
the result. Otherwise an expected callable view supplies it; with no expected
view, the result is inferred from the lambda's reachable value returns. The
inferred or expected signature is checked against the body before the closure
type is completed.

Source `fn(...) -> R throw E + F` denotes a non-owning callable view. Parameter
access, parameter types, and success result match exactly. A source callable
may have a smaller failure set than the expected view. Direct closure calls
retain their concrete closure type.

A callable view may be a parameter or local value, including local aggregate
storage, but it cannot be stored in a structure or enum, returned from a
function or lambda, or captured by a lambda. These restrictions apply
recursively through arrays. A capturing lambda temporary may form a view only
as a direct call argument and remains valid for that call. A named capturing
closure may initialize a local view while its owner remains in an enclosing
scope.

## Control flow and loops

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

Arrays support Read and, for a mutable source, Write range bindings. `str.bytes`
and `str.chars` support Read bindings only. A range binding is scoped to the
loop and cannot be taken. Its name is not visible in its declared type or range
source; it is published only after those inputs complete and is then visible
throughout the loop body.

`break` and `continue` are valid only inside a loop. `return` targets the
current function or lambda. A value-form `if`, `match`, or `try` is a control
boundary: its result branches cannot return from an enclosing callable or
break/continue an enclosing loop, though a transfer may target a loop nested
within that branch. An invalid crossing uses
`CV-FLOW-TRANSFER-VALUE-BRANCH`.

## Failure contracts

Carven models recoverable failure as a typed control effect represented to
source users by callable failure contracts. Failure values are copyable nominal
structures or enums. A failure contract denotes one exact closed mathematical
set: member spelling order and declaration order are not observable. An
explicit `throw` clause is an upper bound on a callable body. Module-private
functions and lambdas that omit it infer the least fixed-point failure set
across forward calls, direct recursion, and mutual recursion. A published
function—bare or exported—with a nonempty actual set must state an explicit
`throw` contract; omitting it produces
`CV-EFFECT-THROW-PUBLISHED`. Entry functions and tests must handle every failure
and cannot expose a failure contract. Failure types in a published contract
also obey the declaration-audience closure.

A value with pending failures cannot be consumed where an ordinary completed
value is required. Postfix `?` consumes the pending failures of its operand at
that lexical position and transfers them to the nearest enclosing failure
target. The operand may be a call or a composite expression whose selected
evaluation path carries pending failures. Applying `?` to an infallible operand
is invalid. `throw` transfers the supplied failure and does not complete
normally.

`try` handles failures produced by its protected body. Catch arms may select a
failure type, wildcard, alternatives, payload patterns, and guards; they must
cover the protected body's actual failure set. Failures produced by a guard or
handler propagate to the enclosing failure target and are never caught again by
the same `try`. Alternatives in one arm form one or-pattern. The first matching
alternative establishes its bindings, then the arm guard runs once. A false
guard continues with the next arm. Catch-arm order remains observable even
though failure-set member order does not. A `try` around an infallible body is
valid and produces no diagnostic.

`rethrow` is valid only within a catch handler and transfers the caught failure
identity selected for that handler. Closure bodies are separate callable
boundaries: their failures and control transfers do not belong to evaluation of
the expression that creates the closure.

## Numeric types and conversions

The integer types are the fixed-width signed and unsigned types plus `isize`
and `usize`; the floating types are `f32` and `f64`. `char` is not numeric.
Unsuffixed integer literals default to `i32` and unsuffixed floating literals
default to `f64`. In an expected numeric context, an unsuffixed literal may
instead adopt a representable type from the same integer or floating family.

Numeric types are otherwise compatible only with the identical
canonical type. There is no implicit integer promotion, signedness conversion,
or integer-to-floating conversion.

`expression as Type` accepts exactly:

- identity conversion between the same canonical type;
- every integer-to-integer conversion;
- conversions in either direction between an integer and `bool`;
- an integer to `f32` or `f64`;
- `f32` to `f64`;
- a numeric enum value to any integer type.

It rejects narrowing `f64` to `f32`, floating-point to integer or `bool`,
`bool` to floating-point, integer to enum, conversions between `char` and a
numeric type, and non-identity `str` conversions.

A constant integer cast to an `N`-bit integer reduces the mathematical value
modulo `2^N`. An unsigned target is that residue; a signed target interprets the
same bits as an `N`-bit two's-complement value. Accepted constant casts remain
constant facts.

Constant arithmetic is checked. Literal range errors, overflow, division by
zero, and invalid shift counts are Carven diagnostics and are never evaluated
through undefined host arithmetic.

Runtime integer negate, add, subtract, multiply, and left shift wrap at the
Carven type width; compound assignment and increment/decrement inherit the same
rule. Signed `MIN / -1` returns `MIN`, with remainder zero. Division or remainder
by zero and a negative or out-of-width shift count terminate. Signed right shift
is arithmetic.

`isize` and `usize` are the signed and unsigned native-model integers. Their
width is fixed by the supported compilation data model and participates in the
ordinary integer rules above.

## Operators and evaluation

Logical negation requires `bool`; numeric negation requires a numeric operand;
bitwise complement requires an integer. Arithmetic operators require identical
numeric operands. Remainder, bitwise, and shift operators require integers.
Ordering requires identical numeric operands. Logical `&&` and `||` require
`bool` and short-circuit from left to right.

Equality requires compatible operands and an equality-capable type. It is
available for `bool`, `char`, integers, floating-point values, `str`, arrays
whose elements support equality, structures whose fields all support equality,
numeric enums, and payload enums whose payloads all support equality. Callable
types, process-entry arguments, and `str.bytes` / `str.chars` iteration views do
not support equality.

Payload enum values with different cases compare unequal. Values of the same
case compare payloads in position order with short-circuiting. Floating-point
equality follows IEEE `==`; `!=` is its negation.

All Carven expression evaluation is left to right and exactly once. This
includes callee before arguments, binary left before right, receiver before
index, assignment target before value, and initializer clauses in source order.
Short-circuiting operators and control expressions evaluate only the selected
operands or branches.

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

## Unicode text

`char` is one immutable, copyable Unicode scalar value. It supports equality,
inequality, and matching, but not numeric arithmetic, ordering, truthiness, or
numeric conversion.

`str` is an immutable, copyable, value-passed UTF-8 view. It has no owning
storage, `&str` type, or source lifetime syntax. Its ordinary safe backing is
static literal storage and copies of such views. C++ boundary support and
Unicode validation are defined under
[C++ interoperation](#c-interoperation).

Decoded string and character literal values are not Unicode-normalized.

The builtin text surface is closed:

- `text.len() -> usize` returns the UTF-8 byte count.
- `text.is_empty() -> bool` tests that byte count.
- `text.bytes` is a copyable read-only range of `u8` values.
- `text.chars` is a copyable read-only range that decodes `char` values.

`bytes` and `chars` are computed projections on `str`, not general properties.
Their view types cannot be spelled. They support Read range iteration and
inferred value bindings, but not Write iteration, indexing, slicing, or
construction. User structures may declare same-named fields because member
resolution depends on the receiver type.

## Patterns and matches

Both value and statement matches must be exhaustive. A value match also
requires compatible arm results. The subject is evaluated exactly once, and
the first matching arm is selected.

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

## Entry points and tests

At most one function named `main` may exist in a compilation. Its module path,
module domain, and declaration visibility do not affect entry selection. It
accepts no parameters or one untyped Read parameter representing command-line
arguments; ordinary function parameter rules apply elsewhere. `main` must
handle every failure. Normal completion produces process status zero; a
declared Carven result, if present, is not a process exit status.

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
including parentheses, whitespace, line breaks, and comments. `fail` has no
invented condition. Doctest's surrounding wording, colors, statistics, and
layout are not language contracts.

## Diagnostics

The following table is the stable public diagnostic catalog:

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
| `CV-EFFECT-THROW-PUBLISHED` | Error | A published callable has failures without an explicit `throw` contract |
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

The code, severity, and condition in this table are stable. Human-readable
messages, notes, formatting, colors, and incidental ordering may change.
Warnings do not make an otherwise valid program fail.

For unused diagnostics, a reachable reference counts as a use and `_` is never
an unused candidate. A list import is used when any selected binding is
uniquely referenced.
