# Cross-thread concurrency

- **Status:** Exploration
- **Scope:** Cross-thread value admission, shared-state ordering, and thread/synchronization operations

## Summary

This proposal owns cross-thread execution from value admission and memory ordering
through the operations that establish those guarantees. No memory model or thread
API is selected. The design starts from a concrete scoped-thread, shared-state,
or message-passing use case.

Memory rules and operation contracts are separate sections of one design:
operations consume the value, data-race, and happens-before rules and identify
which rules are needed. Suspension and structured operation lifetime have separate
contracts. Same-thread async is independent; migration requires both contracts.

## Context

Carven currently has no thread, atomic, lock, shared-ownership, or synchronization
source form. Permanent language documentation defines no cross-thread memory
model, data-race freedom, thread-safety, happens-before, Send/Sync-like capability,
or cross-thread task-lifetime guarantee.

`std::async` composition, timeout operations, and `std::time` timers
execute through the same-thread runtime. Their scheduling and cancellation
contracts are implemented, but no task migration or cross-thread value admission
is defined. Compiler-stage parallelism is a separate implementation experiment.

`std::channel::ByteChannel` exchanges owned bytes on one driver thread.
`std::build` overlaps native compiler subprocesses through `std::process`.
Hosting permits typed request completion and event posting from native threads,
while session control remains serialized. These boundaries do not define
cross-thread execution of Carven code or a general source memory model.

Write access is non-owning and nonexclusive: a mutable owner can be passed to
multiple Write parameters in one call. Evaluate concurrency-specific capabilities,
cross-thread type restrictions, stronger access in concurrent contexts, runtime
synchronization, or explicit C++ boundary obligations. No model is selected.

Check cross-thread shareability recursively through contained values and referents,
independently of binding immutability or copyability. Define movement, shared
access, referent lifetime, and synchronization separately.
Require the selected synchronization rule for conflicting accesses, including
through shared owners or nonexclusive Write aliases. Task migration must satisfy
both cross-thread admission and async lifetime rules.

C++ source-fragment authors, import providers, and export callers own
control, mutation, lifetime, concurrency, and undefined-behavior obligations
outside Carven's modeled boundary. The downstream toolchain checks target and
ABI validity. Specify reentrancy and thread-compatible value shapes at the native
boundary when the selected consumer requires them.

Owner, borrowed, shared, nullable, and erased values, allocators, dispatch tables,
and failure/control carriers all affect cross-thread validity. Migrating operations,
frames, captures, continuations, or completions need these rules. Thread and
synchronization operations consume the value-capability, data-race, and ordering
rules. A non-async thread, channel, shared owner, or C++ integration use case may
also activate this work.

## Design

Define the value, data-race, and ordering contracts required by a concrete
cross-thread operation. Write remains nonexclusive unless a selected contract
strengthens it.

Select capability evidence and native mechanisms with the first operation.
Deliver its source semantics, compiler facts, diagnostics, lowering, interoperation,
tests, and documentation together.

### Ownership and first consumer

Compare scoped execution and ownership-transferring message passing as initial
consumers. Each selects its admitted values, referent lifetimes, conflicting-access
rules, and synchronization relations. Shared mutable state and task migration
add their own requirements when selected.

Compiler knowledge records and checks cross-thread admission through values and
referents under the selected evidence, lifetime, and access rules. Bounded builtin
thread or channel operations can establish handoff and completion contracts. Runtime Craft
supplies shared thread and synchronization mechanisms; Standard Craft owns public
declarations, channel/buffering policy, higher-level algorithms, and native
adapters where they express the complete contract.

Compare compiler-derived evidence and explicit native-provider obligations for
the first operation. Source impl evidence needs a contract for how its claims
are established; public thread-safety concepts are selected when a source API
needs to express these requirements. The operation requires its memory and
ordering rules, independently of a general capability language, Shared owner,
or executor framework. Define blocking or awaitable behavior for that operation;
async syntax alone supplies no cross-thread guarantee.

## Open decisions

### OPEN-02 — Where do shared-state and data-race guarantees apply?

- **Status:** Blocked
- **Activation condition:** A concrete cross-thread consumer needs shared state.
- **Question:** Define valid aliasing and synchronization in ordinary source and
  the handoff to C++ boundary responsibility.
- **Constraints:** Write remains nonexclusive unless explicitly strengthened;
  opaque-boundary obligations stay explicit.
- **Options:** Type/capability restrictions, stronger access in concurrent
  contexts, runtime synchronization, or explicit C++ responsibility. Guarantees
  and costs remain open.
- **Closure condition:** Use a concrete shared-state example to define source
  guarantees, aliasing proofs, and fragment/provider/export-caller duties.

### OPEN-03 — Which values may cross threads, and who supplies evidence?

- **Status:** Blocked
- **Activation condition:** A concrete owner, borrowed, shared, dynamic, or
  operation value needs to move or be shared across a thread boundary.
- **Question:** Define movement, sharing, and referent-lifetime requirements.
- **Constraints:** Immutability, Write, generic conformance, and generated layout
  alone imply no thread-safety guarantee.
- **Options:** Compiler-derived capability, source impl evidence, C++ boundary
  assertions, or a combination under explicit coherence rules.
- **Closure condition:** Specify admitted values, evidence ownership, diagnostics,
  and boundary assertions for a concrete consumer.

### OPEN-04 — Which operations establish happens-before?

- **Status:** Blocked
- **Depends on:** Value admission from `OPEN-03` and a concrete synchronization
  operation; `OPEN-02` supplies conflicting-access rules when shared state is exposed
- **Activation condition:** An atomic, lock, channel, thread-completion, or
  cross-thread task-completion operation enters active design.
- **Question:** Define synchronization and observable ordering.
- **Constraints:** Different operation families may have different contracts.
  A target without lock-free support must preserve promised behavior.
- **Options:** Select candidates with the first operation and use case.
- **Closure condition:** Specify synchronization relations, ordering, target
  lowering, and explicit C++ interoperation obligations.

## Deferred work

The following slices are deferred until a concrete consumer selects the required
memory rules. They can be delivered independently. A complete threading library,
work stealing, parallel algorithms, and a production executor are outside this
scope. C++ mechanisms implement the selected source contract.

### DEFER-01 — Thread lifecycle

- **Reason deferred:** Entry-value capability, data-race validity, and completion
  synchronization are undefined.
- **Depends on:** Entry-value admission, referent lifetime, conflicting-access
  rules, and completion ordering for the selected thread operation
- **Reactivation condition:** A program requires a scoped thread or equivalent
  cross-thread execution.

Decide handle ownership or sharing, entry-callable and capture capabilities,
join/detach and structured/process shutdown, typed failure transport, and C++
thread-local state and callback obligations.

### DEFER-02 — Blocking synchronization

- **Reason deferred:** Guard/access interactions and happens-before need a memory
  model contract.
- **Depends on:** Shared-state access rules and ordering for the selected
  synchronization operation
- **Reactivation condition:** A program needs a blocking mutex, condition,
  semaphore, or equivalent primitive.

Define guard interaction with Read/Write, lock/unlock guarantees, poisoning,
failure, cancellation, and early return. Condition waits additionally need guard
release/reacquisition, spurious-wakeup, and predicate rules. Define whether async
contexts admit blocking operations and how misuse is diagnosed.

### DEFER-03 — Atomics

- **Reason deferred:** Payload admission, ordering vocabulary, and target fallback
  depend on the memory model.
- **Depends on:** Payload admission, conflicting-access rules, and ordering for
  the selected atomic operation
- **Reactivation condition:** A shared-state API needs an operation that a narrower
  abstraction cannot express.

Choose a type family, capability, or intrinsic-operation family. Define payloads,
orders and defaults, compare/exchange success and failure ordering, observable
fallback when lock-free implementation is unavailable, C++ ABI, and platform
lowering.

### DEFER-04 — Cross-thread channels and message passing

- **Reason deferred:** Capacity, movement, closure, and synchronization require
  concrete value capabilities and happens-before rules.
- **Depends on:** Sent-value admission, ownership handoff, referent lifetime,
  and send/receive/closure ordering for the selected channel
- **Reactivation condition:** A program requires cross-thread communication.
  A channel may provide the first narrow slice by reducing shared mutable state.

Decide whether channels are library abstractions or language/runtime primitives.
Define ownership transfer on send, availability on receive, bounded capacity,
blocking, closure, and failure. Awaitable channels require explicit integration
with async semantics.

## Implementation

For the selected movement, sharing, or synchronization operation, publish the
required semantic facts and implement its diagnostics, target/runtime support,
native boundary checks, tests, and documentation.

## Validation

Validate accepted and rejected cross-thread values; thread and capture lifetime
on success, failure, and shutdown; data races, synchronization, and ordering;
target fallback and C++ boundary obligations; and ownership without orphaned
work or implicit detach. Blocking operations in async contexts need an explicit
integration contract. Check diagnostics, compile, link, and execute generated
C++, and measure resources relevant to the selected operation.
