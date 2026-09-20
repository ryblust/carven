# Cross-thread concurrency

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Cross-thread value admission, shared-state ordering, and thread/synchronization operations
- **Depends on:** Concrete ownership or cross-thread APIs for the future model

## Summary

This proposal owns cross-thread execution from value admission and memory ordering
through the operations that establish those guarantees. No memory model or thread
API is selected. The design starts from a concrete scoped-thread, shared-state,
or message-passing use case.

Memory rules and operation contracts are separate sections of one design:
operations consume the value, data-race, and happens-before rules and identify
which rules are needed. [Async](async.md) owns suspension and structured operation
lifetime. Same-thread async is independent; migration requires both contracts.

## Context

Carven currently has no thread, atomic, lock, shared-ownership, or synchronization
source form. Permanent language documentation defines no cross-thread memory
model, data-race freedom, thread-safety, happens-before, Send/Sync-like capability,
or cross-thread task-lifetime guarantee.

Write access is non-owning and nonexclusive: a mutable owner can be passed to
multiple Write parameters in one call. It therefore supplies no unique mutable
alias proof. A future model could add concurrency-specific capabilities,
restrict cross-thread types, strengthen access in an explicit concurrent context,
require runtime synchronization, or use explicit C++ boundary obligations.
No choice is selected.

The model must distinguish these properties:

- An immutable binding does not establish recursive cross-thread shareability.
- Copyable representation and valid lifetime do not establish thread safety.
- Shared lifetime does not synchronize access.
- Nonexclusive Write does not define data races as valid.
- Same-thread suspension does not establish sharing or physical parallelism.
- Generic evidence and coherence do not supply runtime synchronization.

C++ source-fragment authors, import providers, and export callers already own
control, mutation, lifetime, concurrency, and undefined-behavior obligations
outside Carven's modeled boundary. The downstream toolchain checks target and
ABI validity. Compiling or concurrently calling generated C++ does not add a
Carven semantic guarantee. Any promised reentrancy or thread-compatible value
shape requires an explicit interoperation contract.

Owner, borrowed, shared, nullable, and erased values, allocators, dispatch tables,
and failure/control carriers all affect cross-thread validity. The
[async proposal](async.md) needs this model when operations, frames, captures,
continuations, or completions can migrate. The thread and synchronization operations below consume its
value-capability, data-race, and ordering rules. A non-async thread, channel,
shared owner, or C++ integration use case may also activate this work.

## Design scope

Define the value, data-race, and ordering contracts required by a concrete
cross-thread operation. Write remains nonexclusive unless a selected contract
strengthens it.

No atomic or lock API is selected. Async owns suspension lifetime and scheduling.
This proposal does not select
capability names or derivation policy, a race detector, borrow checker, lifetime
annotations, or a C++ library mechanism. Future features require source semantics,
compiler facts, diagnostics, lowering, interoperation, tests, and documentation.

## Memory rules

**Next discussion:** `OPEN-02`, when a concrete shared-state API enters design.

### OPEN-02 — Where do shared-state and data-race guarantees apply?

- **Status:** Blocked
- **Activation condition:** An owner/shared/nullable design or shared-state API
  enters active design.
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
- **Depends on:** `OPEN-02`, `OPEN-03`, and a concrete synchronization operation
- **Activation condition:** An atomic, lock, channel, thread-completion, or
  cross-thread task-completion operation enters active design.
- **Question:** Define synchronization and observable ordering.
- **Constraints:** Different operation families may have different contracts.
  A target without lock-free support must preserve promised behavior.
- **Options:** Select candidates with the first operation and use case.
- **Closure condition:** Specify synchronization relations, ordering, target
  lowering, and explicit C++ interoperation obligations.

## Implementation

A future feature must connect a concrete movement, sharing, or synchronization
operation to semantic guarantees, diagnostics, SemIRProgram facts, target/runtime
support, C++ boundary obligations, tests, and permanent documentation.

## Validation

Selected features need accepted and rejected source programs,
diagnostics, happens-before and data-race cases, generated C++ compilation,
linking and execution, boundary tests, and relevant resource measurements.

## Thread and synchronization operations

The following slices are deferred until a concrete consumer selects the required
memory rules. They can be delivered independently. A complete threading library,
work stealing, parallel algorithms, and a production executor are outside this
scope. C++ mechanisms implement the selected source contract.

### DEFER-01 — Thread lifecycle

- **Reason deferred:** Entry-value capability, data-race validity, and completion
  synchronization are undefined.
- **Depends on:** The memory model and a thread-creation use case
- **Reactivation condition:** A program requires a scoped thread or equivalent
  cross-thread execution.

Decide handle ownership or sharing, entry-callable and capture capabilities,
join/detach and structured/process shutdown, typed failure transport, and C++
thread-local state and callback obligations.

### DEFER-02 — Blocking synchronization

- **Reason deferred:** Guard/access interactions and happens-before need a memory
  model contract.
- **Depends on:** The memory model and a shared-state use case
- **Reactivation condition:** A program needs a blocking mutex, condition,
  semaphore, or equivalent primitive.

Define guard interaction with Read/Write, lock/unlock guarantees, poisoning,
failure, cancellation, and early return. Condition waits additionally need guard
release/reacquisition, spurious-wakeup, and predicate rules. Define whether async
contexts admit blocking operations and how misuse is diagnosed.

### DEFER-03 — Atomics

- **Reason deferred:** Payload admission, ordering vocabulary, and target fallback
  depend on the memory model.
- **Depends on:** The memory model and an atomic use case
- **Reactivation condition:** A shared-state API needs an operation that a narrower
  abstraction cannot express.

Choose a type family, capability, or intrinsic-operation family. Define payloads,
orders and defaults, compare/exchange success and failure ordering, observable
fallback when lock-free implementation is unavailable, C++ ABI, and platform
lowering.

### DEFER-04 — Channels and message passing

- **Reason deferred:** Capacity, movement, closure, and synchronization require
  concrete value capabilities and happens-before rules.
- **Depends on:** The memory model and a producer/consumer use case
- **Reactivation condition:** A program requires cross-thread communication.
  A channel may provide the first narrow slice by reducing shared mutable state.

Decide whether channels are library abstractions or language/runtime primitives.
Define ownership transfer on send, availability on receive, bounded capacity,
blocking, closure, and failure. Awaitable channels require explicit integration
with async semantics.

## Operation validation

For each selected operation, validate thread and capture lifetime on success,
failure, and shutdown; accepted and rejected cross-thread values; synchronization
and ordering; target fallback and C++ obligations; and ownership without orphaned
work or implicit detach. Blocking-in-async behavior requires an explicit async
integration contract. Validate generated C++ by compilation and execution.
