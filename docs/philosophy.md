# Design philosophy

This document records lasting criteria for evaluating Carven language and
compiler design. A principle guides a decision; it does not establish a
feature or specify supported behavior.

## Intent over mechanism

Carven source should express program intent while leaving representation
choices to the compiler when they do not change observable behavior. A second
source form is justified by a distinct operation, guarantee, cost, or
interoperability boundary, not merely by a second C++ mechanism.

Source should expose behavior that changes how a caller or reader reasons about
control, ownership, mutation, allocation, failure, or compatibility. Generated
names, unit layout, temporaries, and equivalent calling representations remain
implementation choices unless a program or supported consumer observes them.

## Semantic closure

Names, visibility, types, call selection, evaluation order, ownership,
mutation, failure contracts, pattern coverage, and required constant facts
should be decided before C++ generation. A valid semantic program has one
resolved meaning; the target realizes that meaning rather than completing it.

C++ overloads, templates, destructors, library types, and optimizer choices may
implement a resolved contract. They do not decide ordinary Carven validity.
Facts inside an explicit C++ boundary remain outside compiler knowledge unless
the boundary supplies a Carven contract.

## Constraint provenance

Every target or runtime precondition must trace to a verified semantic fact, a
guarantee of compiler-generated types, or an explicit interoperability
contract. A later layer may assert an earlier fact as an invariant, but C++
traits and library protocols do not create new source-language validity rules.

A representation uses the weakest operations sufficient to preserve the
semantic contract. Convenience in a selected C++ type does not justify
requiring stronger construction, assignment, comparison, ownership, or
exception properties from admitted source types. When a target mechanism has a
stronger incidental protocol, the backend or its private runtime support adapts
that mechanism without leaking the stronger requirement upward.

## Constant evaluation

`constexpr` is a constant-evaluation contract, not an optimization hint.
Generated C++ carries it when accepted Carven semantics require a value or
operation to be usable during constant evaluation. The backend does not infer a
second, target-only constant language merely to decorate private helpers.

Generic runtime facilities may be `constexpr` when their complete operation is
uniformly valid for every admitted instantiation whose underlying operations
are constant-evaluable. This conditional capability belongs in the C++
template implementation, as it does in the standard library; it must not add a
stronger source-type constraint or require recursive semantic capability facts
without a language observer. Header visibility, direct operations, and
`noexcept` expose runtime optimization opportunities independently of
`constexpr`.

## Abstraction cost

Expressing a compile-time abstraction should not introduce runtime machinery
after the compiler has used and discharged it. A compile-time-only fact may
therefore have no generated declaration or source-shaped target analogue.

Observable runtime behavior may require storage, checks, allocation,
indirection, or dispatch. Its baseline is skilled handwritten C++ preserving
the same Carven guarantees, including order, ownership, safety checks, and
control behavior.

Ordinary target optimization remains a downstream C++ responsibility. A
Carven-specific optimization is justified when a source or target requirement
cannot be expressed or recovered downstream, its legality has one authority,
and its benefit warrants the added compiler responsibility. Evidence should
match the claimed benefit; runtime performance, code-size, or compilation-time
claims require representative measurement. Correctness must not depend on a
downstream optimizer performing an optional transformation.

The backend emits optimizer-visible C++ whose static requirements are no
stronger than the verified semantic program. It prefers direct C++ control,
ownership, and value operations and keeps private runtime helpers transparent
to the C++ frontend. Analysis CFGs, SCCs, and fixed-point states do not become a
target control machine. When C++ has no direct structured form, one narrow
synthetic route is preferable to reifying labels, program counters, or control
variants for the whole analysis graph. Avoiding every local label is not an
objective when it would require a larger runtime protocol.

## Reification and materialization

A semantic distinction is reified when runtime execution must still observe
different data or control. It is materialized when that distinction must
persist across a call, join, lifetime, suspension, ABI boundary, or later
observation. Register, stack, and heap placement are later target decisions.

A reified action does not automatically need a source-shaped runtime object.
Structured target control may express a known action directly. A logical place
is warranted when ownership, aliasing, mutation, addressability, foreign
access, or lifetime requires persistent storage identity.

For every runtime representation, a proposal should identify the later
observer, the boundary it crosses, and why direct structured realization is
insufficient.

## Predictable syntax

Parsing should depend on tokens and delimiters rather than name or type lookup.
A source form should retain the same syntactic structure in every scope.

When meanings would otherwise require contextual guessing, syntax should make
the distinction explicit. Accepting a complete construct creates a grammar and
AST responsibility, so parser scaffolding follows accepted semantics rather
than reserving an implementation path for an unsettled feature.

## Safety and interoperability

Safety claims must match enforced analysis. A restriction should name the
guarantee it adds, and an escape boundary should name the guarantees delegated
to its author.

Carven should remain compatible with native C++ libraries, tools, debuggers,
ABIs, and build systems without exposing every C++ mechanism as Carven syntax.
The runtime supplies mechanisms required by accepted semantics; reusable user
capabilities remain outside compiler semantic authority.

## Layered ownership

Language semantics, compiler representations, target realization, runtime
support, artifact materialization, and native build orchestration have distinct
owners. A fact is established once and consumed directly; a later layer does
not reconstruct it from generated names or serialized text.

A persistent representation is justified by a real later consumer. Disposable
analysis projections end after their facts have been committed to the owning
program. Product integration does not by itself grant a library, runtime
helper, or build rule authority over language meaning. An analysis
representation does not prescribe a target representation: graphs,
control-flow blocks, and fixed-point state may establish semantic facts while
structured native C++ realizes them.

Carven remains a source-to-C++ step. Project configuration, dependency
scheduling, C++ compilation, linking, installation, and platform selection are
native build responsibilities.

## Complete slices

A feature includes its complete lifecycle: syntax, semantics, diagnostics,
representation, analysis, lowering, runtime behavior, compatibility,
documentation, and tests. A supported slice may be small, but every accepted
form must close across those responsibilities and unsupported neighboring forms
must be rejected.

## Feature admission

A proposal should answer:

- Which user intent or guarantee is missing?
- What is the normal source form for that intent?
- Which behavior is observable and which mechanism remains selectable?
- What safety, interoperability, runtime, and tooling costs follow?
- Can parsing remain independent of semantic lookup?
- Which semantic distinctions survive to runtime, and where must they be
  materialized?
- Can the behavior be represented, verified, lowered, and tested with one
  semantic authority?

Admission requires a complete answer to these questions; technical feasibility
alone is insufficient.
