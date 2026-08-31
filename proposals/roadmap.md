# Proposal Roadmap

This roadmap coordinates active Carven proposals. It records dependency order
and the evidence that can reactivate deferred work. It does not define source
semantics, duplicate implemented compiler behavior, or promise release dates.

The current milestone context is v0.1.0. Ordering states logical prerequisites,
not a commitment that every listed direction ships in that milestone.

## Current design frontier

Compiler architecture, target ownership, and failure-effect realization are
implemented foundations. Active proposals advance according to their semantic
dependencies and readiness.

The next shared prerequisite in the type and interoperation track is the
concrete scalar boundary owned by the [C++ interop](cpp-interop.md) proposal. It
replaces expression/statement typed `#[cpp]` with top-level companion source
plus explicit `import(cpp)`/`export(cpp)` contracts. Documentation comments are
independent. Async, the memory model, and threading follow the dependency edges
below.

## Type system and abstraction track

The preserved order is:

| Order | Design slice | Owner | Unlocks |
| --- | --- | --- | --- |
| 1 | Complete concrete C++ scalar interop boundary | [C++ interop](cpp-interop.md) | Explicit provider/consumer contracts and a replacement for inline typed `#[cpp]` |
| 2 | Generic participation in typed adapters and finite instance expansion | [Generics](generics.md) | A closed generic implementation contract |
| 3 | Parametric generic core and concrete instance identity | [Generics](generics.md) | Static capabilities and reusable generic consumers |
| 4 | Concept, canonical evidence, coherence, and associated-type normalization | [Generics](generics.md) | Operator capabilities and generic library types |
| 5 | Closed operator capability surface | [Operators](operators.md) | Generic operators without unrestricted overload lookup |
| 6 | Ownership and dynamic-value forms | [Classes](classes.md) | Dynamic interfaces and runtime polymorphism |

Generics owns only future generic participation in typed adapters; it does not
own the interop boundary's complete input, output, control, failure, and
lifetime contract. Its remaining typed-`#[cpp]` terminology must be revised to
the explicit adapter model before that slice can activate.

`Result`, `Option`, alias/distinct types, owning callable values, static meta,
and runtime reflection require dedicated proposals before they become active
roadmap items. Their names alone do not determine their semantics.

The [documentation comments](doc-comments.md) proposal is logically independent
of this chain.

## Failure extension edges

The current failure contract and compiler realization are closed.
Only future features that cross its boundary reactivate design work:

```text
failure-effect private Outcome --> C++ interop failure adapter
failure-effect semantic facts + async suspension facts --> async completion transport
```

The [C++ interop](cpp-interop.md) proposal owns any stable handwritten-C++
surface, even when it adapts a private generated protocol. The
[async](async.md) proposal owns suspension, cancellation, and completion;
failure effects supply only their existing structured semantic facts.

Richer failure-value ownership is explored in
[failure-value.md](failure-value.md).
Interprocedural private-call specialization remains deferred until measurement
shows a material cost that supported C++ optimization cannot remove.

## Concurrency track

The [async](async.md), [memory model](memory-model.md), and
[threading](threading.md) proposals own distinct semantic authorities. Their
cross-proposal edges are:

```text
classes: owner/shared value forms -------+
                                         +--> memory model --> threading
C++ interop: explicit cross-thread use --+

memory model + threading --> cross-thread async
```

Same-thread async has no memory-model or threading dependency. Threading can
also exist without async. The [classes](classes.md) and
[C++ interop](cpp-interop.md) arrows activate memory-model work only when
those proposals introduce a concrete cross-thread value or use case.

## Interoperation track

The [C++ interop](cpp-interop.md) proposal remains opt-in. Its concrete scalar
scope covers top-level companion source, strict `import(cpp)`, and scalar
`export(cpp)`.
Future generic provider or consumer surfaces depend on both that boundary and
the generic instance contract.

Broader ABI stability, precompiled distribution, plugin loading, and open-world
discovery require separate proposals. Public failure mapping is a C++ interop
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
