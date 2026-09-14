# Threads, synchronization, and atomics

- **Status:** Deferred — reactivate when the memory model and a cross-thread API
  provide concrete contracts
- **Implementation:** Not started
- **Scope:** Thread lifecycle, blocking synchronization, atomics, and channels
- **Depends on:** [Memory model](memory-model.md)

## Summary

This proposal holds four deferred directions: thread lifecycle (`DEFER-01`),
blocking synchronization (`DEFER-02`), atomics (`DEFER-03`), and channels
(`DEFER-04`). Carven has no corresponding source forms, and no API is selected.
Each direction needs defined value movement, data-race, and happens-before rules.

Threads can exist independently of async. Cross-thread async executors need both
sets of contracts; same-thread async does not depend on this proposal.

## Context

Current Write access is nonexclusive, immutable bindings do not establish
recursive thread safety, and shared lifetime does not synchronize access.
Concurrent use by C++ callers does not establish a Carven thread guarantee.

Threading defines ordinary source ownership, shutdown, synchronization, failure,
and cancellation. C++ threads, callbacks, atomics, and locks can implement a
selected contract. Lock poisoning, default memory order, and blocking in async
contexts need explicit source decisions.

Async integration is required when tasks, continuations, captures, or completions
migrate, or when blocking primitives are exposed within async contexts. Operation
frames and structured async lifetime remain in the async proposal.

## Goals and non-goals

Start with a concrete scoped-thread or message-passing use case. Define accountable
thread lifetime, entry-callable and capture admission, synchronization, failures,
C++ obligations, and target limitations for that use case.

The initial scope does not include a complete standard-library thread API,
work stealing, parallel algorithms, an actor runtime, or a production executor.
Mutexes, atomics, channels, and cross-thread async can be activated independently.

## Deferred work

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

## Implementation

Implementation waits for the relevant memory-model contract, selected consumer,
and reactivation condition. Deliver source forms, value/callable admission,
ownership and shutdown, semantic facts, ordering edges, lowering, C++ boundaries,
diagnostics, tests, and documentation for that slice. Add representations as the
selected operations require them.

## Validation

A reactivated slice needs source examples covering:

- thread and capture lifetime on success, failure, and shutdown;
- accepted and rejected cross-thread values with precise diagnostics;
- promised happens-before and data-race behavior;
- target fallback and C++ interoperation obligations;
- explicit ownership transfer, with no orphaned work or implicit detach;
- blocking-in-async behavior where applicable;
- generated C++ compilation, linking, and execution on supported targets.
