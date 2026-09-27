# Failure contracts

[Language](README.md)

This page defines typed failures, propagation, recovery, and payload lifetime.
Native exceptions follow the separate [C++ exception boundary](interop.md#native-exception-boundary).

- [Sets and callable contracts](#sets-and-callable-contracts)
- [Propagation and evaluation](#propagation-and-evaluation)
- [Catch selection and remaining failures](#catch-selection-and-remaining-failures)
- [Rethrow and payload lifetime](#rethrow-and-payload-lifetime)

## Sets and callable contracts

Carven models recoverable failure as a typed control effect represented to
source users by callable failure contracts. Failure values are copyable nominal
structures or enums. A failure contract denotes a closed set of types; member
spelling order and declaration order do not affect it. An explicit clause may
name each failure type only once; `throw E + E` is invalid. An explicit `throw`
clause is an upper bound on a callable body. Module-private non-entry functions,
implicit entries, and lambdas that omit it infer the least fixed-point failure
set across forward calls, direct recursion, and mutual recursion. A published
function—bare or exported—with a nonempty actual set must state an explicit
`throw` contract;
omitting it produces `CV-EFFECT-THROW-PUBLISHED`. Explicit entry functions also
require an explicit `throw` contract for outward failures, regardless of
declaration visibility. Tests must handle every failure and cannot expose a
failure contract. Failure types in a published contract must be visible to that
contract's audience.

Calls use the callee's failure contract. An explicit contract determines the
call's failure set even when the callee's body produces fewer failures:
`fn source() throw E {}` still makes `source()` fallible with type `E`.

An immutable local initialized from a known function preserves that target
through copies and view adaptation. Calling it uses the function's contract,
including any explicit `throw` clause on the function. A wider view type does
not add failures to this known call. Calls through parameters, mutable views,
or unresolved target selections use the view's contract.

## Propagation and evaluation

A value with pending failures cannot be consumed where an ordinary completed
value is required. Postfix `?` consumes the pending failures of its operand at
that lexical position and transfers them to the nearest enclosing failure
target. The operand may be a call or a composite expression whose selected
evaluation path carries pending failures. Applying `?` to an infallible operand
is invalid. `throw` transfers the supplied failure and does not complete
normally.

The nonempty requirement for `?` is checked against the solved failure set.
For `private fn source() {}`, `source()?` is invalid because the inferred set
is empty; the call is written `source()`. This also applies when a body change
makes a previously fallible private function infallible.

Pending failures describe static expression composition. Evaluation stops at
the first failure: in `(first() + second())?`, a failure from `first()` skips
`second()` and the addition. Operands evaluate left to right, exactly once,
along the selected path.

Failure propagation does not roll back completed mutations or external effects.

Carven failures are independent of C++ exceptions. Native exceptions must be
handled in C++ before they escape a generated `noexcept` boundary to continue
execution.

## Catch selection and remaining failures

`try` handles failures produced by its protected body. Catch arms may select a
failure type, wildcard, alternatives, payload patterns, and guards. Remaining
failures transfer to the enclosing failure target. An enclosing protected body,
a lambda, a private non-entry function or implicit entry with inferred
failures, or a function with an explicit `throw` contract accepts this
transfer. At a test boundary or a published or explicit entry function
without an explicit contract, catch arms must
cover every protected failure. Outward failures remain subject to the enclosing
callable's contract.

For example, this handler consumes `A` and forwards `B`:

```carven
struct A {}
struct B {}
fn source() throw A + B {}

fn wrapper() throw B {
    try {
        source()?;
    } catch {
        A(_) => {},
    }
}
```

Adding `_ => rethrow,` after the `A` arm explicitly forwards the remaining
failure and gives the same outward set, `{B}`. A failure type remains in the
set unless the arms collectively cover all its values, accounting for guards
that may reject.

Failures produced by a guard or handler propagate to the enclosing failure
target and are never caught again by the same `try`. Alternatives in one arm
form one or-pattern. The first matching alternative establishes its bindings,
then the arm guard runs once. A false
guard continues with the next arm. Catch-arm order remains observable even
though failure-set member order does not. A non-exhaustive catch diagnostic
identifies each failure type that is not fully covered, including partial payload
patterns and guards that may reject. A `try` around an infallible body is valid
and produces no diagnostic.

## Rethrow and payload lifetime

`rethrow` is valid only within a catch handler and transfers the caught failure
identity selected for that handler. Closure bodies are separate callable
boundaries: their failures and control transfers do not belong to evaluation of
the expression that creates the closure.

Failure payloads and copied catch bindings follow the ordinary ownership rules.
The original payload retains its backing relationships through selection,
guards, and rethrow, independently of copied catch bindings. Borrowed storage
must remain alive while the failure payload refers to it.
