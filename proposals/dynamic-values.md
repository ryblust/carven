# Dynamic values and contracts

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** Runtime values whose concrete type is hidden behind a checked contract
- **Depends on:** Ordinary [value and access rules](../docs/semantics.md#ordinary-value-classes);
  generic dynamic operations also depend on [Generics](generics.md)

## Current boundary

Ordinary classes encapsulate a known nominal value. Their operations are checked
as ordinary calls with Read, Write, or Take receivers. The language has no erased
value, dynamic conformance declaration, or dynamic dispatch operation.

A dynamic value needs a contract for available operations and a holding form for
its concrete value. The holding form determines ownership, borrowing, allocation,
nullability, and lifetime. Static generic evidence is selected during compilation.
A dynamic value would need runtime dispatch information. Conversion between them
needs an explicit cost and lifetime contract.

The first design should start from a callback, heterogeneous collection, service
API, or another concrete consumer. Its operations and value lifetime determine
the minimum source forms. `class(form)` and `dyn Contract` are syntax candidates,
not accepted language forms.

## Open decisions

### Contract and conformance

Define a contract's identity, operations, receiver access, result and failure
types, and how a concrete type proves conformance. Specify which members remain
accessible through a held value and how missing or mismatched operations are
diagnosed.

### Holding and type use

Define how a value is constructed, stored, passed, returned, copied, and taken.
Borrowed and owned forms require separate lifetime rules. Shared, nullable, and
inline storage need a consumer that establishes their behavior and cost. Type-use
syntax must make the selected holding form visible.

### Calls and C++ boundaries

Dynamic calls must evaluate each operand once and follow the declared access,
failure, and cleanup rules. Lowering can then choose a representation. C++ imports
and exports require explicit carrier, ownership, lifetime, and ABI contracts.

Generic dynamic operations remain outside the initial design until an API needs
them. That API must determine their finite call graph and C++ boundary before a
dispatch representation is selected.

## Validation

A selected slice needs accepted and rejected source programs for conformance,
construction, access, borrowing, transfer, failures, and cleanup, plus compiled
C++ execution for the chosen holding form. Tests should observe its stated
contract rather than a particular table, thunk, or proxy layout.
