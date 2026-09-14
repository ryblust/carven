# Failure Value

- **Status:** Deferred — reactivate when a concrete ownership form and failure
  use case are available
- **Implementation:** Not started
- **Scope:** Extending typed failure contracts beyond copyable nominal values
- **Depends on:** Concrete ownership forms and use cases

## Summary

Carven currently limits failure values to copyable nominal structures and
enums. These can already contain owning String payloads and borrowed text
views; ownership alone does not put a payload outside the current contract.
This proposal explores whether future move-only or managed values beyond that
copyable nominal category should also participate in failure contracts.

Failure contracts remain closed sets carried through `throw`, postfix `?`,
`try`/`catch`, and `rethrow`. The question is which additional values those
structured effects may carry and how ownership transfers. Select a concrete
use case after its underlying ownership semantics are defined.

## Context

A richer failure payload requires source rules for consumption by `throw`,
handler ownership, continued matching after a rejected guard, and preservation
by `rethrow`. Extending the private C++ `Outcome` carrier follows those rules.

## Direction

Any extension should preserve the existing static failure contract and make
ownership explicit in source semantics before choosing a runtime shape.

The first candidate should be the smallest ownership category justified by a
real API. Move-only values, new owning-reference forms, and managed references need not
share one design or ship together.

Async completion and public C++ failure mapping have separate contracts and
remain outside this scope.

## Open decisions

**Next discussion:** `OPEN-01`

### OPEN-01 — Which richer failure value should be supported first?

- **Status:** Blocked
- **Blocked by:** A settled ownership form and a concrete API that needs it
- **Question:** The value category determines whether propagation copies,
  moves, owns, borrows, shares, or erases identity.
- **Options:** Unknown until a concrete use case exists
- **Closure condition:** Select one value category and describe an end-to-end
  `throw` → `?` → `catch` → `rethrow` example with unambiguous ownership.

### OPEN-02 — What does a handler own?

- **Status:** Blocked
- **Depends on:** `OPEN-01`
- **Question:** Define payload copying, consumption, and destruction for pattern
  bindings, false guards, handler-produced failure, and `rethrow`.
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
