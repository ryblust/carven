# Generic extensions and static capabilities

- **Status:** Draft
- **Scope:** Static capabilities, associated types, generic class operations, and finite boundary extensions

## Summary

Add static capabilities and associated types so generic bodies can require
checked operations on unknown types. The capability design is accepted; its
implementation scope and remaining decisions are open. Parameterized class
operations and generic C++ boundaries select their own delivery scopes.

## Context

Type and failure-set parameters on named functions and nominal declarations,
definition-site body checking, local call inference, fixed tuple substitution,
and first-order recursive-growth rejection are implemented. Static function
parameters and residual specialization keep parameter types, result types, and
layouts fixed. Value-dependent types and generic value arguments remain deferred.

Unknown type parameters admit forwarding and whole-value Take; additional
operations need capability evidence. Associated projections must extend finite
instance formation with a termination argument. Parameterized class operations
and generic C++ boundary declarations remain unsupported.

`concept`, `impl`, `Self`, and `where` are ordinary identifiers today.

## Design

### Unified static capability model

**Maturity:** Accepted semantics.

A generic body declares capabilities for operations beyond universal type rules:

```carven
concept Comparable {
    fn less(left: Self, right: Self) -> bool;
}

fn choose<T: Comparable>(&&left: T, &&right: T) -> T { ... }
```

Compiler-derived properties and source `impl` evidence share satisfaction,
diagnostics, and resolved-operation representation. The capability identity
defines which producers may supply evidence. Compiler-derived evidence can be
represented as a synthesized impl within this same model.

A `concept` is a compile-time contract with no runtime value representation or
subtyping. Runtime holding and dispatch require a separate value contract.

### Concept and impl declarations

**Maturity:** Accepted syntax and semantics.

A concept is a named module declaration identified by canonical module path and
declaration identity. `private concept`, bare `concept`, and `export concept`
have Module, ModuleDomain, and Compilation visibility respectively. Concepts
appear in bounds, impl heads, projections, and qualified operation selection.

Callable requirements end in `;`. Operation and associated-type names share one
namespace with unique names and no overloading. An empty body declares a marker
capability.

An `impl` is an anonymous semantic declaration with no source name, visibility
modifier, import, re-export, or local activation. A valid impl contributes
canonical evidence throughout the compilation. It must implement every
requirement exactly once; missing, extra, duplicate, and signature-mismatched
members are diagnosed after substitution.

`concept` and `impl` are contextual keywords recognized at top-level declaration
positions, including after `private` or `export`. They are not global keywords,
and parsing does not consult the symbol table.

### Function-shaped concept operations

**Maturity:** Accepted semantics.

Initial requirements are functions with explicit parameters:

```carven
concept Comparable {
    fn less(left: Self, right: Self) -> bool;
}

impl Comparable for Meter {
    fn less(left: Meter, right: Meter) -> bool { ... }
}

fn minimum<T: Comparable>(&&left: T, &&right: T) -> T {
    if Comparable::less(left, right) { return &&left; }
    return &&right;
}
```

Resolve qualified operations through the concept in generic and concrete contexts.
Keep impl operations outside member lookup, ADL, and free-function search. Match
parameter access, result, and failure contracts exactly after substituting `Self`.

### Contextual Self

**Maturity:** Accepted syntax and semantics.

`Self` denotes the implementing type inside a concept or its impl. Evidence
selects that type in a requirement; an impl normalizes `Self` to its target.
Signature conformance is checked after substitution.

`Self` is a contextual type placeholder, distinct from a value receiver `self`.
It introduces no nominal type, object address, pointer, or global keyword.

### Conjunctive bounds

**Maturity:** Accepted syntax and semantics.

Bounds follow the type parameter. `+` expresses finite static conjunction:

```carven
fn minimum<T: Equality + Comparable>(&&left: T, &&right: T) -> T { ... }

impl<T: Equality + Debug> Debug for Box<T> { ... }
```

Bound order has no semantic effect; duplicate capabilities are diagnosed.
Ordinary direct lookup resolves capability names. Inference determines the
substitution before checking each evidence obligation.

Conjunction creates no combined evidence identity, inheritance, subtyping, or
runtime composition. The initial scope excludes `where`, disjunction, negation,
refinement, and general constraint logic.

### Minimal associated types

**Maturity:** Accepted semantics and syntax.

An associated type is an output determined by canonical evidence:

```carven
concept Iterator {
    type Item;
}

impl<T> Iterator for VecIterator<T> {
    type Item = T;
}
```

Associated names are unique, and each impl supplies exactly one definition per
requirement. Definitions may use impl type parameters. Associated parameters,
defaults, bounds, constants, and general equations remain outside this phase.

After application substitution, projections normalize through unique evidence
before lowering. Cycles and failed normalization produce Carven diagnostics.
The external form is `Concept::Associated<SelfType>`. This abbreviated consumer
assumes an `Iterator::next` requirement and an `Option` declaration:

```carven
fn first<I: Iterator>(&iterator: I) -> Option<Iterator::Item<I>> {
    return Iterator::next(&iterator);
}
```

The proposed phase does not include `I::Item`, `<I as Concept>::Item`, or
`Concept<I>::Item` shorthand.

### Strong global coherence

**Maturity:** Accepted semantics.

Each resolved `(capability identity, concrete semantic arguments)` has at most
one applicable evidence in a closed compilation. Compiler-derived and source
evidence share this coherence domain.

Reject overlapping evidence even across modules or when no current application
uses it. Generic impls that may both match are also rejected. Imports do not
activate evidence, and the first phase has no specialization, priority, or
declaration-order tie-break. APIs needing multiple strategies use explicit
strategy values or types.

### Impl module domain and anchored head

**Maturity:** Accepted semantics.

An impl's module must share a module domain with either the capability declaration
or the normalized target's outermost nominal declaration. Ordinary direct lookup
must also permit naming the capability and type:

```carven
impl LocalCapability for ExternalType { ... } // capability domain: allowed
impl ExternalCapability for LocalType { ... } // nominal-head domain: allowed
impl ExternalCapability for ExternalType { ... } // neither: rejected
```

Normalize aliases before identifying the target head. Aliases introduce no head;
builtins, arrays, and functions have no source nominal head.

Initial generic impls require a concrete nominal head, with bounded parameters
permitted in its arguments:

```carven
impl<T: Debug> Debug for Box<T> { ... }
```

Bare-parameter blanket impls are rejected:

```carven
impl<T: Debug> Printable for T { ... }
```

Index candidates by capability and nominal head, then apply local argument
unification and bound validation. This bounds candidate lookup.

### Evidence and instance identity

Concrete generic nominal types and callable applications use this key:

```text
(generic declaration identity, normalized semantic arguments)
```

Declaration identity includes the canonical module path. Expand aliases and
normalize associated projections first: a projection resolving to `u8` makes
`Box<Projection>` the same instance as `Box<u8>`. The key retains each argument's
kind and declaration position.

Select evidence canonically during analysis and retain it as a semantic and
lowering dependency. Use normalized
declaration and argument identities across imports, aliases, generated names,
and target representations. Storage, interning, hashing, caches, emitted-text
deduplication, mangling, and artifact location remain implementation choices.

### Definition-site integration

A checked generic body may retain resolved requirement operations and symbolic
associated projections. Applications first infer ordinary type and failure-set
arguments, then validate bounds and select canonical evidence. They do not use
available impls to infer otherwise unknown arguments or reinterpret the body.

Define access, failure, and constant-execution admission in requirement signatures.
Require separate evidence for copying an unknown value. Close evidence
dependencies and normalized projections before concrete publication, including
applications demanded by static execution.

Cross-module evidence retains canonical module identity and ordinary visibility.
Lowering may share concrete declarations, use C++ templates, or erase distinctions
that no longer affect behavior. Validate each shared representation through its
typed calls, access, failure, and lifetime contracts. Equal storage size and
function-pointer casts do not establish typed-call compatibility.

## Decision record

The identifiers below retain accepted choices for unfinished work.

| ID | Decision and reason |
| --- | --- |
| GEN-05 | Compiler and source evidence share satisfaction, diagnostics, and resolved operations. |
| GEN-06 | Concepts are named contracts; anonymous impls provide compilation-wide canonical evidence. |
| GEN-07 | Explicit function-shaped requirements use concept-qualified lookup consistently. |
| GEN-08 | Contextual `Self` names the implementing type in requirements and impls. |
| GEN-09 | `+` combines bounds by finite static conjunction. |
| GEN-10 | Associated types normalize through unique evidence before lowering. |
| GEN-11 | Compilation-wide coherence gives each obligation at most one applicable evidence. |
| GEN-12 | Module-domain locality and anchored nominal heads bound impl lookup and overlap checking. |
| GEN-16 | Ordinary value classes share the parametric nominal model; operations inherit their owner's parameters and lexical access authority. |

## Open decisions

### OPEN-01 — Generic C++ boundary extensions

- **Status:** Blocked
- **Activation condition:** A concrete native provider or consumer needs a generic
  declaration rather than the implemented concrete wrapper boundary.

The current boundary excludes generic `import(cpp)` and `export(cpp)` declarations.
An extension could select explicit instance lists, compilation-derived instances,
or separately named provider forms. Every application and evidence dependency
must enter the closed semantic graph. C++ deduction, overload resolution, and
substitution failure cannot complete Carven inference or determine source validity.
Close the design with typed inbound and outbound examples, finite publication,
ownership, and rejected open applications.

### OPEN-02 — Finite capability and associated-type expansion

- **Status:** Active for the capability design
- **Depends on:** GEN-10, GEN-11, GEN-12

The implemented parameter-flow rule handles finite first-order constructors.
Define how evidence obligations and associated projections extend that proof.
Projection cycles, recursive impl bounds, and construction through a normalized
projection must have decidable admission and source diagnostics. Budget exhaustion
remains distinct from semantic infinite expansion. If OPEN-01 admits new external
entry points, those entries must obey the same closure rule.

Validate same-key recursion, finite permutations and concrete resets, recursive
associated projections, overlapping evidence, and growing obligations, including
unused definitions. Check finite source formation before C++ lowering.

### OPEN-04 — Parameterized class operations

- **Status:** Active
- **Depends on:** GEN-16

Class field substitution is implemented. Operations still need to inherit owner
parameters and lexical access authority while preserving receiver Read, Write,
Take, result backing, failures, and static admission. Select a concrete container
or wrapper with at least two element types. Establish definition-site legality,
method lookup, and finite call closure without requiring capability dispatch for
operations already justified by universal rules.

## Deferred work

### DEFER-01 — Const and value generic parameters

- **Reason deferred:** The selected core has type and failure-set parameters;
  value arguments add inference, identity, equality, and evaluation rules.
- **Depends on:** Implemented parametric generic core
- **Reactivation condition:** A concrete API requires a compile-time value that
  cannot remain an ordinary runtime argument or type-level distinction.

Implemented static function parameters fix values for ordinary calls while keeping
parameter types, result types, and layouts independent of those values. Their
instances are residual semantic bodies selected by normalized static values;
source types and callable signatures remain fixed. Generic value arguments and
value-dependent types remain deferred here.

### DEFER-02 — Generic lambdas

- **Reason deferred:** Generic closures add capture, callable identity,
  inference, and lifetime questions independently from named declarations.
- **Depends on:** Checked polymorphic calls and the selected callable and capture
  lifetime contract; owning storage only if the selected holding form requires it
- **Reactivation condition:** A real higher-order API requires a locally
  declared polymorphic callable.

### DEFER-03 — Blanket impl

- **Reason deferred:** An unanchored target turns every obligation into global
  rule search and introduces proof termination and non-local overlap.
- **Depends on:** Implemented canonical evidence and coherence
- **Reactivation condition:** A real library use case supplies finite
  candidate, overlap, termination, and evolution rules.

### DEFER-04 — Specialization and candidate ranking

- **Reason deferred:** Selecting among multiple applicable evidence requires
  ordering and overlap rules beyond strong coherence.
- **Depends on:** Canonical evidence, coherence, and candidate-selection rules
- **Reactivation condition:** A concrete API cannot use explicit strategy
  types and can define stable ordering and evolution behavior.

### DEFER-05 — Advanced associated items

- **Reason deferred:** Associated consts, defaults, bounds, and generic
  associated types exceed the first functional projection model.
- **Depends on:** Implemented minimal associated types
- **Reactivation condition:** A real capability requires one such item and can
  define normalization, conformance, and cycle behavior.

### DEFER-06 — Richer concept relationships and constraint logic

- **Reason deferred:** Default operations, refinement, disjunction, negation,
  and general equations exceed finite conjunctive bounds.
- **Depends on:** Implemented concepts and conjunctive bounds
- **Reactivation condition:** Repeated APIs expose a relationship the minimal
  model cannot state locally.

### DEFER-09 — Static capability to dynamic-interface bridging

- **Reason deferred:** The two models have different identity, representation,
  ownership, and dispatch costs.
- **Depends on:** Implemented static capabilities and dynamic interfaces
- **Reactivation condition:** Repeated APIs need the same contract in both
  worlds and can make conversion and cost explicit.

### DEFER-12 — Generic product traversal

- **Reason deferred:** Fixed tuple substitution and decomposition are implemented;
  no consumer currently establishes a general heterogeneous traversal contract.
- **Reactivation condition:** An ordinary source API needs computed positional
  selection or traversal beyond fixed product shapes.

Select the required operation before adding parameter packs, mapping, or expansion.
Define element typing, order, nesting, empty products and unit slots, inference,
access, whole-owner transfer, and a finite expansion rule. Dynamic heterogeneous
indexing additionally needs an explicit sum result. Builtin `when_all` already
forms ordered products from concrete operands and requires no such source feature.

Consuming decomposition must evaluate the complete source once and make its owner
unavailable. Define the disposition of every field, including unused fields and
borrowed backing, on success, failure, and rejected patterns. No usable partially
moved owner may remain.

## Implementation

Deliver the chosen capability identities, declarations, definition-site operations,
conformance, coherence, normalization, and finite closure as one usable slice.

Class operations and native generic boundaries select their own slices and need
only the capability features their contracts use.

## Validation

### Static capabilities

Validate missing and duplicate members, signature mismatch, module-domain locality,
anchored heads, overlapping impls, projection cycles, and canonical instance reuse.
Include static calls, failures, exactly-once operand evaluation, borrowed backing,
and Take across generic and concrete callers.

### Class operations and native boundaries

Test private arguments and cross-module dependencies through self-contained
generated interfaces and C++20 compile, link, and execution. Do not fix private
names or a concrete/template representation in the contract.

## References

- [Go parameter-flow checking](https://go.dev/src/cmd/compile/internal/types2/mono.go):
  a reference for the first-order recursive-growth boundary that extensions must preserve.
