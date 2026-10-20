# Dynamic values and contracts

- **Status:** Exploration
- **Scope:** Runtime values whose concrete type is hidden behind a checked contract

## Summary

A dynamic value needs a contract for available operations and a holding form for
its concrete value. The holding form determines ownership, borrowing, allocation,
nullability, and lifetime. Static generic evidence is selected during compilation.
For the proposed dynamic value, define runtime dispatch information. Specify
conversion costs and lifetimes when a selected API converts between the two.

A callback, heterogeneous collection, or service API can establish the required
operations and value lifetime. `class(form)` and `dyn Contract` are syntax
candidates.

## Context

Ordinary classes encapsulate a known nominal value. Their operations are checked
as ordinary calls with Read, Write, or Take receivers. Callable views
provide borrowed indirect calls through a fixed checked signature. The language
has no general nominal erased-value or dynamic-conformance facility.

Owning closures and builtin `Operation<T, throw E>` have fixed call or completion
contracts. General nominal conformance, existential packaging, and dynamic
operation tables remain unimplemented.

The hosting API's `HostValue` is an owning C++ data boundary for admitted
source values. Typed service registrations and native object references supply
fixed call contracts and object identity. They introduce no source-level erased
type or nominal dynamic conformance.

## Design

### Ownership and first consumer

Select a fixed-operation holding contract from a callback, heterogeneous
collection, or service consumer. Compare existing callable/service boundaries
with a nominal erased value using that consumer's required operations and
extension model. User-defined implementation types and domain operations are
an independent reason for a source interface; a fixed builtin boundary need not
select general existential elimination.

Compiler knowledge checks conformance, receiver access, hidden-value lifetime,
failures, and conversions. A builtin holding type or operation may provide the
bounded contract. Runtime Craft owns shared native storage, dispatch, and cleanup
mechanisms; realization selects carriers and thunks from the published contract.
Standard Craft and user modules own domain contracts, adapters, and algorithms.
Select borrowed, owned, or shared holding from the
consumer, with its representation and costs.

The first consumer selects its required holding and dispatch forms. Generic
operations, polymorphic elimination, and open-plugin ABI require their own
consumers and closure rules; none is a prerequisite for fixed-operation dispatch.

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
  checked polymorphic calls and finite formation of their concrete instances
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

- [Mitchell and Plotkin, Abstract Types Have Existential Type](https://www.cs.cmu.edu/~crary/819-f09/MitchellPlotkin88.pdf):
  existential packaging, hidden type identity, and polymorphic elimination.
