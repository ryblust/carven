# C++ backend

This document describes how a valid `SemanticProgram` is realized as private
C++ artifacts. Artifact-path and generated-C++ stability boundaries are defined
by [compatibility.md](compatibility.md).

```text
SemanticProgram
  -> TargetGenerationPlan
  -> TargetUnitBuilder
  -> TargetUnitFinalizer
  -> TargetRenderer
  -> ArtifactSet
```

## Backend boundary

Semantic analysis supplies resolved declarations, types, constants, calls,
visibility, failure and control facts, places, evaluation effects, and nominal
storage order. Backend failures are invariant violations rather than source
diagnostics.

`TargetGenerationPlan` records cross-unit decisions. Each artifact then uses a
fresh generation context, builder, and value-owned `TargetUnit`; the unit is
finalized, rendered, and released. Unit-local target nodes carry already chosen
names, placement, ownership, and attribution.

The target-quality baseline is the simplest optimizer-visible C++ that
preserves Carven evaluation order, ownership, lifetime, safety, and control
behavior without imposing static requirements stronger than the verified
`SemanticProgram`. Literal source-shaped output is not required when those
guarantees need target temporaries or regions, and an analysis representation
does not by itself justify a corresponding target representation. Private
runtime support adapts incidental C++ library constraints instead of turning
them into source-language requirements.

## Plan construction

Plan construction has four implementation responsibilities:

1. derive type, failure-set, and local-binding representation requirements;
2. allocate linkage, module, entity, payload-enum, and source-scope names;
3. collect published and implementation references and form interface components;
4. finalize artifact paths and data required by lowering.

The implementation is split along those boundaries into names,
representation, references/interfaces, and finalization units. Every module owns a
namespace identity. An entity name stores its owner module and complete relative
member path. One resolver preserves that relative path for same-module uses and
adds the module namespace for cross-module uses without inspecting the entity
kind.

The reference analysis first creates a transient declaration-owner index. For
each module it starts from `HIRModule.items`, scans declarations and their
structured bodies, and scans nested closure bodies through their closure
expressions. It does not follow call edges into another callable body. A call
contributes its callee and argument types plus the referenced callable or
signature contract.

One reference walker handles nominal types, callable signatures, and concrete
callables. Function parameters, results, failure types, and callable signatures
create declaration requirements. Struct fields, enum payloads, and array
elements create complete-definition requirements. The same scan records
external published modules used by a module implementation. An import without
an actual declaration or implementation reference creates no requirement.

## Interface SCCs

Published complete-definition requirements form a module graph. The shared SCC
routine collapses cycles and returns dependency-first components. Every
component emits one interface header anchored to its lexicographically first
canonical module path. Independent components and declarations use stable
module-path and source order.

An interface component plan records its complete logical path, prerequisite
header paths, forward declarations, and declarations. A module plan records its
complete implementation logical path and the interface header paths it uses.
Every semantic unit logical path is finalized by the plan; lowering and emission
do not derive artifact names. Component indices and predecessor indices remain
temporary plan-builder state.

Declaration-only nominal requirements emit deterministic `struct`, `class`, or
explicitly based `enum class` forward declarations only when the name is needed
before its definition. Definitions supplied by predecessor interfaces and
earlier declarations are treated as complete. Complete-definition requirements
include prerequisite interface paths. Published nominal definitions follow
global nominal storage order; callable declarations follow module-path and
source order. Includes use artifact-root-relative angle paths, so
source-directory nesting does not affect lookup.

Private implementation edits do not change an interface when its published
surface and dependencies are unchanged. A published complete-definition edit
changes its component and the components that depend on it; unrelated
components remain independent.

## Module implementations

Each source module emits one path-derived `.cpp`. It includes its own interface,
when present, and the interface paths selected by its actual external published
references. Module-private declarations and definitions, opaque module-level
`#[cpp]` regions, and callable implementations stay in that module unit. Test
functions and registrars are emitted in an anonymous namespace. Test support is
included only by units that use it; default test mode also emits its entry unit.

The stable logical artifact roles are listed in
[compatibility.md](compatibility.md#generated-artifacts).

## Linkage domain and names

A closed compilation request already contains a resolved `LinkageDomain`.
The driver resolves an explicit value or a normalized absolute artifact root.
The backend derives a private 128-bit `LinkageDomainID` from a versioned
protocol, test-emission mode, a fixed domain-kind tag, and the domain value.

Module namespace identity depends on the canonical module path. Generated
declarations live under `carven::generated`, followed by the linkage-domain and
module namespaces. The readable root separates generated private symbols from
runtime and user C++ integration names; the hashed layers provide collision and
linkage isolation.

Module entity names are allocated in three phases: published source entities,
private source entities, then any remaining compiler-owned entities. This keeps
published spellings stable when private declarations are added or removed in a
stable linkage domain. Legal source spelling is retained until a same-scope
collision or C++ reserved form requires deterministic escaping or a suffix.
Callable parameters and the outermost body share a C++ collision domain; nested
blocks retain separate domains. Compiler-owned types use `UpperCamelCase`;
functions, values, members, and temporaries use `lower_snake_case`.

Current-module references use relative member paths. Cross-module references
add the complete qualified module namespace. Nested source entities, including
enum cases, retain their complete relative path. Source declarations use their
typed HIR IDs; local bindings continue to use `SymbolID`.

## Representation and failure transport

Numeric enums lower to native `enum class` declarations with explicit
underlying types. Payload enums lower to a `std::variant` wrapper with factories
and queries. Scalar and numeric-enum read parameters pass by value; aggregate,
array, and payload-enum reads pass by `const&`; write parameters pass by `&`;
take parameters own their values.

The plan stores the pass-by-value decision directly by `HIRTypeID`; it does not
publish a general target representation kind when no lowering consumer needs
one. Payload-enum factories are ordinary inline functions. Backend generation
does not recursively infer whether private concrete factories happen to be
constant-evaluable. A generated declaration is `constexpr` when that property
is required by a semantic constant contract or is unconditional for the
compiler-generated representation, rather than as a target optimization hint.
Generic runtime templates may expose conditional constant evaluation directly
through C++ when doing so neither strengthens admitted type requirements nor
requires an additional semantic fact.

HIR primitive identities remain language types. Target lowering maps fixed
width integers to `std::int*_t` and `std::uint*_t`, pointer-sized integers to
`std::ptrdiff_t` and `std::size_t`, and floating-point primitives to `float` and
`double`. The runtime prelude provides the standard integer headers and retains
the helpers that implement Carven arithmetic and bounds semantics.

Failure information that survives a call or local control join requires a
target representation; replacing that representation does not eliminate the
transport requirement.

A non-failing callable returns its result directly. A failing callable returns
the raw `carven::runtime::Outcome<Result, Failures...>` type in its signature;
there are no per-callable aliases. Each Semantic `FailureSetID` has one
`TargetFailureSetProfile`. Its members are ordered by canonical module path,
qualified source name, and nominal kind. Equal keys for distinct nominals are
an invariant violation; transient HIR IDs never break a tie.

All callable signatures, callable views, carrier construction, and failure
iteration query that same profile. A lowering carrier descriptor stores only
its HIR result, `FailureSetID`, and unit-local target type. Identity transfers
forward the same carrier. A strict subset-to-superset transfer constructs the
destination Outcome from an rvalue source through the runtime converting
constructor. Result changes, narrowing, and incomparable sets are invariant
violations. `HIRTryFacts` records each arm's accepted failure set and its sorted
source indices for reachable alternatives. Lowering consumes those facts
without recomputing coverage. Alternatives in one arm form one conditional
chain; the selected alternative establishes bindings before the arm guard, and
the guard runs once before either entering the body or continuing to the next
arm.

Runtime `Outcome` stores `SuccessState<Result>` and the failure values directly
in one variant. Its class contract requires a nonempty unique failure pack and
nothrow move construction. Same-specialization move assignment replaces the
active alternative by moving it into freshly constructed storage, so transport
does not require Result or failure alternatives to be move assignable.
Cross-specialization construction is implicit and `noexcept` only when the
result types are identical and the destination failure set strictly covers the
source; identity uses the ordinary move constructor. `FunctionRef` applies the
same nothrow result conversion for borrowed objects and compatible function
pointers. Function pointers are kept in erased pointer storage and restored by
a typed thunk; noncapturing temporary lambdas use that pointer path, while
capturing objects remain borrowed. Outcome construction, observation, taking,
widening, and state replacement are `constexpr` whenever their admitted result
and failure operations can be constant-evaluated.

## Evaluation and control

A lowered expression contains an ordered prelude, a target expression, and
optionally an unconsumed failure carrier. A fallible call produces an
unconsumed carrier, and a
propagation expression is its semantic consumption boundary. Return statements,
local carrier state replacement, callable views, and protected try results
share the same identity-or-widening runtime protocol.
Simple protected expressions forward their carrier directly; an IIFE remains
when statements, sequencing, control flow, or local lifetime requires a region.

The evaluation sequencer consults `EvaluationEffect` and materializes a value
when required by non-commuting evaluation, C++ value category or lifetime,
ownership transfer, failure transport, or an opaque raw boundary. Each
materialization carries its source or synthetic attribution. Read, Write, and
Take place sets are sorted;
commutativity uses two-pointer intersections over borrowed spans and does not
construct mutation or access unions. Access conflicts are checked first. Opaque
effects commute only with a genuinely empty effect; a may-terminate effect may
commute with a non-conflicting observational read.

Semantic CFGs and SCCs do not prescribe a `TargetUnit` control-flow shape, and
lowering does not routinely flatten structured functions into label-and-`goto`
graphs. Lowering first uses direct C++ `if`, `switch`, loop, return, and region
forms. When C++ lacks a direct structured transfer, a narrowly scoped synthetic
route is allowed if it is smaller than reifying the surrounding transfers as a
runtime control state. A zero-`goto` target is not a goal by itself.

Structured conditionals, statement matches and catches, loops, and range loops
retain structured C++ where their source control can be represented directly.
Expression matches use an immediately invoked lambda. A C-style `for` is
normalized when a clause needs a prelude, failure transport, or ownership
handling. Local synthetic routes forward failure out of a statement `try`
region and direct `continue` to the step of a normalized C-style `for`; ordinary
lowering has no general `goto` route. Unit validation checks those routes, label
resolution, scope entry, initialization barriers, node ownership, and tree
acyclicity.

## Provenance and rendering

Target declarations and statements carry `SourceOwned`, `SourceExpansion`,
`CompilerOwned`, or `RawSource` attribution. The layout renderer tracks
physical and logical lines and writes final `#line` directives while
serializing. Raw fragments remain opaque bytes. Header and implementation roots
own ordinary preamble, body, and epilogue sections. Linkage, domain, module,
and anonymous namespaces are represented by the same general `TargetNamespace`
node; the test-entry root owns global items.

Rendering serializes planned placement and names. Filesystem mutation, C++
compilation, dependency scanning, optimization, linking, ABI realization, and
platform selection occur downstream. Focused target improvements follow the
criteria in [philosophy.md](philosophy.md#abstraction-cost).
