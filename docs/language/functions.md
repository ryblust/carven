# Functions and callable values

[Language](README.md)

This page defines ordinary calls, closures, and callable views. Required
compile-time calls additionally follow [Compile-time function execution](constants.md#compile-time-function-execution).

## Functions and calls

Every ordinary function parameter requires an explicit type. A function with a
body may omit its result type. Block and expression bodies infer it from return
operands, without using the caller's expected type. Each operand is typed
independently; inferred result types must agree, including nested callable failure
contracts. Returns that cannot complete normally contribute no result type.
Native returns must have the same Carven type identity; an explicit result type
is required when determining compatibility needs C++ type resolution.
Return order does not supply type context to later operands. An
explicit result annotation supplies the expected type to every return operand.
A body without return operands infers `void`; bare returns also require `void`.
Normal completion of a value-returning body requires a return on every path.
Result-inference dependency cycles require an explicit result type to break the
cycle. A declaration without a body defaults to `void` when no result type is written. Parameter names must be
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
explicit `-> T` to break the signature dependency. Result completion uses
declaration signatures and body inference; operator constraints, constant
branches, and caller context do not resolve a signature cycle.

## Lambdas and callable views

### Creation and captures

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

### Closure identity, copies, and aliases

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

### Signatures and views

A lambda parameter may omit its type only when an expected callable view
supplies the parameter type at that position. Without such an expected view,
every parameter requires an explicit type. An explicit lambda result type fixes
the result. Otherwise an expected callable view supplies it; with no expected
view, return operands independently determine one consistent result, including
`void`, using the same rules as ordinary functions. The inferred or
expected signature is checked against the body before the closure type is completed.
These rules apply equally to block and expression bodies.

Source `fn(...) -> R throw E + F` denotes a non-owning callable view. Parameter
access, parameter types, and success result match exactly. A source callable
may have a smaller failure set than the expected view. Array adaptation applies
these rules recursively to element types, including zero-length arrays. Failure
contracts inside parameter and success-result types remain identical. Direct closure calls
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
does not copy its captures or acquire ownership. Copying a view of the same type
copies its target description and preserves its backing requirement. While the view's
loan remains active, its closure owner cannot be taken. Ending an inner scope
containing the borrowers permits a later Take of the still-live owner.
Assigning a capturing target through a Write parameter of callable-view type
is rejected; the parameter does not establish a sufficient backing lifetime.

Widening an existing view to a larger failure set borrows that view's storage.
The source view must remain alive, and rebinding it changes the target observed
through the wider view. The same rule applies to element-wise array widening.
Adapting a concrete callable directly to the wider contract uses that callable
as its target.

For example, given functions `first` and `second` with the same signature:

```carven
var source: fn(i32) -> i32 throw E = first;
let copied = source;
let widened: fn(i32) -> i32 throw E + F = source;
source = second;
```

`copied` calls `first`; `widened` calls `second`. The widened view invokes the
source view and adapts its result to the wider failure contract. It retains a
borrow of the source view's storage; both views preserve the backing lifetime
requirements of their targets.

### Invocation and snapshots

Calling a concrete closure first selects its object identity, then evaluates
arguments, then runs the body using that object's current captures. Assigning
new contents to the same closure object during argument evaluation therefore
affects this invocation. Calling a view first saves its target description:
reassigning the view variable during argument evaluation affects later calls,
but replacing contents of the already-selected closure object affects this
call. Neither form creates an implicit capture snapshot.

For example, the following uses one closure type produced by `factory`:

```carven
let factory = [](value: i32) => [value](_: i32) => value;
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
