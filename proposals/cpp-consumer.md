# C++ 消费者契约

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** Opt-in C++ consumption of selected Carven declarations
- **Depends on:** Canonical declaration/type identities; generic expansion additionally depends on a typed `#[cpp]` boundary

## Summary

本文定义 C++ caller 如何通过显式 Carven declaration contract 消费 Carven API，而不把
ordinary generated C++ 提升为 public interface。`export(cpp)` 的 selection、visibility
与 grammar shape 已裁定；V1 declaration/type closure、access/failure mapping、C++
spelling、artifact 与 build contract 仍未裁定。

下一项是 `OPEN-C2`：先明确 include、namespace、name 与 signature contract
需要固定哪些维度，再用 representative surface 驱动 `OPEN-C3` 至 `OPEN-C6`。C2 的完整 signature
不能在 C3-C6 之前最终关闭，但这不阻止先讨论其独立 naming/artifact-facing 形态；
C3 也可以并行收集候选 evidence。

| Slice | Maturity | Current frontier |
| --- | --- | --- |
| Source selection | Accepted | `C1` |
| V1 declaration and type closure | Exploration | `OPEN-C3` 可与 C2 evidence 并行 |
| Access and failure mapping | Exploration | `OPEN-C4` through `OPEN-C6` |
| C++ names and signatures | Exploration | `OPEN-C2` 是下一讨论，final closure 等待 C3-C6 |
| Artifact and downstream build | Exploration | `OPEN-C7` |
| Consumer fixture ownership | Delivery gate | `C8` in Implementation and Validation |
| Broader ABI and surfaces | Deferred | `DEFER-01` through `DEFER-06` |

## Context

### Problem and boundary

Carven already generates ordinary C++, but generated names, helpers, placement,
formatting, and artifacts are compiler implementation details. C++ consumers
need an explicit stable surface whose source selection, admissible types,
access/failure behavior, declarations, artifacts, and build responsibilities
are checked by Carven.

This proposal covers the direction **C++ calls Carven**. It does not redefine
the `#[cpp]` expression boundary through which Carven embeds or calls C++.
Canonical identities are a direct prerequisite. A future generic consumer
surface also depends on the typed `#[cpp]` and closed-instance rules so raw C++
cannot create untracked Carven generic applications through incidental names.
The concrete non-generic V1 can be designed without admitting that expansion.

### Current behavior

Ordinary generated C++ is not a stable consumer interface. Carven currently has
no accepted header path, namespace, C++ declaration spelling, recursive
admissible-type closure, adapter signature, ODR ownership, or downstream build
contract for handwritten C++ callers.

The accepted-but-unimplemented `export(cpp)` form below is the only settled
source selection. All V1 surface examples after it remain candidates until
their OPEN entries close.

## Goals and non-goals

### Goals

- Select declarations explicitly for C++ consumption.
- Define the admissible Carven source and type closure.
- Preserve Carven access, failure, evaluation, and runtime semantics through
  adapters.
- Publish stable C++ names, signatures, artifacts, and downstream
  compile/link responsibilities only where explicitly promised.
- Reject invalid consumer surfaces with Carven diagnostics before C++ emission.
- Validate the contract with handwritten C++20 and C++23 consumers.

### Non-goals

- Stable ABI, binary-only distribution, or cross-compiler compatibility.
- C++ templates consuming Carven generics in V1.
- C++ inheritance of Carven classes, construction of payload enums, or calls to
  closures and dynamic values in V1.
- Automatic conversion between Carven failure contracts and C++ exceptions.
- C++ declarations participating in Carven lookup, identity, or coherence.
- A general attribute namespace, user-defined export forms, macros, rename,
  façade declarations, wrapper bodies, or module-wide export switches.
- `export(c)`, `export(python)`, or multi-form export in V1.

## Design

### Source selection with `export(cpp)`

**Maturity:** Accepted source semantics; not implemented.

`export(cpp)` is declaration-adjacent:

```carven
export fn parse(input: i32) -> i32 {
    return input;
}

export(cpp) fn parse_for_cpp(input: i32) -> i32 {
    return input;
}
```

The rules are:

- bare `export` gives Compilation visibility to Carven source;
- `export(cpp)` includes bare-export visibility and additionally selects the
  same declaration for the C++ consumer surface;
- there is no C++-visible but Carven-private export state;
- `cpp` is a compiler-defined contextual form name, not a global keyword or
  ordinary looked-up declaration;
- the parser recognizes `export`, delimiters, and one form identifier without
  semantic lookup;
- `export()`, multiple arguments, or missing delimiters are rejected in V1;
- unknown forms diagnose and do not fall back to bare `export`;
- selection does not change declaration identity, Carven call semantics, type
  identity, or module lookup;
- the form does not provide rename, alternate signature, wrapper body, helper
  name, generated-name selection, module switch, or façade declaration.

`export(cpp)` and `class(form)` both use compiler-defined parenthesized
declaration forms but remain independent. `#[cpp]` continues to denote the
opaque C++ source boundary rather than a general attribute namespace.

### Candidate V1 source surface

**Maturity:** Exploration; owned by `OPEN-C3` through `OPEN-C6`.

The leading V1 candidate is concrete and non-failing:

```carven
export(cpp) struct Point {
    x: i32,
    y: i32,
}

export(cpp) fn translate(point: Point, dx: i32, dy: i32) -> Point {
    return Point { x: point.x + dx, y: point.y + dy };
}
```

Candidate declaration kinds are non-generic functions, transparent structs,
and numeric enums. Candidate types are builtin numeric types, `bool`, `char`,
selected numeric enums, and selected transparent structs whose fields
recursively satisfy the same closure.

The candidate rejects `str`, payload enums, arrays, callable views, `Foreign`,
generics, classes, dynamic values, closures, Take parameters, nonempty failure
contracts, and stable symbols for top-level constants. These are not settled
V1 exclusions until the corresponding OPEN decisions close.

Any nominal declaration recursively exposed by a selected signature must itself
write `export(cpp)`. Ordinary `export` does not upgrade transitively.

### Semantic invariants

**Maturity:** Existing Carven semantic constraints; their concrete C++ mapping
is owned by `OPEN-C3` through `OPEN-C6`.

Regardless of the final V1 closure:

- analysis builds consumer facts from completed Carven declaration, type,
  access, failure, visibility, and identity facts;
- invalid surfaces are rejected before target generation;
- C++ deduction, overload resolution, declaration lookup, link results, and
  generated spelling never determine Carven validity or identity;
- an ordinary generated declaration is not public without `export(cpp)`;
- an adapter may call the selected implementation but cannot change evaluation,
  access, failure, lifetime, or runtime behavior;
- ordinary lowering names/placement and public consumer names/placement are
  managed separately;
- the downstream C++ compiler realizes target well-formedness, ABI, and linking
  but does not complete Carven semantic decisions.

Read, Write, and Take retain their Carven meanings. The selected C++ parameter
forms must express those contracts rather than copying ordinary private
lowering shapes. In particular, Write is non-owning and nonexclusive, including
the case where one C++ object supplies multiple Write arguments. A C++ `T&&`
alone does not express Carven's Take availability transition.

### Public surface and artifacts

**Maturity:** Exploration; owned by `OPEN-C2` and `OPEN-C7`.

Only explicitly promised header paths, namespaces, declaration names,
signatures, collision rules, implementation artifacts, and link responsibilities
become stable. Full generated header text, private namespaces, helper names,
ordinary declaration placement, and formatting remain implementation details.

Path-derived module domains do not themselves create a C++ façade. Public
consumer artifacts are selected independently from ordinary target module
interface artifacts.

### Compiler-owned facts and responsibilities

**Maturity:** Delivery obligations; concrete facts remain blocked by
`OPEN-C2` through `OPEN-C7`.

The eventual implementation must:

- parse and represent the optional export form and reject malformed shapes;
- validate form, declaration kind, recursive type closure, visibility, access,
  and failure;
- freeze selected consumer facts in semantic IR rather than recovering them
  from source text or C++;
- generate stable public declarations/adapters separately from private ordinary
  implementation;
- emit the selected logical artifacts while leaving materialization to the
  artifact sink;
- leave target grouping, compilation, linking, and ABI realization to the
  downstream build.

AST containers, SemanticProgram fields, unit-local TargetUnit helpers, naming allocators, and adapter
implementation are private choices. No lowering representation has been
selected while C2-C7 remain open.

### Diagnostics

**Maturity:** Required diagnostic boundary; the exact matrix is owned by
`OPEN-C2` through `OPEN-C7`.

The eventual source contract must diagnose malformed or unknown export forms,
unsupported declarations and types, unselected nominal leaks, visibility
leaks, generic forms, disallowed Take/failure contracts, recursive closure
failure, public-name collisions, and source/build configurations that cannot
satisfy the selected artifact/ODR contract. Diagnostics anchor owning Carven
source rather than delegating ordinary rejection to C++.

## Decision record

| ID | Decision | Design | Rationale |
| --- | --- | --- | --- |
| `C1` | `export(cpp)` selects the same declaration for Carven visibility and the C++ consumer surface, using one compiler-defined contextual form. | [Source selection with `export(cpp)`](#source-selection-with-exportcpp) | Makes interop opt-in without changing declaration identity or creating a second wrapper language. |

## Open decisions

**Next discussion:** `OPEN-C2`

### OPEN-C2 — What stable C++ names and signatures are published?

- **Status:** Active
- **Depends on:** `C1` for initial naming exploration; complete signature
  closure additionally depends on `OPEN-C3` through `OPEN-C6`
- **Blocked by:** None
- **Activation condition:** Active now
- **Why it matters:** C2 owns the include path, namespace, declaration name,
  full parameter/result spelling, and collision/duplicate rules that make the
  consumer surface real.
- **Constraints:** Canonical Carven identity is not redefined by generated
  spelling; only explicitly selected declarations become stable; private
  lowering remains independently replaceable.
- **Options:** First compare one dedicated logical header/namespace/name scheme
  against current artifact roles using representative function, struct, and
  enum signatures. Exact type/access/failure spellings remain conditional on
  C3-C6 and are not accepted by this exploration.
- **Closure condition:** First record the independent path, namespace, name, and
  collision shape; after C3-C6 close, publish one exact complete signature
  contract for every admitted declaration kind.

### OPEN-C3 — Which declarations and recursive types form V1?

- **Status:** Active
- **Depends on:** `C1`
- **Blocked by:** None
- **Activation condition:** Active now
- **Why it matters:** Every signature, diagnostic, artifact, and fixture depends
  on a finite admissible Carven source surface.
- **Constraints:** Nominal types exposed recursively are explicitly selected;
  Carven validity closes before C++; V1 remains small enough to specify fully.
- **Options:** A — non-generic functions, transparent structs, numeric enums,
  builtins/`bool`/`char`, and recursively selected structs; B — a smaller
  function-and-builtin slice; broader generic/class/dynamic forms remain
  deferred.
- **Closure condition:** Select declaration kinds and a recursive type closure,
  then classify every current builtin, aggregate, enum, callable, foreign, and
  nominal form as admitted, rejected, or separately deferred.

### OPEN-C4 — How do Read and Write map to public C++ parameters?

- **Status:** Blocked
- **Depends on:** `OPEN-C3`
- **Blocked by:** `OPEN-C3`
- **Activation condition:** V1 value types are known.
- **Why it matters:** Ordinary private `In<T>` lowering is not a public contract,
  and Write must preserve non-owning, nonexclusive behavior.
- **Constraints:** Read and Write keep their Carven meaning; aliasing one C++
  object into multiple Write arguments behaves like the Carven call; adapters
  introduce no ownership.
- **Options:** Unknown until concrete scalar, enum, and struct signatures from
  `OPEN-C3` are compared.
- **Closure condition:** Fix complete C++ parameter types and adapter behavior
  for every V1 source type, including aliasing and mutation examples.

### OPEN-C5 — Does V1 reject Take permanently?

- **Status:** Blocked
- **Depends on:** `OPEN-C3`
- **Blocked by:** `OPEN-C3`
- **Activation condition:** V1 value types and ownership expectations are known.
- **Why it matters:** C++ `T&&` does not by itself communicate or enforce the
  Carven binding's unavailable transition.
- **Constraints:** No adapter may pretend move syntax alone proves Take
  semantics; any admitted form needs explicit lifetime and post-call behavior.
- **Options:** A — reject Take in V1; B — define a dedicated consumer ownership
  carrier and adapter contract.
- **Closure condition:** Either record the V1 rejection or demonstrate a
  complete source-to-C++ ownership transition for representative values.

### OPEN-C6 — Does V1 reject failure contracts or define a public carrier?

- **Status:** Blocked
- **Depends on:** `OPEN-C3`
- **Blocked by:** `OPEN-C3`
- **Activation condition:** The V1 callable kinds are known.
- **Why it matters:** The current private exact-contract failure carrier is not
  automatically a stable consumer API, and exceptions are not an implicit
  translation.
- **Constraints:** Failure alternatives, propagation, lifetime, and any logical
  runtime-header dependency are part of C6 if failures are admitted; C7 later
  decides artifact grouping, ODR, and downstream materialization. The internal
  protocol selected by the typed-failure-effects proposal remains replaceable;
  C6 must define an adapter contract rather than stabilize that protocol
  accidentally.
- **Options:** A — require an empty failure set in V1; B — define an explicit
  stable failure carrier and header dependency.
- **Closure condition:** Choose the V1 rule and, if failures are admitted,
  specify complete C++ construction, inspection, propagation, destruction, and
  the stable carrier/runtime-header contract.

### OPEN-C7 — Which artifacts own implementation, ODR, and downstream build responsibility?

- **Status:** Blocked
- **Depends on:** `OPEN-C2`
- **Blocked by:** `OPEN-C2`
- **Activation condition:** Header and declaration surface are fixed.
- **Why it matters:** Multiple downstream targets and repeated generation need
  one link and duplication contract.
- **Constraints:** Module-domain identity does not imply C++ artifact grouping;
  BMI orchestration remains a build concern; consumer and private artifacts may
  have different roles.
- **Options:** Reuse an existing logical target artifact role or introduce a
  dedicated consumer header/implementation role; both must define repeated
  include, duplicate implementation, library grouping, and contract-instance
  identity.
- **Closure condition:** Specify artifacts and ownership for one generated batch
  shared by targets and for equivalent closed compilations generated
  independently.

## Deferred work

### DEFER-01 — Generic declaration and instance consumption

- **Reason deferred:** V1 first needs one finite concrete contract; generic
  consumption adds instance discovery, specialization boundary, body
  availability, and closed/open-world accounting.
- **Depends on:** Implemented concrete V1 and the generics proposal
- **Reactivation condition:** A real C++ consumer needs a concrete generic
  instance or template-facing surface and supplies finite identity and artifact
  requirements.

### DEFER-02 — Class and dynamic-value consumption

- **Reason deferred:** Class representation, ownership, conformance, erasure,
  dispatch, and lifetime are not part of the concrete V1.
- **Depends on:** Implemented concrete V1 and the classes proposal
- **Reactivation condition:** A C++ caller needs an ordinary class or dynamic
  interface and its ownership/dispatch contract is stable.

### DEFER-03 — Closure and owning-callable consumption

- **Reason deferred:** Captures, callable ownership, invocation lifetime, and
  failure behavior need an owning-callable contract not present in V1.
- **Depends on:** Implemented concrete V1 and an owning-callable proposal
- **Reactivation condition:** A C++ callback use case supplies explicit capture,
  ownership, invocation, and artifact requirements.

### DEFER-04 — Stable ABI and binary distribution

- **Reason deferred:** A source-level consumer header does not define
  cross-compiler layout, calling convention, runtime compatibility, or
  binary-only artifact evolution.
- **Depends on:** Implemented consumer contract and a dedicated ABI proposal
- **Reactivation condition:** A supported binary distribution or plugin use case
  requires compatibility across independent builds.

### DEFER-05 — Façade customization

- **Reason deferred:** Rename, alternate signatures, wrapper bodies, and
  module-wide switches create a second API-shaping layer beyond direct selected
  declarations.
- **Depends on:** Implemented `export(cpp)` consumer V1
- **Reactivation condition:** A concrete C++ API cannot be represented by direct
  selected declarations and can define one deterministic façade contract.

### DEFER-06 — Additional foreign export forms

- **Reason deferred:** `export(c)`, `export(python)`, and multi-form export
  each need independent type, lifetime, failure, runtime, and artifact
  contracts.
- **Depends on:** Implemented `export(cpp)` consumer V1
- **Reactivation condition:** A supported non-C++ consumer has a concrete
  end-to-end contract and cannot reuse the C++ surface.

## Implementation

Implementation is blocked by `OPEN-C2` through `OPEN-C7`. Once they close, one
vertical delivery adds export-form syntax/AST, semantic selection and recursive
validation, published consumer facts, public generated declarations/adapters,
logical artifacts, diagnostics, and permanent documentation.

### C8 — Consumer fixture ownership and compatibility matrix

**Maturity:** Delivery requirement; not a source-semantic open decision.

C8 is resolved during implementation after C2-C7 produce a complete public
surface. The delivery must choose between the existing interop-test ownership
and a dedicated consumer fixture, then cover C++20/C++23, include/compile/link/
run behavior, repeated includes, multiple targets, and the C7 ODR cases.

The proposal does not yet select the fixture owner. The fixture and matrix land
with the first public contract and assert only stable paths, names, signatures,
diagnostics, and behavior.

Artifact sinks materialize selected paths but do not decide semantics.
Downstream builds compile, group, link, and realize ABI according to C7.

## Validation

The first implementation must prove:

- bare `export` does not enter the public C++ surface while `export(cpp)` remains
  ordinarily importable by Carven;
- every admitted function, struct, enum, and recursively admitted field type
  has the exact C2 declaration and behavior;
- unsupported declarations/types, unselected nominal leaks, generic forms,
  Take/failure choices, visibility leaks, and collisions produce Carven
  diagnostics;
- Read/Write signatures and runtime mutation/alias behavior match C4;
- handwritten C++20 and C++23 translation units include, compile, link, and run;
- ordinary private generated spelling can change without breaking the fixture;
- repeated include, duplicate implementation, multiple targets, and independent
  generation exercise C7.

C8 owns the fixture location and complete compatibility matrix used to collect
that evidence; Validation does not preselect its repository target.

Validation snapshots only stable paths, names, signatures, diagnostics, and
observable behavior, never the complete generated C++ text.

## References

- [Carven semantics](../docs/semantics.md)
- [Carven compiler model](../docs/compiler.md)
- [Carven backend](../docs/backend.md)
- [Typed-failure effects proposal](typed-failure-effects.md)
- [Failure-model and runtime-materialization note](../notes/failure-models.md)
- [Generics and static constraints](generics.md)
- [Classes and dynamic polymorphism](classes.md)
- [Proposal roadmap](roadmap.md)
