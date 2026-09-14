# Memory model and shared state

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Cross-thread values, shared state, data races, and happens-before
- **Depends on:** Concrete ownership or cross-thread APIs for the future model

## Summary

This proposal defines the questions for a future cross-thread memory model:
value movement and sharing, valid shared-state access, data races,
synchronization, and responsibility at C++ boundaries. No model is selected.

`OPEN-01` asks how permanent documentation should state the current guarantee
boundary. `OPEN-02` through `OPEN-04` wait for concrete values and operations.
Thread APIs, atomics, locks, and async operations have separate design scopes.
These dependencies constrain future ownership and concurrency work without
committing them to the v0.1.0 milestone.

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
continuations, or completions can migrate. [Threading](threading.md) consumes its
value-capability, data-race, and ordering rules. A non-async thread, channel,
shared owner, or C++ integration use case may also activate this work.

## Goals and non-goals

Define current guarantee boundaries and the decisions required for future
cross-thread values. Preserve nonexclusive Write unless an explicit new contract
strengthens it, and state responsibility at each C++ boundary.

This proposal does not select atomic or lock APIs, async lifetime or scheduling,
capability names or derivation policy, a race detector, borrow checker, lifetime
annotations, or a C++ library mechanism. Future features require source semantics,
compiler facts, diagnostics, lowering, interoperation, tests, and documentation.

## Open decisions

**Next discussion:** `OPEN-01`

### OPEN-01 — How should permanent documentation state the current boundary?

- **Status:** Active
- **Question:** Decide whether to add an explicit current-guarantee statement.
- **Constraints:** Describe current facts, preserve Write and C++ responsibility
  boundaries, and leave future model selection open.
- **Options:** The proposed statement says Carven defines no cross-thread memory
  model or thread creation, sharing, synchronization, or atomic source forms,
  and proves no data-race freedom. Integration authors own cross-thread calling,
  sharing, synchronization, referent lifetime, and target data-race validity;
  fragment/provider/export-caller concurrency follows existing boundary duties.
  This wording is not yet accepted.
- **Closure condition:** Review language and lowering documentation together,
  then accept or revise a permanent statement.

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

After `OPEN-01`, a documentation-only change can publish the accepted current
boundary. It adds no syntax, IR, runtime, or tests.

A future feature must connect a concrete movement, sharing, or synchronization
operation to semantic guarantees, diagnostics, SemIRProgram facts, target/runtime
support, C++ boundary obligations, tests, and permanent documentation.

## Validation

For `OPEN-01`, check consistency with Write/access, callable-view and string-view
lifetimes, and C++ boundary responsibility. Preserve the distinction between
integration obligations and source guarantees. The roadmap must expose relevant
ownership, dynamic-value, and async dependencies without promising release scope
or dates.

Future features additionally need accepted and rejected source programs,
diagnostics, happens-before and data-race cases, generated C++ compilation,
linking and execution, boundary tests, and relevant resource measurements.
