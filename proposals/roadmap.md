# Proposal Roadmap

This roadmap records proposed priorities, semantic dependencies, and activation
criteria for deferred work. Individual proposals own open design decisions;
[language semantics](../docs/semantics.md) describes implemented behavior.
The current milestone is v0.1.0; allocation of the proposed work to milestones
remains open.

## Available foundations

| Available foundation | Evidence and planning consequence |
| --- | --- |
| Modules, structs, payload enums, arrays, matching, constants, and range loops | [Language semantics](../docs/semantics.md); enough concrete data and control flow to exercise new abstractions |
| Read/Write/Take, closures and callable views, typed failures | [Language tests](../tests/language/); new receivers and generic values must compose with these contracts |
| Pointer values and local non-null analysis | [Pointer semantics](../docs/semantics.md#pointer-values); native pointers do not establish an owning resource abstraction |
| Owning String, text borrowing, interpolation, and String-bearing nominal failures | [String failure tests](../tests/language/failure_contracts/string.cv); owning text and copyable owning failure payloads already exist |
| Read-only `[T]` slices and storage-borrow propagation | [slice tests](../tests/language/types_and_values/slices.cv) and [type parser](../src/frontend/parse/grammar/type.cpp); concrete input for reusable sequence consumers |
| UTF library and crafts build/test integration | [UTF API](../crafts/carven/std/utf/README.md) and [crafts tests](../tests/crafts/); concrete library APIs for evaluating encapsulation |
| C++ header imports and concrete scalar `import(cpp)`/`export(cpp)` | [C++ boundary](../docs/semantics.md#scalar-function-boundaries); explicit function boundaries still exclude generic and aggregate signatures |

User-defined generic declarations, concepts/impls, ordinary classes, dynamic
class forms, and async remain unimplemented. Named type arguments and builtin
`ptr<T>` syntax do not constitute Carven parametric generics.

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

[Classes](classes.md) `OPEN-01` and `OPEN-02` cover the first scope decisions.
Ordinary classes can be developed independently of dynamic `class(form)`,
erased values, inheritance, and generic dynamic operations.

`UTF8Validator` is a candidate design exercise: its public fields record pending
sequence state, while callers initialize and mutate it through library functions.
An encapsulated validator would exercise factory visibility, Read queries,
Write operations, and private helpers. Its existing `finish` operation checks EOF.

A String-backed builder with a consuming operation could exercise
whole-representation decomposition. Open questions include field disposition,
outstanding borrows, and receiver state on success and failure.

Validation would cover cross-module visibility, valid construction and rejected
external field access, receiver ownership and failure rules, and direct C++
realization without mandatory allocation or dynamic dispatch.

### Generic core: scope decision

[Generics](generics.md) `OPEN-01` currently includes generic C++ boundary
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
- A growable container needs a concrete ownership, mutation, invalidation, and
  allocation-failure contract. Generic syntax alone does not settle a `Vec<T>`
  API. Iterator and operator protocols should follow real consumers.
- [Documentation comments](doc-comments.md) are independent and can be a small
  separate delivery when a documentation artifact is selected. They do not
  block classes or generics.
- The UTF `from_utf8([u8]) -> str` API currently delegates the returned borrow
  to its native provider/caller contract. Compiler-checked return borrowing
  across native boundaries would require a separate interoperation proposal.

## Failure extension edges

The current failure contract and compiler realization are closed.
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
Interprocedural private-call specialization remains deferred until measurement
shows a material cost that supported C++ optimization cannot remove.

## Concurrency track

The [async](async.md), [memory model](memory-model.md), and
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

| Direction | Reactivation evidence |
| --- | --- |
| General pass/dialect or optimization framework | At least two implemented transformations require scheduling or extension machinery that direct SemIRProgram-to-unit-local TargetUnit lowering cannot express cleanly |
| Query system, incremental analysis, or persistent IDs | Measured compilation behavior or an interactive use case requires stable reusable analysis |
| Multi-backend IR | A supported non-C++ backend has concrete semantic and artifact requirements |
| Generated C++ module interfaces | A supported consumer use case requires them; BMI orchestration remains a build-system responsibility |
| Static meta or compile-time generation | Stable generics expose a real consumer and a proposal can define a bounded query and generation model |
| Runtime reflection | A concrete dynamic use case justifies explicit metadata, ownership, and runtime cost |

Deferred work is inactive until its evidence exists; implementation convenience
alone is not a reactivation condition.
