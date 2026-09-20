# Proposal Roadmap

This roadmap records proposed priorities, semantic dependencies, and activation
criteria for deferred work. Individual proposals own open design decisions;
`docs/` describes implemented behavior.
The current milestone is v0.1.0; allocation of the proposed work to milestones
remains open.

## Available foundations

Implemented foundations include:

- modules, structs, Read/Write/Take value classes, payload enums, arrays, matching, constants, and range loops;
- Read/Write/Take, closures and callable views, typed failures, pointer values,
  and local non-null analysis;
- owning String, tracked text borrows, interpolation, and copyable nominal
  failures containing String or borrowed text;
- builtin interpolation precomputation and explicit formatting/output paths;
- constant functions, static text, struct and fixed-array execution, and frozen
  `[T]` results with preserved nominal and field types;
- read-only slices, storage-borrow propagation, and UTF crafts with build/test integration;
- header imports and concrete scalar `import(cpp)`/`export(cpp)` boundaries.

These provide consumers and constraints for new abstractions. Native pointers
do not establish owning resources, and named type arguments or builtin `ptr<T>`
do not supply user-defined generics. Generic declarations, concepts/impls,
dynamic classes, and async remain unimplemented.

## Current consolidation

Ordinary classes, Read/Write/Take receivers, and owning field projection are
implemented. `UTF8Validator` uses private representation and checked operations;
String-backed builders exercise consuming calls and field delivery.

The current engineering focus is the handoff from checked semantics to C++:

- Native result queries and executed expressions preserve the same access and
  constant-value requirements.
- Operand storage serves source order, backing lifetimes, and cleanup obligations.
- Direct display uses C++ type classification for native scalar results without
  invoking user formatting protocols.

Validation covers accepted and rejected operations, observable evaluation order,
and resource lifetime.

## Proposed capabilities

| Capability | Consumer | Design gate |
| --- | --- | --- |
| Parametric functions, structs, and enums | Type-safe reusable values and algorithms | [Generics](generics.md) scope and finite instance rules |
| Static capabilities and associated types | Generic algorithms requiring explicit operations | Definition-site checking and coherent evidence |
| Operator capabilities | User-defined operations for existing tokens | [Operators](operators.md) signature and result rules |
| Multi-field consuming decomposition | Independent owners extracted from one class | [Ownership contract below](#multi-field-consuming-decomposition) |

Generic implementation is deferred while the current contracts are consolidated.
Class constant execution, generic classes, dynamic ownership, and async retain
their own design and admission requirements.

### Generic core: scope decision

Generics `OPEN-01` currently includes generic C++ boundary
participation, which requires a representative use case. One proposed scope is
to deliver generics within the closed Carven compilation first, retaining the
existing concrete scalar C++ boundaries. This choice remains open in the owning
proposal.

`OPEN-02` covers finite expansion, including recursion, growing arguments,
by-value storage cycles, and compiler budgets. Candidate validation examples are
`identity<T>`, a transparent value holder, a payload enum, and a function passing
through `[T]`. Definition-site checking needs to cover copying, Take, stored
borrows, and failures.

### Multi-field consuming decomposition

Single-field `(&&owner).field` delivery is implemented. Multi-field extraction
remains a separate ownership design question. A consumer needing two independent
field owners must establish whole-representation extraction, with the original
owner unavailable, exactly-once field evaluation, and deterministic disposition
of every field on success, failure, and rejected patterns. No usable partially
moved object remains. Select syntax using a concrete `build`, `finish`, or
`into_*` operation before implementation. Dynamic values are not a prerequisite.

### Library consumers and independent work

- `Option<T>` is a candidate early generic-enum consumer. Public Option/Result
  APIs need their own proposal; Result must explain explicit failure capture
  and its relationship to the existing typed control effect.
- The [growable-container follow-up](constant-storage.md#follow-up-growable-library-containers)
  records the `Vector<T>` candidate's dependencies, contract decisions, and
  validation. Iterator and operator protocols should follow real consumers.
- [Documentation comments](doc-comments.md) are independent and can be a small
  separate delivery when a documentation artifact is selected. They do not
  block classes or generics.
- The current UTF craft uses checked Carven
  text borrowing. Broader return-borrow contracts for native calls need a concrete
  interoperation use case and their own design.

## Text composition and library storage

Current formatting, output, constant functions, compound execution, and static
slices are implemented foundations described in `docs/`. Remaining
[formatting and output composition](formatting.md) work concerns broader capacity
planning and completion/failure boundaries.
These candidates do not depend on classes or generics.

Constant library storage requires ordinary generic and
encapsulated declarations, admitted storage operations, and a valid retained
result. Its growable-container follow-up
is unimplemented; fixed arrays and struct execution do not complete that scope.
Additional operation consumers
need concrete algorithms and their own contracts.

Type computation, structural queries, and declaration generation remain separate
capabilities that require their own source contracts and concrete consumers.

## Failure extension edges

The current copyable nominal failure contract is implemented.
Features that extend their boundary require the following design work:

```text
failure-effect private Outcome --> C++ interoperation failure contract
failure-effect semantic facts + async suspension facts --> async completion transport
```

Any public C++ failure mapping must extend the C++ interoperation contract, even when
it adapts a private generated protocol. The [async](async.md) proposal owns
suspension, cancellation, and completion; failure effects supply only their
existing structured semantic facts.

Copyable nominal failures can already contain owning String values and tracked
borrowed views. [Failure value](failure-value.md) concerns extensions beyond
that admitted category, such as move-only or managed payloads.

## Dynamic values and concurrency

[Dynamic values](dynamic-values.md) owns erased holding forms, nominal conformance,
and dispatch. Ordinary class implementation is complete within its documented
scope. Dynamic ownership begins with a concrete API; it does not block static
generic declarations or ordinary consuming operations.

[Concurrency](concurrency.md) owns cross-thread value admission, shared state,
data races, ordering, and the thread/synchronization operations that use them.
A concrete scoped thread or message-passing API selects the first slice.

[Async](async.md) independently owns suspension, cancellation, and structured
operation lifetime. Its next decisions are execution context and suspension/
borrow/frame admission. A timer or I/O consumer can activate same-thread work.
Migration and cross-thread completion additionally require concurrency contracts.

## C++ interoperation track

C++ interoperation is opt-in through header imports, top-level C++ source
fragments, and explicit `import(cpp)` and `export(cpp)` declarations. Explicit
function boundaries admit concrete scalar signatures. Generic provider or façade
surfaces require a concrete use case and a generic instance contract.

Broader ABI stability, precompiled distribution, plugin loading, and open-world
discovery require separate proposals. Public failure mapping is a C++
interoperation decision; adapting a private generated protocol does not make
that protocol stable.

## Deferred infrastructure

Each direction needs the following evidence before design or implementation.

| Direction | Reactivation evidence |
| --- | --- |
| Query system, incremental analysis, or persistent IDs | Measured compilation behavior or an interactive use case requires stable reusable analysis |
| Generated C++ module interfaces | A supported consumer use case requires them; BMI orchestration remains a build-system responsibility |
| Reflection and declaration generation | A concrete consumer and a proposal define the required query surface and bounded generation model |
| Runtime reflection | A concrete dynamic use case justifies explicit metadata, ownership, and runtime cost |

Deferred work is inactive until its evidence exists; implementation convenience
alone is not a reactivation condition.
