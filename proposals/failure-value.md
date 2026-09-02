# Failure Value

- **Status:** Deferred — reactivate when a concrete ownership form and failure
  use case are available
- **Implementation:** Not started
- **Scope:** Extending typed failure contracts beyond copyable nominal values
- **Depends on:** Concrete ownership forms and use cases

## Summary

Carven currently limits failure values to copyable nominal structures and
enums. This proposal explores whether future move-only, owning, or managed
values should also participate in failure contracts.

The existing typed failure model is not under reconsideration. Failure
contracts remain closed sets, and `throw`, postfix `?`, `try`/`catch`, and
`rethrow` remain structured control effects. The open question is what kinds of
values those effects may carry and what ownership transfer means for them.

No richer value category is proposed for acceptance yet. The next step is to
choose one concrete use case after its underlying ownership semantics exist.

## Context

The implemented behavior lives in the permanent
[failure-contract semantics](../docs/semantics.md#failure-contracts),
[compiler architecture](../docs/compiler.md), and
[backend contract](../docs/backend.md). This document does not duplicate them.

Supporting a new C++ alternative inside the private `Outcome` carrier would be
mechanically simple, but it would not answer the source questions: whether
`throw` consumes a value, what a handler owns, whether a rejected guard may
continue matching, or how `rethrow` preserves the original failure.

## Direction

Any extension should preserve the existing static failure contract and make
ownership explicit in source semantics before choosing a runtime shape.

The first candidate should be the smallest ownership category justified by a
real API. Move-only values, owning references, and managed references need not
share one design or ship together.

Async completion is outside this proposal and remains owned by
[async.md](async.md). Public C++ failure mapping is outside this proposal and
must extend the current
[C++ interoperation contract](../docs/semantics.md#c-interoperation).

## Open decisions

**Next discussion:** `OPEN-01`

### OPEN-01 — Which richer failure value should be supported first?

- **Status:** Blocked
- **Blocked by:** A settled ownership form and a concrete API that needs it
- **Why it matters:** The value category determines whether propagation copies,
  moves, owns, borrows, shares, or erases identity.
- **Options:** Unknown until a concrete use case exists
- **Closure condition:** Select one value category and describe an end-to-end
  `throw` → `?` → `catch` → `rethrow` example with unambiguous ownership.

### OPEN-02 — What does a handler own?

- **Status:** Blocked
- **Depends on:** `OPEN-01`
- **Why it matters:** Pattern bindings, false guards, handler-produced failure,
  and `rethrow` must not copy, consume, or destroy the payload accidentally.
- **Options:** Determined by the selected value category
- **Closure condition:** Define binding lifetime and disposition on every
  handler exit without relying on the private C++ representation.

## Implementation

Implementation starts only after one value category has an accepted source
contract. Runtime carrier changes follow that decision; they do not define it.

## Validation

An accepted design needs source examples for construction, propagation,
matching, guard rejection, handler-produced failure, and `rethrow`, plus
rejected examples that expose invalid ownership or lifetime.

## References

- [Failure-contract semantics](../docs/semantics.md#failure-contracts)
- [Classes and ownership proposal](classes.md)
- [Async proposal](async.md)
- [C++ interoperation semantics](../docs/semantics.md#c-interoperation)
