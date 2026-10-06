# Generics and static capabilities

- **Status:** Draft
- **Implementation:** In progress for the parametric core; capability dispatch is not started
- **Scope:** Generic instances, static capabilities, coherence, and target-realization freedom
- **Depends on:** None for the accepted source semantics; generic
  `import(cpp)`/`export(cpp)` participation depends on the implemented concrete
  C++ boundary and a finite generic publication contract

## Summary

This proposal defines type parameters, definition-site checking, local inference,
static capabilities, associated types, coherence, and generic instance identity.
The source semantics below are accepted. The selected implementation slice
defines the boundaries of the parametric core separately from capability dispatch.

The first implementation slice covers nominal type parameters and finite instance
formation. Parameterized bodies, their universal operations, and generic C++
boundary participation remain subsequent work under OPEN-01 through OPEN-03.
Lowering may use concrete C++ declarations, templates, erasure, or
a combination after semantic analysis.
Advanced facilities remain under DEFER-01 through DEFER-11.

## Context

Carven compiles a closed batch of modules with canonical module paths,
module-domain visibility, and direct lookup. Nominal type parameters produce
concrete declarations before publication. Parameterized function bodies,
concept requirements, and impl evidence are not implemented. `concept`, `impl`,
`Self`, and `where` are ordinary identifiers today.

Named type arguments, builtin `ptr<T>`, and nested `>>` parsing exist.
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

The parametric core covers type-parameterized functions, transparent structs,
enums, and ordinary value classes. The broader design includes checked generic
bodies, bounded local inference, canonical static
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

The parametric core supports generic functions, transparent structs, enums,
and ordinary value classes:

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

The Read `identity` example requires copying. Its definition needs a universal
copy contract for the admitted argument domain. A copyable application alone
cannot establish that contract; OPEN-03 governs this part of body checking.

### Selected implementation slice

**Design:** Nominal type parameters first, then checked parametric bodies.

**Implementation:** Nominal type parameters are implemented. Parameterized bodies
are not implemented; a parsed declaration does not establish semantic support.

The nominal milestone supports structs, payload enums, and the field-type model
of classes without operations. Parameters are identified by their definition
and ordinal. Definitions bind field and payload type expressions once; instance
formation substitutes normalized arguments into ordinary concrete declarations.
Their actual types obey existing value-position, storage, ownership, and native
interop rules. This milestone does not require a universal copy operation on an
unknown parameter. SIMD and mask values follow their existing concrete rules.

Source visibility belongs to the generic definition and its actual arguments.
Generated instances are implementation declarations. C++ interface planning
places their concrete definitions and dependencies independently of source
audience; it does not introduce source-public instance names.

Parameterized functions, class operations, and numeric enums have explicit
unsupported-form diagnostics in this milestone. It does not complete the
parametric core or provide a growable owning container.

The parameter-flow rule described below applies to the slice's finite first-order
constructors. A cycle containing a construction edge is invalid; forwarding and
permutation cycles are finite. By-value storage cycles are checked separately.
Implementation budgets report resource exhaustion without changing source
legality.

Source-head completion and concrete-instance closure are separate construction
boundaries. Publication requires complete concrete declarations. C++ lowering
consumes the ordinary concrete representation.

#### Subsequent parameterized bodies

The next stage reuses checked structured operations with rigid parameter types.
Class operations inherit their owner's parameters and retain ordinary lexical
representation access. Independently generic operations and combined static
value/type specialization remain outside the initial body slice.

Holding, copying, complete Take, forwarding, and same-type assignment require
their shared operation contracts. Default construction, comparison, arithmetic,
formatting, and member lookup on an unknown parameter are not universal.
Copying String copies its content; copying a slice preserves its backing.

Generic bodies preserve selected storage separately from contained loans.
Unknown contents carry an opaque loan bundle identified by the input value and
its projection path. Copy preserves that bundle in a new owner, Take transfers
it, and known aggregate construction and projection preserve field paths.
Definition checking establishes availability and escape restrictions; concrete
instances validate actual backing through ordinary ownership rules.

Generic C++ import/export declarations and native operations whose legality
depends on a type parameter are rejected. Concrete boundary wrappers may call
recorded instances. Concepts, evidence, associated projections, value parameters,
generic lambdas, and combined static-value/type-generic specialization are outside
this slice. Array extents and failure types remain independent of type parameters.

Checked bodies reuse structured semantic operations and explicit substitution;
applications do not replay source AST. Executable publication requires concrete
types and resolved operation identities.

Container storage, view invalidation, and constant-result retention belong to
[constant storage](constant-storage.md), independently of generic declaration
support. This slice does not establish a growable owning container.

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
after OPEN-01 and OPEN-02.

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
| GEN-01 | Check bodies at definition site for caller-independent meaning and diagnostics. |
| GEN-02 | Functions, transparent structs, and enums form a parametric core independent of capability dispatch. |
| GEN-03 | Nonempty `<...>` type clauses use token-only parsing with no semantic fallback. |
| GEN-04 | Infer a unique substitution locally from signatures, arguments, and immediate expected types. |
| GEN-05 | Compiler and source evidence share satisfaction, diagnostics, and resolved operations. |
| GEN-06 | Concepts are named contracts; anonymous impls provide compilation-wide canonical evidence. |
| GEN-07 | Explicit function-shaped requirements use concept-qualified lookup consistently. |
| GEN-08 | Contextual `Self` names the implementing type in requirements and impls. |
| GEN-09 | `+` combines bounds by finite static conjunction. |
| GEN-10 | Associated types normalize through unique evidence before lowering. |
| GEN-11 | Compilation-wide coherence gives each obligation at most one applicable evidence. |
| GEN-12 | Module-domain locality and anchored nominal heads bound impl lookup and overlap checking. |
| GEN-13 | Declaration identity and normalized semantic arguments identify an instance independently of target form. |
| GEN-14 | Cross-module use retains resolved facts needed for checking and instance formation. |
| GEN-15 | Concrete declarations, templates, erasure, and mixed forms remain equivalent lowering choices. |
| GEN-16 | Ordinary value classes share the parametric nominal model; operations inherit their owner's parameters and lexical access authority. |

## Open decisions

### OPEN-01 — How do C++ boundary functions participate in generics?

- **Status:** Active
- **Depends on:** GEN-01, GEN-04, GEN-10 through GEN-14
- **Question:** Does the initial generic core exclude generic C++ boundary
  declarations, or include a finite provider or consumer contract? Participation
  must not create unrecorded applications, instances, conversions, or evidence
  outside the closed semantic graph.
- **Constraints:** C++ names, deduction, overload resolution, and substitution
  failure cannot complete Carven inference or constraints; every generic value
  crossing the boundary has normalized concrete Carven types and a finite typed
  callable contract.
- **Options:** A closed Carven core that rejects generic `import(cpp)` and
  `export(cpp)` declarations while concrete scalar wrappers may call recorded
  instances; explicit instance lists; closed compilation-derived instances;
  or separately named generic provider/façade forms.
- **Closure condition:** Record the exclusion with accepted and rejected boundary
  examples, or use a representative boundary consumer to specify typed inbound
  and outbound examples and prove that every application and evidence enters
  the instance graph. Reject open uses that cannot be accounted for.

### OPEN-02 — What finite-instance expansion rule is source semantics?

- **Status:** Blocked
- **Depends on:** OPEN-01, GEN-13
- **Activation condition:** OPEN-01 closes the initial boundary scope; every
  admitted external generic entry point is represented in the closed graph.
- **Question:** The compiler must distinguish valid recursion, invalid
  by-value storage cycles, infinite argument growth, and implementation
  resource exhaustion.
- **Constraints:** The rule cannot delegate to C++ template-depth failure;
  semantic invalidity needs a stable Carven diagnostic and source anchor;
  compiler budgets are not language limits.
- **Options:** The parameter-flow candidate below rejects cycles containing type
  construction while permitting forwarding and permutation. Evaluate its
  conservatism and extend its proof to the accepted associated-type and evidence
  model before selecting it. Another rule needs equivalent decidability,
  diagnostic, and closure evidence.
- **Closure condition:** Define a decidable semantic rule, diagnostic anchor,
  and separate implementation budget, then validate them against recursive
  call, recursive type, finite mutual recursion, and growing-instance examples.

### OPEN-03 — Which operations are universal for admitted type arguments?

- **Status:** Active
- **Depends on:** GEN-01, GEN-02, GEN-05
- **Question:** Does unconstrained `T` admit ordinary copying, or does a copying
  body need an explicit capability? Define the admitted type set and distinguish
  holding, Read/Write forwarding, complete Take, copying, default construction,
  comparison, formatting, and parameter-dependent assignment.
- **Constraints:** Definition-site checking cannot infer a capability from the
  currently selected applications. Copying String content is not a bitwise copy.
  The accepted Read `identity` example must be accounted for explicitly.
- **Options:** Admit a type set with universal copying; require copy evidence;
  or define another operation boundary with corresponding admission rules.
- **Closure condition:** Check Read identity, Take relay, storage and release,
  slice forwarding, and enum payload bindings with copyable and borrowed values.
  Record any revision to an accepted decision explicitly.

## Candidate implementation contracts

**Maturity:** Exploration.

### Consumers and admission

Choose a real interface repeated across at least two concrete types and identify
its transfer, borrowing, copying, and operation needs. Candidate examples,
subject to OPEN-03, include:

```carven
fn relay<T>(&&value: T) -> T => &&value;
fn hold<T>(&&value: T) -> Box<T> => { value: &&value };
fn release<T>(&&owner: Box<T>) -> T => (&&owner).value;
enum Maybe<T> { None, Some(T) }
fn pass_slice<T>(values: [T]) -> [T] => values;
```

Slice forwarding copies its descriptor and preserves backing, without copying
`T`. A payload wildcard can match without producing an owner; an owning payload
binding may require copying. Each operation needs its own guarantee.

A restricted argument set could admit modeled Carven scalars, String/str,
arrays, slices, pointers, and ordinary nominal combinations, excluding void,
unknown native types and their containing types, callable/closure value types,
and compiler-private forms. Recursive admission distinguishes pointer edges from
by-value storage cycles. An ordinary class argument does not admit generic class
declarations or class constant execution. SIMD arguments need explicit operation
and ownership rules; builtin identity gives unknown `T` no SIMD capability.

One experiment can keep array lengths independent of `T`, failure types concrete,
and native operations independent of `T`. Constant generic aggregates and generic
static calls have separate admission questions; an ordinary `const fn` cannot
bypass an unsupported generic call. Local inference under GEN-04 includes
occurs-check and evaluates each actual argument once.

### Finite instance expansion

The candidate for OPEN-02 builds a graph whose vertices are type-parameter
positions in generic declarations. An argument coming directly from a source
parameter creates a forwarding edge; an argument containing that parameter under
an array, slice, pointer, or nominal constructor creates a construction edge.
Fully concrete arguments contribute no parameter-origin edge. Analyze nested
applications separately. Reject every strongly connected component containing a
construction edge; components containing only forwarding edges are allowed.

| Application or storage relation | Candidate result |
| --- | --- |
| `f<T>` calls `f<T>`; `f<A, B>` calls `f<B, A>` | Allowed reuse or finite permutation |
| `f<T>` calls `g<Box<T>>`, with no return edge | Allowed finite wrapping |
| `f<T>` calls `g<Box<T>>`, which calls `f<Box<T>>` | Rejected argument growth |
| `f<T>` calls `g<Box<T>>`; `g<U>` calls `f<i32>` | Allowed concrete reset |
| `Node<T>` contains `ptr<Node<T>>` | Finite arguments; no by-value cycle |
| `Grow<T>` contains `ptr<Grow<[T]>>` | Rejected growth despite pointer indirection |
| `Bad<T>` contains `Bad<T>` | Finite instances but rejected by-value storage cycle |

Check unused definitions and ordinary source branches; backend elimination is
not a legality proof. With finite first-order constructors, concrete array
lengths, and no associated-projection reduction or declaration generation,
collapsing forwarding cycles leaves an acyclic construction graph with bounded
wrapping depth. Those assumptions do not cover the full accepted capability and
associated-type model. Its normalization and evidence expansion need a separate
termination argument before this rule can close OPEN-02.

Symbolic checking rejects evident layout cycles; concrete instances still check
the substituted storage topology. Budgets for instance count, type nodes, and
work queues diagnose resource exhaustion separately from semantic infinite
expansion. Raising a budget does not change source legality.

### Instance lifecycle and publication

A construction design can retain each checked parameterized definition once and
index its instances by the accepted normalized identity. Rigid parameters use
definition identity and ordinal, rather than an arbitrary placeholder TypeID or
incomplete C++ type. Instances reuse structured semantic operations and traversal;
application sites consume checked bodies rather than replaying source AST.

Distinguish reserved, head-available, body-in-progress, complete, and failed
instances. Recursion reuses the same key. A failed instance retains failure state
to avoid duplicate construction and primary diagnostics. An available signature
permits recursive calls without permitting reads of an unfinished body or layout.

Evaluate reuse of the existing program and stage-session completion boundaries.
Define how catalog closure, static-value instances, and type-generic closure
interact; they retain distinct argument identities. Every concrete application
requiring semantic checking enters the closure even if its enclosing function
does not generate native code. Unused definitions are checked without enumerating
all possible arguments.

Before publication, check key uniqueness, program-owned and closed arguments,
complete reservations, body ownership, and converged types, failures, and
ownership. Concrete bodies contain no symbolic parameters, pending applications,
or unresolved evidence. Parameterized bodies may be released when no downstream
consumer needs them, while required provenance survives. Caller lifetime, source
origin, and target names do not become instance-key components.

### Ownership and artifact delivery

An unknown `T` may contain loans. Parameterized checking must preserve transfer
and projection relationships, reject use after Take and local-borrow escape, and
avoid assuming a scalar snapshot, no cleanup, or copying beyond OPEN-03's
selected guarantee. Concrete calls check the actual type contents and backing;
a String owner and a record containing str cannot share a single borrowed-input
flag. Residual realization uses concrete `TypeContents`. Pattern rejection is
proved in the symbolic domain or reconstructed and checked for an instance;
guards and alternative binding sources remain independent.

One artifact candidate gives the defining module ownership of an instance and
makes callers reference the same semantic declaration. Caller-private nominal
arguments may require internal support interfaces, without widening source
audience. Validate two caller modules requesting one instance, private arguments,
and reverse dependencies through independent generation, self-contained headers,
multi-TU compile/link/run, and input-order determinism. Specify any additional
planning dependencies and preserve private-type audience in internal interfaces.

## Deferred work

### DEFER-01 — Const and value generic parameters

- **Reason deferred:** The first core has only type parameters; value arguments
  add kinds, inference, identity, equality, and evaluation rules.
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

- **Reason deferred:** Multiple applicable evidence would replace strong
  coherence with priority and compatibility rules not needed by the first model.
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

### DEFER-07 — Static meta and source generation

- **Reason deferred:** Compile-time queries and generation need input, output,
  semantic ownership, and diagnostic rules beyond parameterized declarations.
- **Depends on:** A bounded query and generation contract; generic reflection
  additionally consumes checked generic definitions and instances
- **Reactivation condition:** A concrete consumer defines bounded inputs,
  outputs, semantic ownership, and diagnostics.

### DEFER-08 — Runtime reflection

- **Reason deferred:** Static evidence creates no runtime descriptor, registry,
  ownership, or cost contract.
- **Depends on:** Type identity and metadata, lifetime, and lookup contracts
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

Implementation of the selected parametric slice proceeds through checked nominal heads,
parameterized bodies, and concrete instance closure. Each delivered declaration
form needs source syntax, definition-site checking, normalized identity, and
finite graph production together; generic calls additionally need local inference.
Broader domains remain subject to OPEN-01 through OPEN-03.
Capability consumers additionally need their
concept and impl contracts, coherent evidence, and associated normalization.
TargetUnit lowering consumes published semantic facts. Each slice includes its
diagnostics, generated C++ checks, tests, and permanent documentation.

## Validation

Validation covers the rules admitted by the selected slice:

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
- unused invalid definitions, String transfer, borrowed forwarding and escape,
  payload wildcard versus owning bindings, and exactly-once arguments;
- same-key recursion, failed-instance reuse, complete reservations, foreign
  identities, duplicate keys, symbolic leakage, and bounded diagnostic chains;
- C++ boundary instances and stable rejection of open/untracked applications;
- finite instance production versus semantic infinite expansion and separate
  compiler resource limits;
- C++20/C++23 compile, link, and run without fixing concrete/template target
  shape or private generated names.

Application-site AST replay, C++ diagnostics deciding source legality, and caller
lifetime in instance identity violate the definition-site and identity contracts.
Representation sharing needs correct C++ signatures or typed thunks; arbitrary
function-pointer casts and equal storage size do not establish type or behavioral
equivalence.

## References

- [Dynamic values](dynamic-values.md): runtime holding and dispatch, including
  existential packages and polymorphic elimination.
- [C++ interoperation](../docs/language/interop.md): implemented concrete boundary
  used by the options in OPEN-01.
- [Go parameter-flow checking](https://go.dev/src/cmd/compile/internal/types2/mono.go):
  a compiler implementation reference for detecting recursive type-argument growth.
