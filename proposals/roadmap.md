# Proposal Roadmap

This roadmap coordinates active Carven proposals. It records dependency order
and the evidence that can reactivate deferred work. It does not define source
semantics, duplicate implemented compiler behavior, or promise release dates.

The current milestone context is v0.1.0. Ordering states logical prerequisites,
not a commitment that every listed direction ships in that milestone.

## Current design frontier

The compiler architecture and failure-effect realization are implemented and
validated foundations. Active proposals advance according to their own
semantic dependencies and readiness.

The next shared prerequisite in the type and interoperation track is a complete
typed `#[cpp]` boundary authority. Documentation comments are independent and
may advance without that authority. Async, the memory model, and threading
follow the dependency edges below.

## Implemented foundations

| Foundation | Stable result | Permanent authority |
| --- | --- | --- |
| Compiler stages | `CompilationRequest -> ParsedBatch -> SemanticProgram -> TargetGenerationPlan -> unit-local TargetUnit -> ArtifactSet` | [`docs/compiler.md`](../docs/compiler.md) and [`docs/backend.md`](../docs/backend.md) |
| Failure contracts | Exact structured failure effects and one private flat Outcome transport | [`docs/semantics.md`](../docs/semantics.md#failure-contracts) and [`docs/backend.md`](../docs/backend.md) |

The [typed-failure effects](typed-failure-effects.md) record preserves the
feature rationale. The permanent documentation owns the implemented compiler
architecture.

## Type system and abstraction track

The preserved order is:

| Order | Design slice | Owner | Unlocks |
| --- | --- | --- | --- |
| 1 | Complete typed `#[cpp]` boundary | Unassigned; authority must be established first | Generic participation and future generic C++ consumption |
| 2 | Generic participation in `#[cpp]` and finite instance expansion | [Generics](generics.md) | A closed generic implementation contract |
| 3 | Parametric generic core and concrete instance identity | [Generics](generics.md) | Static capabilities and reusable generic consumers |
| 4 | Concept, canonical evidence, coherence, and associated-type normalization | [Generics](generics.md) | Operator capabilities and generic library types |
| 5 | Closed operator capability surface | [Operators](operators.md) | Generic operators without unrestricted overload lookup |
| 6 | Ownership and dynamic-value forms | [Classes](classes.md) | Dynamic interfaces and runtime polymorphism |

Generics owns only generic participation in the typed `#[cpp]` boundary; it
does not own the boundary's complete input, output, control, failure, and
lifetime contract.

`Result`, `Option`, alias/distinct types, owning callable values, static meta,
and runtime reflection require dedicated proposals before they become active
roadmap items. Their names alone do not determine their semantics.

The [documentation comments](doc-comments.md) proposal is logically independent
of this chain.

## Failure extension edges

The current failure contract and compiler realization are closed.
Only future features that cross its boundary reactivate design work:

```text
failure-effect private Outcome --> C++ consumer failure adapter
failure-effect semantic facts + async suspension facts --> async completion transport
```

The [C++ consumer](cpp-consumer.md) proposal owns any stable handwritten-C++
surface, even when it adapts a private generated protocol. The
[async](async.md) proposal owns suspension, cancellation, and completion;
failure effects supply only their existing structured semantic facts.

Interprocedural private-call specialization and richer failure-value ownership
remain deferred in [typed-failure-effects.md](typed-failure-effects.md#deferred-work).

## Concurrency track

The [async](async.md), [memory model](memory-model.md), and
[threading](threading.md) proposals own distinct semantic authorities. Their
cross-proposal edges are:

```text
classes: owner/shared value forms -------+
                                         +--> memory model --> threading
C++ consumer: explicit cross-thread use -+

memory model + threading --> cross-thread async
```

Same-thread async has no memory-model or threading dependency. Threading can
also exist without async. The [classes](classes.md) and
[C++ consumer](cpp-consumer.md) arrows activate memory-model work only when
those proposals introduce a concrete cross-thread value or use case.

## Interoperation track

The [C++ consumer](cpp-consumer.md) proposal remains opt-in. Its concrete V1
surface depends on stable canonical identities, but not on completing the
Carven-to-C++ typed `#[cpp]` expression boundary. A future generic consumer
surface depends on both that boundary and the generic instance contract.

Broader ABI stability, precompiled distribution, plugin loading, and open-world
discovery require separate proposals. Public failure mapping is a C++ consumer
decision; adapting a private generated protocol does not make that protocol
stable.

## Deferred infrastructure

| Direction | Reactivation evidence |
| --- | --- |
| General pass/dialect or optimization framework | At least two implemented transformations require scheduling or extension machinery that direct SemanticProgram-to-unit-local TargetUnit lowering cannot express cleanly |
| Query system, incremental analysis, or persistent IDs | Measured compilation behavior or an interactive use case requires stable reusable analysis |
| Multi-backend IR | A supported non-C++ backend has concrete semantic and artifact requirements |
| Generated C++ module interfaces | A supported consumer use case requires them; BMI orchestration remains a build-system responsibility |
| Static meta or compile-time generation | Stable generics expose a real consumer and a proposal can define a bounded query and generation model |
| Runtime reflection | A concrete dynamic use case justifies explicit metadata, ownership, and runtime cost |

Deferred work is inactive until its evidence exists; implementation convenience
alone is not a reactivation condition.

## Maintenance

- Proposal documents own design rationale and unresolved choices.
- Permanent documentation owns implemented behavior.
- This roadmap owns only cross-proposal dependency order and reactivation
  conditions.
- Short-term implementation queues and progress reports belong in task or
  issue tracking rather than this file.
