# Proposal Roadmap

This roadmap records proposed priorities, semantic dependencies, and activation
criteria for deferred work. Individual proposals own open design decisions;
`docs/` describes implemented behavior.
The current milestone is v0.1.0; allocation of the proposed work to milestones
remains open.

## Available foundations

Implemented foundations include:

- modules, structs, payload enums, arrays, matching, constants, and range loops;
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
ordinary and dynamic classes, and async remain unimplemented.

## Proposed priorities

Priority is an implementation recommendation, not a semantic dependency.
Ordinary classes and parametric generics can be designed independently.

| Priority | Design slice | Why now | Gate before implementation |
| --- | --- | --- | --- |
| 1 | Ordinary value class, encapsulation, associated factories, Read/Write/Take receivers | Protect real library invariants and make reusable behavior expressible | Close [Classes](classes.md) `OPEN-01` and `OPEN-02` |
| 2 | Parametric functions, structs, and enums; local inference and finite instance identity | Reuse the new slice and nominal-value facilities across element types | Revisit [Generics](generics.md) `OPEN-01` scope, then close `OPEN-02` |
| 3 | Static capabilities, canonical evidence, coherence, and associated types | Let generic algorithms request operations explicitly | Deliver generic core and its definition-site checking first |
| 4 | Small operator capability surface | Reuse the capability machinery for existing operator tokens | Close [Operators](operators.md) identity, carrier, signature, and equality decisions |

Priorities for dynamic ownership, same-thread async, and containers depend on
concrete library or application consumers.

### Ordinary classes: open decisions

Ordinary classes can be designed and delivered independently of dynamic class
forms, erased values, inheritance, and generic dynamic operations.

`UTF8Validator` is a candidate for construction, Read queries, Write operations,
and private helpers. Its public fields currently hold pending sequence state;
library functions initialize and mutate it, and `finish` checks EOF. A
String-backed consuming builder can exercise whole-representation decomposition,
field disposition, borrows, and success/failure availability.

#### Next round actions

1. Use `UTF8Validator` to compare construction, query, mutation, and private-helper
   surfaces. Resolve operation visibility and helper syntax in Classes
   `OPEN-01`.
2. Use a small String-backed consuming builder to resolve `OPEN-02`: whole-object
   decomposition, disposition of every field, outstanding borrows, and receiver
   availability on success and failure.
3. Map the selected operations onto frontend syntax, semantic access and ownership,
   C++ lowering, and craft/native support. Add a runtime primitive only where a
   concrete operation needs one; public library APIs remain in crafts.
4. Once those decisions are closed, implement the minimal ordinary value-class
   slice and validate its accepted and rejected source programs, cross-module
   visibility, lifetimes, and direct C++ output.

The recommended next slice is ordinary classes. Constant execution of class
operations requires separate admission and retained-result decisions.

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
planning, completion/failure boundaries, and compatibility before any runtime replacement.
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

## Concurrency track

The async, [memory model](memory-model.md), and
[threading](threading.md) proposals own distinct semantic authorities. Their
cross-proposal edges are:

```text
classes: owner/shared value forms -------+
                                         +--> memory model --> threading
C++ interoperation: explicit cross-thread use --+

memory model + threading --> cross-thread async
```

Same-thread async has no memory-model or threading dependency. Its next design
work is execution context followed by suspension/borrow/frame rules. Activate
implementation around a concrete operation such as a timer or I/O consumer;
generics and ordinary classes are not blanket semantic prerequisites. Threading can
also exist without async. The classes and C++ interoperation arrows activate
memory-model work only when a concrete cross-thread value or use case exists.

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
