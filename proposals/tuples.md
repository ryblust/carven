# Builtin heterogeneous tuples

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Ordered builtin products, construction, element access, decomposition, and ownership

## Summary

This proposal defines the open rules for a builtin ordered heterogeneous product:
type identity, construction, element access, decomposition, ownership, and cleanup.
Its concrete consumer is the async proposal's selected `when_all` result design, which
returns an argument-ordered tuple from successful `when_all`.

The general product and multi-element `(a, b, c)` form are established by that
consumer. Arity rules, complete grammar, access, native eligibility, and static
execution remain open. A builtin product can use a finite list of resolved
element types; generic tuple algorithms are a separate scope.

## Current boundary and consumer

The current grammar defines parentheses as expression grouping and has no tuple
type, expression, or pattern production. The aggregate model includes nominal
records, homogeneous arrays, and enums. A struct has declaration identity and
named fields; a tuple would select elements by position and need its own type
identity.

Async `when_all` requires results in argument order, independently of child
completion order:

```text
Operation<A, E1> + Operation<B, E2>
    -> Operation<(A, B), E1 | E2>
```

The async owner defines child execution, completion arbitration, closure, and
result delivery. The product defines how delivered values are held and decomposed.

## Candidate contracts

### Type identity and arity

One candidate uses a canonical ordered list of element types. Order and arity
participate in identity: `(i32, str)` differs from `(str, i32)`. Use resolved
element identities to identify the product independently of target layout.

Define the admitted element types explicitly, including owning text, stored
borrows, ordinary classes, pointers, and native values. Derived equality, default
construction, formatting, and conversion each need an element-eligibility rule.

Empty and single-element products remain open. An empty tuple could be a distinct
unit value, be related to an existing no-value result, or be excluded initially.
A single-element form needs an unambiguous spelling distinct from `(value)`
grouping. Define the result shape of zero- and one-child combinators consistently
with the selected arity rules.

### Construction and completion

The following illustrates the multi-element surface; type annotations and the
complete construction grammar remain subject to `OPEN-01`:

```carven
let pair = (3, "three");
let (count, label) = pair;
```

Construction evaluates supplied expressions once in source order. Expected
element types, conversion, and Copy/Take admission need explicit contracts.
Preserve source observation order and ordinary backing lifetimes across the
selected target representation.

If an element fails, later expressions do not execute. Account for every earlier
completed element, its backing, and the failed element's partial work before
propagating the existing typed failure. Define the cleanup order and which owner
holds each completed value. A tuple result is delivered only after all elements
succeed.

### Element access and decomposition

A statically selected position could use a numeric projection, a constant-index
form, or a compiler-known operation. Define bounds diagnostics and whether the
selector is syntax or an evaluated operand. Dynamic indexing of heterogeneous
elements needs a common result or an explicit sum contract; it is outside the
initial positional-selection candidate.

Binding decomposition must evaluate its source once, check arity, and define
named bindings and ignored positions. Selection of an element and decomposition
of a complete product are distinct operations. Nested patterns, match patterns,
and catch patterns need admission decisions rather than being inferred from
the binding example. Specify each binding's access and backing before lowering.

Read selection may copy a snapshot or borrow selected storage according to the
element's source contract. Write selection preserves an existing updatable place
and does not make Write exclusive. Decide whether decomposition can introduce
borrowed Write bindings, and how mutation affects concurrent loans and retained
Read observations. Copying a tuple or decomposing Read into owning bindings must
not silently copy elements whose contracts do not admit it.

A consuming-decomposition candidate transfers element owners in one operation,
makes the original owner unavailable, and defines the disposition of every unused
element. There is no usable partially moved tuple. The design must state transfer
order, when unused elements are destroyed, and cleanup when a later conversion
or binding fails. It must also reject transfers that leave returned borrows
dependent on discarded tuple storage or an element owner whose lifetime ended.
Distinguish external backing from backing in another transferred element. The
latter needs a valid relationship to the destination owner or must be rejected.

Existing single-field owning projection retains the rest of its source for
cleanup. Whole-product consumption needs a disposition rule for every position.

### Representation and execution modes

Published semantics need element identities, access modes, result delivery, loans,
and whole-owner disposition. Lowering may use concrete C++20 aggregates, specialized
result storage, `std::tuple`, or equivalent scalarized delivery.

Preserve each admitted native type's construction and destruction capabilities.
Direct delivery of an immovable prvalue differs from transferring a component
already completed in separate storage across a failure barrier. Define admission
or rejection for each path using the native type's actual copying and movement
capabilities.

Static execution and freezing require their own admitted element and backing
rules for class operations, native values, and retained pointers. Current freezing
rejects non-null execution-local pointers. Preserve element
types and order, including String fields; String-to-str conversion applies at its
defined root boundary. Check evaluator admission and C++20 static-initialization
eligibility separately.

Tuple-valued calls use ordinary failure propagation. Tuple payloads remain outside
the current nominal failure category. Async owns cancellation and sibling outcomes.

## Open decisions

### OPEN-01 — Define identity, arity, and source forms

- **Status:** Active
- **Question:** Which element types and arities are admitted, and how are tuple
  types, construction, positional selection, and decomposition spelled?
- **Constraints:** Preserve `when_all`'s general ordered product and existing
  grouping; distinguish nominal records and homogeneous arrays.
- **Closure condition:** Accept and reject concrete multi-element, empty,
  single-element, nested, annotated, and out-of-range examples. Record any
  deliberately excluded forms and their consequences for the async consumer.

### OPEN-02 — Define access and complete-owner disposition

- **Status:** Active
- **Question:** Which selections and decompositions copy, borrow, mutate, or
  consume, and who owns completed and ignored elements on each exit?
- **Constraints:** Preserve source observation, nonexclusive Write, loan validity,
  exactly-once source evaluation, and the original owner's availability rules.
- **Closure condition:** Define copy eligibility, whole-product Take, unused
  element cleanup, and binding/conversion failure using owned and borrowed values.
  Final source examples use the forms selected in `OPEN-01`.

### OPEN-03 — Select native and static-execution admission

- **Status:** Blocked
- **Depends on:** `OPEN-01`, `OPEN-02`
- **Activation condition:** Element identity, construction, and lifetime contracts
  are defined for a selected tuple slice.
- **Question:** Which native constructor capabilities and constant operations
  does that slice admit, and which results may be frozen?
- **Closure condition:** State explicit rejected categories and demonstrate
  C++20 construction, partial cleanup, and any selected static-result path.
  Specify whether the selected scope includes constant execution.

## Validation

Use heterogeneous results with different ownership and lifetime requirements,
including String owners and values borrowing external backing. Check identity,
arity, nested products, grouping ambiguity, exact evaluation order, and one-time
selection/decomposition. Reject unavailable owners, illegal copies, escaping
borrows, out-of-range positions, and unsupported source or element forms.

Observe element construction and destruction across successful construction,
failure at each position, propagated failure, return, ignored positions, and
consumption. Check that original owners cannot be reused after Take and that
unused elements are neither leaked nor destroyed twice. Compile and run admitted
nontrivial, non-default-constructible, and immovable native cases; distinguish
direct construction from later transfer without fixing incidental C++ spelling.

For any static slice, test operation admission, budgets, freezing eligibility,
element type preservation, and cross-module backing separately. The async
consumer additionally tests completion order differing from argument order,
non-value outcomes, cancellation, and child closure before outward delivery.
Deliver source rules, semantic publication checks, lowering, diagnostics, and
permanent aggregate documentation together for the selected slice.

## References

- [Grammar](../docs/language/grammar.md) and [aggregates](../docs/language/aggregates.md): current source forms.
- [Async fixed composition](async.md#fixed-composition): ordered heterogeneous results.
- [Uninitialized storage](uninitialized-storage.md): construction destinations.
- [Constant storage](constant-storage.md): storage admission and retained results.
