# Uninitialized storage and construction

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** Construction destinations, partial initialization, cleanup, native object lifetime, and caller validity obligations

## Summary

Native output adapters, buffers, containers, and memory pools need storage that
can be filled or constructed without first default-constructing its contents.
Ordinary values remain initialized values; a destination awaiting construction
needs a contract for access, completion, and cleanup.

An owning raw slot with borrowed construction access is one candidate. Library
adapters could manage progress and cleanup. Names such as `Uninit<T>`, `construct`,
and `assume_init` are conceptual. The public surface and checking rules remain open.
Evaluate candidates with concrete synchronous native adapters.

The intended result is direct construction in final storage, preserving evaluation
and destruction while avoiding redundant initialization, temporaries, and state.
Runtime progress remains explicit when the operation needs it, such as a count of
completed elements. Allocator policy, container APIs, synchronization, and general
borrow checking belong to their consumers. Delayed local initialization is a
separate possible feature.

## Current boundary

Bindings require initializers, and aggregate construction initializes the whole
value. Empty construction requests the type's defined default. Write accesses an
existing value; Take changes source availability without establishing bytewise
relocatability.

Pointers carry access and local non-null facts. Unmodeled target lifetimes and
native output obligations belong to providers and callers. Each adapter defines
its initialization and backing guarantees. Existing unchecked text factories
require callers to establish Unicode validity while retaining type and known
lifetime checks.

The backend already constructs results in final storage and uses `DeferredResult`
for delayed construction and cleanup. A source-level raw-storage API remains open.
Generated C++ has a C++20 baseline.

## Candidate contracts

Track storage availability, object lifetime, and value validity separately.
Construction requires suitable size and alignment and type-specific initialization.
The selected API must distinguish construction from assignment and define these
operations:

| Operation | Required behavior |
| --- | --- |
| Reserve | Provide storage without constructing or clearing the payload |
| Construct | Initialize an empty destination without reading or assigning old contents |
| Complete | Expose the initialized result and identify its cleanup owner |
| Access | Preserve storage identity and backing lifetime |
| Destroy | End a live payload's lifetime once before storage release or reuse |

A raw slot could omit implicit payload destruction and an initialized flag,
leaving cleanup to its adapter. A managed alternative could own that obligation.
The choice must define Copy, Take, address stability, and the validity of outstanding
construction handles. Ordinary reads and assignment require an initialized `T`
place. Write access is nonexclusive; define destination aliasing requirements for
each construction operation.

A prefix builder is a candidate for partial construction: progress advances after
each successful element, failure destroys the completed prefix, and completion
hands off cleanup. Reverse construction order is the proposed destruction order.
The containing array's lifetime must satisfy C++ rules as well as each element's.

Modeled construction can establish completion on success. Opaque native output
needs a provider contract and a way to assert completion. A conceptual
`assume_init` would assert object lifetime, value validity, and cleanup ownership;
it would perform no construction or validation and extend no borrow. A returned
count or status gains initialization meaning through the adapter's contract.
Each unchecked operation must state its preconditions and which checks remain.
Statically known violations receive diagnostics; unmodeled preconditions remain
provider and caller obligations.

For each candidate, define the subject and validity interval of completion facts,
invalidation by writes, callbacks, escaping aliases, or owner movement, and the
facts retained at control-flow joins. Examples must identify checked facts and
caller assertions.

Evaluate the surface against three consumers:

- **Byte output:** One bounded native call writes a reported prefix. Define count
  bounds, how they are checked or asserted, and which prefix is initialized and
  readable after success, short writes, or failure. Specify how movement, reuse,
  release, retention, and reentry affect addresses and views.
- **Object output:** A provider constructs a concrete non-default-constructible
  object in aligned storage. Success exposes it; failure defines cleanup.
- **Fallible sequence construction:** A builder constructs elements in order,
  cleans completed elements on failure, and hands off the completed sequence
  without constructing it again.

C++ realization must preserve the admitted constructor capabilities. Placement
construction or `std::construct_at` and `std::destroy_at` provide native lifetime
operations. Immovable results require final-location construction and an interface
that preserves prvalue delivery. Specify constructor requirements separately for
access at the final location and extraction into another owner. Native exception
recovery occurs before a `noexcept` boundary.

The chosen source contract determines the semantic facts and checks needed by
analysis, publication, and realization. Shared native lifetime mechanisms belong
to runtime support; buffer policy and container algorithms belong to their craft.

## Related work

The selected storage surface determines its prerequisites; generic classes remain
unimplemented. Concrete native adapters can use the existing explicit C++ boundary.
Evaluate storage candidates for both runtime and constant execution, including
operation admission and retained-result eligibility.

Suspending operations need a lifetime-closure contract before reusing their storage;
cross-thread use requires synchronization and visibility guarantees. Erased holders
need defined ownership before consuming construction primitives. Text output
requires UTF-8 validity and a failure policy in addition to initialized byte extents.
Multi-field consuming decomposition starts from an initialized owner and needs
its own transfer and cleanup contract.

## Open decisions

### OPEN-01 — Select the destination and ownership surface

- **Status:** Active
- **Question:** Builtin storage, semantic operations wrapped by a craft, or concrete native adapters first?
- **Required decision:** Empty/live/consumed states, cleanup ownership, Copy and Take, address stability, and native type capabilities.
- **Evidence:** Accepted and rejected byte-output and object-output examples, including final-location construction of an immovable object. State the selected surface's generic prerequisites.

### OPEN-02 — Define construction handoff and completion evidence

- **Status:** Blocked by `OPEN-01`
- **Question:** Scoped destinations, raw-address adapters with assertions, or separate modeled and native operations?
- **Required decision:** Aliasing, retention, reentry, movement, partial failure, stale handles, and returned-view backing. Select runtime state only where the protocol needs it.
- **Evidence:** The three consumer contracts have implementable source operations and C++20 realizations.

### OPEN-03 — Express unchecked obligations

- **Status:** Blocked by `OPEN-02`
- **Question:** Explicitly named operations or a declaration/call-site marker?
- **Required decision:** Distinguish checked facts from provider and caller obligations while retaining existing access and lifetime checks.
- **Evidence:** An adapter can expose an ordinary result without requiring callers to repeat its internal assertions.

## Deferred work

### DEFER-01 — Adopting existing byte representations

- **Reason deferred:** Direct construction and byte output do not require typed byte adoption.
- **Reactivation condition:** A mapped-memory or native binary-layout consumer specifies valid representations, eligible types, alignment, extent, ownership, and target support.

The consumer must identify a native operation that establishes the required
lifetime and state its target and library requirements.

### DEFER-02 — Arbitrary partial structure construction

- **Reason deferred:** Independent fields need their own progress and cleanup policy.
- **Reactivation condition:** A concrete builder needs independently constructed fields and establishes why whole-object construction is insufficient.

Its success and failure examples determine the field-state requirements.

## Validation

Implementation follows `OPEN-01` through `OPEN-03`. The selected slice must deliver
source operations, the facts and checks they require, native realization, and
support together. Validation covers:

- Check accepted access to initialized contents and single cleanup ownership.
  Exercise uninitialized reads, ordinary `T` Write access to uninitialized contents,
  invalid completion, duplicate construction or destruction, and use after
  consumption. State which cases the selected contract diagnoses and which remain
  unchecked obligations. Include branch joins, zero-iteration loops, owner movement,
  and external invalidation to establish the limits of the available facts.
- Compile and execute C++20 with the admitted non-default-constructible,
  nontrivially destructible, over-aligned, and immovable types. Observe construction
  and destruction in the specified order.
- Check empty, partial, and full prefixes, short writes, and failure at each
  construction position. Cover return, break, and propagated failure wherever
  admitted; each supported exit follows the selected cleanup contract. Returned
  views obey their backing contract.
- Check admission and retained-result rules for each execution mode delivered by
  the selected slice. Native provider obligations require their own interop evidence.

Compare direct fill and construction with default initialization followed by
writes under equal results and effects. Inspect generated and optimized native
code, then measure initialization traffic, constructor calls, bookkeeping, and
compilation cost separately. Update permanent references with delivered behavior.
