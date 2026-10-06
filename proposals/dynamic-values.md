# Dynamic values and contracts

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Runtime values whose concrete type is hidden behind a checked contract
- **Depends on:** Ordinary value and access rules; generic dynamic operations also
  require checked polymorphic calls and finite generic instances

## Current boundary

Ordinary classes encapsulate a known nominal value. Their operations are checked
as ordinary calls with Read, Write, or Take receivers. The language has no erased
value, dynamic conformance declaration, or dynamic dispatch operation.

A dynamic value needs a contract for available operations and a holding form for
its concrete value. The holding form determines ownership, borrowing, allocation,
nullability, and lifetime. Static generic evidence is selected during compilation.
A dynamic value would need runtime dispatch information. Conversion between them
needs an explicit cost and lifetime contract.

A callback, heterogeneous collection, or service API can establish the required
operations and value lifetime. `class(form)` and `dyn Contract` are syntax
candidates.

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

## Deferred work

### DEFER-01 — Existential containers and polymorphic elimination

- **Reason deferred:** Polymorphic elimination adds hidden-type call and result
  contracts beyond fixed-operation dispatch.
- **Depends on:** The holding and lifetime contracts of the selected consumer;
  checked polymorphic calls and a finite instance contract from Generics
- **Reactivation condition:** A concrete API must apply external algorithms to a
  container whose element type is hidden, and ordinary fixed-operation dispatch
  cannot express the requirement.

A candidate package is `exists T. (Evidence<T>, Payload<T>)`. Elements within one
package may share the same hidden `T`; two independent packages do not establish
`T = U`. Cross-package comparison therefore needs a common package, explicit type
equality evidence, or a defined heterogeneous policy. A shared operation table
does not establish hidden-type equality. Each erased call must satisfy its
checked signature.

Elimination through a polymorphic callback requires the callback to work for
every admitted hidden `T`. Its result cannot freely expose `T`; a result depending
on it needs repackaging or an explicit type relation. Borrowed and owned holding,
mutable access, callback lifetime, aliasing, and cleanup still need ordinary
contracts. Runtime elimination also needs a checked polymorphic callable and
value representation.

A closed compilation could evaluate a finite matrix of known types and algorithms
or type-correct thunks. Open plugins or externally introduced algorithms require
their own ABI and distribution contract. Select a representation only after
defining higher-order calls, result packaging, and the complete instance closure.

## Validation

A selected slice needs accepted and rejected source programs for conformance,
construction, access, borrowing, transfer, failures, and cleanup, plus compiled
C++ execution for the chosen holding form. Validate observable conformance,
ownership, lifetime, and completion behavior.

The existential candidate additionally needs same-package and independent-package
examples, accepted polymorphic elimination, rejected hidden-type escapes, explicit
result repackaging, and cross-package comparison under the selected policy. Check
borrowed and owned callback lifetimes and the finite closed dispatch graph before
testing a C++ representation.

## References

- [Ordinary aggregates](../docs/language/aggregates.md#ordinary-value-classes):
  current value, access, and lifetime rules.
- [Generics](generics.md): checked polymorphic calls, static evidence, and finite
  instances required by generic elimination.
- [Mitchell and Plotkin, Abstract Types Have Existential Type](https://www.cs.cmu.edu/~crary/819-f09/MitchellPlotkin88.pdf):
  existential packaging, hidden type identity, and polymorphic elimination.
