# Proposals

`proposals/` is Carven's design workspace and decision archive. A proposal
separates current repository facts, settled design, open questions, deferred
directions, implementation state, and validation. It does not define current
language or compiler behavior.

## Document roles

| Location | Role |
| --- | --- |
| `docs/` | Current project contracts, repository rules, and lasting design criteria |
| `notes/` | Educational mental models and research synthesis |
| `proposals/` | Design work and retained decision records |
| `proposals/roadmap.md` | Ordering and dependencies across proposal domains |

A proposal may summarize current behavior so that it can be read on its own.
The implementation and permanent documentation own current repository facts.
After implementation, validation, and documentation handoff, proposed behavior
becomes Carven's current contract.

## Flat layout

The workspace uses a flat layout. Each design domain is represented by as few
root-level Markdown files as its independent decisions require. Workspace
guidance lives in `README.md`, `TEMPLATE.md`, and `roadmap.md`.

```text
proposals/
    README.md
    TEMPLATE.md
    roadmap.md
    <design-domain>.md
```

`README.md`, `TEMPLATE.md`, and `roadmap.md` coordinate the workspace. Other
Markdown files own one proposal domain or preserve one completed decision
record.

Use the shortest unambiguous filename for the domain. Keep syntax, semantics,
compiler facts, lowering, and backend choices together when they implement one
design authority. Split a domain only when each part can be decided and
implemented independently; record the dependency in the roadmap. Reusable
research may also be summarized in `notes/`, while the proposal remains
self-contained.

## Proposal structure

Active proposals start from [the template](TEMPLATE.md), which gives the
recommended reading order and metadata. Omit a section when it has no
information for the reader. A completed proposal may become a shorter decision
record after its current contract has moved to `docs/`.

Each section records only information available at the proposal's current
maturity. Do not invent alternatives or delivery work to fill a template.

`Decision record` contains consequential settled choices and rationale under
stable identifiers. `Open decisions` orders unresolved questions by dependency
and identifies the next unblocked question. `Deferred work` preserves valid but
inactive directions under stable `DEFER-*` identifiers. `Design` remains the
readable specification; the decision record may be a compact index into it.

One open or deferred identifier covers one dependency set and activation
condition. Top-level metadata describes the whole proposal; slice-specific
status and dependencies stay with the slice. `Design` records the selected
behavior, while `Implementation` records delivery.

Change the shared structure when repeated use shows that a kind of information
has no clear home or that a section impairs readability.

## Lifecycle

Proposal status describes design maturity independently of implementation
progress:

- **Exploration** — the problem, boundary, and viable choices are being
  established. `Design` may explicitly point to the first open decision.
- **Draft** — a coherent design exists, or the domain has mixed-maturity
  slices, while one or more required active choices remain open.
- **Accepted** — all required active design decisions are closed and
  implementation may proceed. Valid inactive directions may remain in
  `Deferred work`.
- **Deferred** — work is paused with a recorded reason and reactivation
  condition. The document preserves the current design and open frontier.
- **Superseded** — another proposal replaces the design and is named in the
  status metadata.

Implementation progress is recorded separately as `Not started`, `In
progress`, `Partial`, `Complete`, or `Not applicable`.

An active proposal remains here while it has unresolved design work or accepted
scope awaiting handoff. Once it is fully implemented and its stable facts are
recorded in `docs/`, it leaves the active proposal set. Git history normally
preserves the rationale; retain an archived proposal when that rationale has
continuing project value.

## Implemented proposals

| Domain | Proposal | Permanent contract |
| --- | --- | --- |
| Failure contracts | [Typed Failure Effects](typed-failure-effects.md) | [Failure-contract semantics](../docs/semantics.md#failure-contracts) and [`docs/backend.md`](../docs/backend.md) |

## Current proposals

| Domain | Proposal | Owns |
| --- | --- | --- |
| Generics | [泛型与静态约束](generics.md) | Generic instances, concepts, evidence, and representation choices |
| Types and abstraction | [`struct`、`class` 形式与动态多态](classes.md) | Nominal data roles, class forms, receivers, and dynamic abstraction |
| Operators | [运算符能力](operators.md) | Closed operator capability design |
| Documentation tooling | [Documentation comments](doc-comments.md) | Documentation attachment and tooling input |
| C++ interoperability | [C++ 互操作契约](cpp-interop.md) | C++ companion source and opt-in typed import/export surfaces |
| Async | [异步编程](async.md) | Async operations, structured ownership, composition, cancellation, and realization |
| Memory model | [内存模型与共享状态](memory-model.md) | Cross-thread visibility, data races, and shared-state guarantees |
| Threading | [线程、同步与原子操作](threading.md) | Thread lifecycle, synchronization, atomics, and channels |

The [proposal roadmap](roadmap.md) coordinates ordering and reactivation
conditions, while each proposal owns its domain semantics.
