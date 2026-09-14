# Generics and static capabilities

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** Generic instances, static capabilities, coherence, and target-realization freedom
- **Depends on:** None for the accepted source semantics; generic
  `import(cpp)`/`export(cpp)` participation depends on the implemented concrete
  C++ boundary and a finite generic publication contract

## Summary

This proposal defines type parameters, definition-site checking, local inference,
static capabilities, associated types, coherence, and generic instance identity.
These semantics are accepted and unimplemented.

Two decisions block implementation: generic C++ boundary participation (`OPEN-01`)
and finite instance expansion (`OPEN-02`). Lowering may use concrete C++
declarations, templates, erasure, or a combination after semantic analysis.
Advanced facilities remain under `DEFER-01` through `DEFER-11`.

## Context

Carven currently compiles a closed batch of modules with concrete declarations,
canonical module paths, module-domain visibility, and direct lookup. It has no
user-defined generic declarations, concept requirements, impl evidence, or
generic instance graph. `concept`, `impl`, `Self`, and `where` are ordinary
identifiers today.

Named type arguments, builtin `ptr<T>`, and nested `>>` parsing already exist.
Target lowering also uses C++ templates for arrays, failures, and runtime helpers.
Source generics require additional syntax and semantic facts.

```text
generic declaration + normalized arguments + required static facts
                              |
                              v
                  one resolved Carven instance
```

Carven analysis resolves the meaning of names, calls, members, operators,
constraints, access, and failures. Capability constraints are needed when a body
uses operations beyond those available for every admitted type. The parametric
core can support storage, forwarding, and return of `T` independently.

## Goals and non-goals

The initial scope covers type-parameterized functions, transparent structs, and
enums; checked generic bodies; bounded local inference; canonical static
evidence; and a finite instance graph.

Value parameters, generic lambdas, type-level execution, dynamic interfaces,
reflection, binary-only distribution, stable ABI, and persistent caches are
outside this scope. Generics add no implicit allocation, runtime descriptors,
registries, initialization, or indirect dispatch.

## Design

### Definition-site contract

**Maturity:** Accepted semantics.

Check each generic body once against universal type rules and its declared
capabilities. Every operation has a resolved meaning even if the declaration is
unused; an invalid body is diagnosed at its definition.

Generic SemIRProgram may retain parameterized types, resolved requirement
operations, and symbolic associated projections. Application sites perform
inference, argument validation, constraint satisfaction, canonical evidence
selection, and instance formation. They do not reinterpret the body, and C++
substitution does not determine its validity.

### Parametric generic core

**Maturity:** Accepted semantics.

The first phase supports generic functions, transparent structs, and enums:

```carven
fn identity<T>(value: T) -> T {
    return value;
}

struct Box<T> {
    value: T,
}
```

These declarations obey ordinary access, copy/Take, storage-cycle, construction,
visibility, and published-audience rules. An unconstrained body may use only
operations valid for every admitted `T`. Knowing all current applications does
not validate an otherwise unsupported operation:

```carven
fn equal<T>(left: T, right: T) -> bool {
    return left == right;
}
```

If equality is not universal, this body requires an explicit capability.
`concept` and `impl` provide that additional layer; simple parametric declarations
such as `Box<T>` do not require capability dispatch.

### Type-generic syntax

**Maturity:** Accepted syntax and parsing contract.

Declarations and applications use `<...>` after the name:

```carven
fn wrap<T>(value: T) -> Box<T> { ... }
struct Box<T> { value: T }

let box: Box<i32> = ...;
let inferred = wrap(1);
let explicit = wrap<i32>(1);
```

Clauses are nonempty, comma-separated type lists with optional trailing commas.
Parameter names are unique. An explicit function application supplies all type
arguments; omitting the clause requests inference for all of them. Partial,
placeholder, named, and value arguments are outside the first phase.

Parsing uses tokens only. A complete `<...>` after a callee followed by `(...)`
forms a generic call; otherwise `<` enters comparison grammar. Semantic failure
does not trigger reparsing. Thus `a < b > (c)` is a generic call, while
`(a < b) > c` expresses the comparison.

### Local type argument inference

**Maturity:** Accepted semantics.

Infer a unique substitution from the public signature, actual arguments, and
immediate expected result type:

```carven
fn identity<T>(value: T) -> T { return value; }
fn make<T>() -> T { ... }

let first = identity(1);
let second: i64 = identity(1);
let third: i32 = make();
consume_i32(make());
```

Inference uses bounded local unification, including `T = i32`,
`Box<T> = Box<i32>`, conflicting bindings, and recursive substitutions. Expected
types may propagate through existing local expression contexts. Insufficient,
conflicting, or ambiguous information produces a call-site diagnostic requiring
explicit arguments.

The solver does not inspect bodies, existing instances, available impls, or C++
deduction. Capabilities validate an already inferred substitution. Programs that
need a fixed point across declarations, statements, call sites, or the instance
graph require annotations.

### Unified static capability model

**Maturity:** Accepted semantics.

A generic body declares capabilities for operations beyond universal type rules:

```carven
concept Comparable {
    fn less(left: Self, right: Self) -> bool;
}

fn choose<T: Comparable>(left: T, right: T) -> T { ... }
```

Compiler-derived properties and source `impl` evidence share satisfaction,
diagnostics, and resolved-operation representation. The capability identity
defines which producers may supply evidence. Compiler-derived evidence can be
represented as a synthesized impl within this same model.

A `concept` is a compile-time contract. It has no runtime value representation
and introduces neither subtyping nor a dynamic interface.

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

fn minimum<T: Comparable>(left: T, right: T) -> T {
    if Comparable::less(left, right) { ... }
}
```

The resolved concept is the lookup anchor for a qualified operation. Impl
operations do not enter member lookup, ADL, or free-function search. This keeps
selection consistent in generic and concrete contexts. Parameter access, result,
and failure contracts are ordinary callable facts, matched exactly after
substituting `Self`.

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
fn minimum<T: Equality + Comparable>(left: T, right: T) -> T { ... }

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
fn first<I: Iterator>(iterator: I) -> Option<Iterator::Item<I>> {
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

### Generic instance identity

**Maturity:** Accepted semantics.

Concrete generic nominal types and callable applications use this key:

```text
(generic declaration identity, normalized semantic arguments)
```

Declaration identity includes the canonical module path. Expand aliases and
normalize associated projections first: a projection resolving to `u8` makes
`Box<Projection>` the same instance as `Box<u8>`.

Canonical evidence is a semantic and lowering dependency, not a hidden
caller-selected identity argument. Import aliases, filesystem spelling,
generated names, and target representation do not change identity. Storage,
interning, hashing, caches, emitted-text deduplication, mangling, and artifact
location remain implementation choices.

### Visibility and source composition

**Maturity:** Accepted boundary.

Cross-module use requires the resolved signatures, constraints, body operations,
or equivalent semantic facts needed for checking and instance formation in the
same closed compilation. Their private storage and transport remain open.

Evidence and declaration identity use canonical module paths, module-domain
visibility, and direct lookup. Generated C++ and linking do not reconstruct
missing source facts.

### Target realization freedom

**Maturity:** Accepted lowering freedom; the first representation is selected
after `OPEN-01` and `OPEN-02`.

Lowering may emit concrete declarations, use C++ templates for checked instances,
erase distinctions that no longer affect operations or representation, or combine
these approaches to control compilation cost, code size, and dependencies.

Generic parameters, normalized arguments, constraints, evidence, and instance
selection are semantic inputs. They need no one-to-one runtime or emitted entity.
All choices preserve observable behavior and confer no additional overload,
specialization, metadata, dispatch, or ABI guarantees. C++ substitution cannot
decide whether a Carven call exists or which implementation it selects.

## Decision record

| ID | Decision and reason |
| --- | --- |
| `GEN-01` | Check bodies at definition site for caller-independent meaning and diagnostics. |
| `GEN-02` | Functions, transparent structs, and enums form a parametric core independent of capability dispatch. |
| `GEN-03` | Nonempty `<...>` type clauses use token-only parsing with no semantic fallback. |
| `GEN-04` | Infer a unique substitution locally from signatures, arguments, and immediate expected types. |
| `GEN-05` | Compiler and source evidence share satisfaction, diagnostics, and resolved operations. |
| `GEN-06` | Concepts are named contracts; anonymous impls provide compilation-wide canonical evidence. |
| `GEN-07` | Explicit function-shaped requirements use concept-qualified lookup consistently. |
| `GEN-08` | Contextual `Self` names the implementing type in requirements and impls. |
| `GEN-09` | `+` combines bounds by finite static conjunction. |
| `GEN-10` | Associated types normalize through unique evidence before lowering. |
| `GEN-11` | Compilation-wide coherence gives each obligation at most one applicable evidence. |
| `GEN-12` | Module-domain locality and anchored nominal heads bound impl lookup and overlap checking. |
| `GEN-13` | Declaration identity and normalized semantic arguments identify an instance independently of target form. |
| `GEN-14` | Cross-module use retains resolved facts needed for checking and instance formation. |
| `GEN-15` | Concrete declarations, templates, erasure, and mixed forms remain equivalent lowering choices. |

## Open decisions

**Next discussion:** None

### OPEN-01 — How do C++ boundary functions participate in generics?

- **Status:** Blocked
- **Depends on:** `GEN-01`, `GEN-04`, `GEN-10` through `GEN-14`
- **Blocked by:** The implemented C++ boundary accepts only concrete scalar
  declarations; generic instance publication is not defined.
- **Activation condition:** Representative generic `import(cpp)` or
  `export(cpp)` use requires a finite instance and symbol contract.
- **Question:** C++ boundary participation must not create unrecorded
  applications, instances, conversions, or evidence outside the closed
  semantic graph.
- **Constraints:** C++ names, deduction, overload resolution, and substitution
  failure cannot complete Carven inference or constraints; every generic value
  crossing the boundary has normalized concrete Carven types and a finite typed
  callable contract.
- **Options:** Explicit instance lists, closed compilation-derived instances,
  or separately named generic provider/façade forms.
- **Closure condition:** Specify typed inbound and outbound examples, prove that
  every resulting application and evidence enters the instance graph, and
  reject open uses that cannot be accounted for.

### OPEN-02 — What finite-instance expansion rule is source semantics?

- **Status:** Blocked
- **Depends on:** `OPEN-01`, `GEN-13`
- **Activation condition:** Every external generic entry point is represented in
  the closed graph.
- **Question:** The compiler must distinguish valid recursion, invalid
  by-value storage cycles, infinite argument growth, and implementation
  resource exhaustion.
- **Constraints:** The rule cannot delegate to C++ template-depth failure;
  semantic invalidity needs a stable Carven diagnostic and source anchor;
  compiler budgets are not language limits.
- **Options:** Unknown. Candidate rules must handle expansion such as `F<T>`,
  `F<Box<T>>`, `F<Box<Box<T>>>` without rejecting ordinary recursive calls or
  indirect type references.
- **Closure condition:** Define a decidable semantic rule, diagnostic anchor,
  and separate implementation budget, then validate them against recursive
  call, recursive type, finite mutual recursion, and growing-instance examples.

## Deferred work

### DEFER-01 — Const and value generic parameters

- **Reason deferred:** The first core has only type parameters; value arguments
  add kinds, inference, identity, equality, and evaluation rules.
- **Depends on:** Implemented parametric generic core
- **Reactivation condition:** A concrete API requires a compile-time value that
  cannot remain an ordinary runtime argument or type-level distinction.

### DEFER-02 — Generic lambdas

- **Reason deferred:** Generic closures add capture, callable identity,
  inference, and lifetime questions independently from named declarations.
- **Depends on:** Implemented generics and an owning-callable design
- **Reactivation condition:** A real higher-order API requires a locally
  declared polymorphic callable.

### DEFER-03 — Blanket impl

- **Reason deferred:** An unanchored target turns every obligation into global
  rule search and introduces proof termination and non-local overlap.
- **Depends on:** Implemented canonical evidence and coherence
- **Reactivation condition:** A real library use case supplies finite
  candidate, overlap, termination, and evolution rules.

### DEFER-04 — Specialization and candidate ranking

- **Reason deferred:** Multiple applicable evidence would replace strong
  coherence with priority and compatibility rules not needed by the first model.
- **Depends on:** Implemented canonical evidence and a dedicated specialization proposal
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

### DEFER-07 — Static meta and source generation

- **Reason deferred:** Stable generics must first reveal a bounded query and
  generation need; arbitrary text generation would create a second template
  language.
- **Depends on:** Implemented generics and a dedicated static-meta proposal
- **Reactivation condition:** A concrete consumer defines bounded inputs,
  outputs, semantic ownership, and diagnostics.

### DEFER-08 — Runtime reflection

- **Reason deferred:** Static evidence creates no runtime descriptor, registry,
  ownership, or cost contract.
- **Depends on:** A dedicated reflection proposal and implemented type semantics
- **Reactivation condition:** A runtime use case justifies explicit metadata,
  lifetime, lookup, and cost.

### DEFER-09 — Static capability to dynamic-interface bridging

- **Reason deferred:** The two models have different identity, representation,
  ownership, and dispatch costs.
- **Depends on:** Implemented static capabilities and dynamic interfaces
- **Reactivation condition:** Repeated APIs need the same contract in both
  worlds and can make conversion and cost explicit.

### DEFER-10 — Persistent generic semantic reuse

- **Reason deferred:** The first design composes source in one closed
  invocation and does not need serialized bodies or persistent instance caches.
- **Depends on:** Implemented generics and measured incremental/reuse needs
- **Reactivation condition:** Compilation measurements justify semantic
  serialization, cache identity, invalidation, and compatibility rules.

### DEFER-11 — Stable generic ABI and binary distribution

- **Reason deferred:** Semantic instance identity does not define mangling,
  artifact placement, stable ABI, or binary-only composition.
- **Depends on:** Implemented generics and a supported separate-compilation use case
- **Reactivation condition:** A distribution requirement needs generic
  compatibility across independently built artifacts.

## Implementation

Implementation is blocked by `OPEN-01` and `OPEN-02`. Once closed, delivery
proceeds vertically:

1. add parametric declaration/application syntax and SyntaxProgram/SemIRProgram facts,
   definition-site checking, local inference, normalized instance identity, and
   finite graph production;
2. add `concept`/`impl` parsing, visibility, canonical evidence, module-domain
   and anchored-head validation, coherence, qualified operations, `Self`,
   conjunctive bounds, and associated normalization;
3. select unit-local TargetUnit/C++ realizations only from published semantic facts;
4. deliver diagnostics, tests, generated C++ checks, and permanent documentation
   with each slice.

The compiler should not add unused generic condition fields, registries, target
templates, caches, or ABI scaffolding before a delivered slice needs them.

## Validation

Validation must cover:

- generic parameter/application parsing and the comparison, shift, member, and
  call ambiguity boundaries;
- definition-site versus application-site diagnostics and source anchors;
- local inference, expected types, explicit arguments, arity, duplicate names,
  conflicts, and underconstrained calls;
- allowed unconstrained operations, capability satisfaction, qualified calls,
  exact impl conformance, and unique evidence;
- impl visibility, module-domain locality, anchored heads, generic overlap, and
  coherence across modules;
- associated projection normalization, aliases, cycles, and instance identity;
- access, Take, failure, constants, storage cycles, evaluation order, and
  visibility under instantiation;
- C++ boundary instances and stable rejection of open/untracked applications;
- finite instance production versus semantic infinite expansion and separate
  compiler resource limits;
- C++20/C++23 compile, link, and run without fixing concrete/template target
  shape or private generated names.
