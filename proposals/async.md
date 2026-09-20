# Async operations and structured lifetime

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** Async user semantics, compiler facts, lowering, and C++ interoperation boundaries
- **Depends on:** None for the same-thread core; [cross-thread concurrency](concurrency.md) for cross-thread execution

## Summary

This proposal defines cold operations, suspension, structured children,
cancellation, lifetime closure, and fixed-size combinators. These phase-one
semantics are accepted and unimplemented. Several source spellings remain
provisional.

Execution context (`OPEN-01`) and suspension, borrow, and frame rules (`OPEN-02`)
block implementation. Provider selection, lowering, and explicit C++ async
bridges remain deferred under `DEFER-08`.

The same-thread core is independent of the cross-thread concurrency proposal.
That dependency applies when operations, frames, captures, or completions may
cross threads.

## Context

Carven currently has no async callable, operation lifetime, scheduler, thread,
atomic, lock, or shared-ownership source contract. Concurrent use of generated
C++ does not establish a Carven concurrency guarantee.

Same-thread suspension can run within one logical thread or event loop. It
implies neither blocking nor thread creation or physical parallelism. Migration,
blocking operations in async contexts, and awaitable channels require explicit
integration contracts. Network, file, timer, DNS, and TLS APIs need concrete
provider use cases and are outside the async core.

The design uses these maturity labels:

- **Accepted:** A settled design decision, available as a dependency.
- **Working spelling:** Accepted behavior with provisional names or grammar.
- **Exploration:** An unresolved design question.
- **Deferred:** Outside phase one, with a recorded reactivation condition.

The numbered design entries are the decision record. Preserve their identifiers
and contracts during editorial changes; a changed decision explicitly supersedes
its predecessor. Working-spelling changes preserve completion, ownership,
lifetime, and failure behavior. Implementation maps the relevant entries to
syntax, semantic facts, diagnostics, and tests.

## Goals and non-goals

The core defines observable async behavior independently of backend mechanisms,
with structured ownership and opportunities to specialize known operations.
Execution context and suspension lifetime must be resolved before implementation.
Future coroutine, sender, and provider bridges must preserve that contract.

Cross-thread executors, runtime-sized groups, streams, `select`, shared tasks,
ownership transfer, supervisors, and general I/O or scheduler APIs are outside
phase one. Structured cancellation does not roll back external effects. The
proposal fixes no runtime library, private generated spelling, or allocation count.

## Design

### Semantic authority and cost model

**Maturity:** Accepted.

The following decisions define semantic ownership and representation constraints.

#### METHOD-01 — Carven owns the semantics

Carven analysis defines async source validity and publishes its semantic facts.
C++ coroutine promises, sender concepts, runtime task types, and libraries may
implement or adapt those facts. They do not introduce additional source behavior.

#### METHOD-02 — Specialize representation from known semantic facts

High-level forms require their observable behavior, with no required generic
runtime object:

```text
Carven intent and facts
    -> specialized C++ state machine
    -> flattened frame/storage
    -> backend-native cancellation/deadline
    -> std::execution bridge
    -> or another observationally equivalent form
```

Known child counts, concrete result and failure types, ownership, cancellation,
context, and lifetime facts can permit elimination of allocations, type erasure,
task wrappers, tuple materialization, stop sources, and join state.

An `await` is a potential suspension boundary. Persistent state is required only
for values, failures, cancellation, ownership, destruction, and resumption that
remain live across a reachable suspension. Lowering must preserve enough state
to resume correctly. Compiler-generated IIFEs and wrappers introduce no source
lifetime boundary or requirement for a carrier.

#### METHOD-03 — Mechanism correctness and use correctness are separate

The language mechanism ensures exactly-once completion, ownership, lifetime
closure, cancellation propagation, result typing, and safe destruction. Programs
still choose which operations may run concurrently, their business priority,
and how to handle external effects already performed. Structured cancellation
does not roll back database writes, network sends, payments, or other effects.

### Vocabulary

**Maturity:** Accepted.

#### TERM-01 — Operation

A cold, owning, single-consumer value produced by an async call. It describes
work that starts through `await`, `async let`, or a combinator. Use “operation”
and “child” for source semantics; “task” may describe a runtime representation.

#### TERM-02 — Child

An operation started by a lexical owner and included in its structured lifetime.
`async let` and fixed-size combinators establish child relationships.

#### TERM-03 — Completion

```text
completion<T, E> = value(T) | failure(E) | cancelled
```

Typed failure uses the nominal failure set. Cancellation is a separate channel.

#### TERM-04 — Cancellation request

A cooperative signal:

```text
request cancellation != operation completed as cancelled
```

An operation may have completed before the request, delay or ignore it, or accept
it at a safe point and eventually complete as cancelled.

#### TERM-05 — Observation

Consumption of a child's value or failure. Abandoning observation leaves the
owner responsible for lifetime closure.

#### TERM-06 — Lifetime-closed

An operation is lifetime-closed when all of these permanent conditions hold:

- no execution agent, callback, continuation, or completion can access its state;
- none can access storage borrowed from its owner, including frames, locals, and buffers;
- external registrations that could resume it are revoked or complete;
- lifetime-related synchronization is complete;
- its state can be destroyed synchronously without blocking, a data race, or use after free.

Completion followed by closure, waiting after cancellation, synchronous backend
revocation, or compiler proof can establish these conditions. No particular
runtime `join()` call is required.

#### TERM-07 — Accountable lifetime owner

The single owner responsible for eventual lifetime closure. Observation,
borrowing, cancellation requests, and internal references do not confer that
responsibility.

### Single async operation

**Maturity:** Accepted.

#### TASK-01 — Async invocation is cold

Calling an async function constructs an operation without executing its body:

```carven
let pending = fetch_user(id); // cold
```

#### TASK-02 — `await` starts in the current logical task

```carven
let user = await fetch_user(id)?;
```

`await` starts and consumes its operand in the current logical task. It may
suspend but creates no independent structured child and implies no thread
blocking, creation, or thread-pool submission.

#### TASK-03 — Operation is owning, move-only and single-consumer

Operations cannot be implicitly copied, started repeatedly, or awaited more
than once. Shared tasks and multi-consumer futures are outside phase one.

#### TASK-04 — Dropping a cold operation is safe but diagnosed

A cold operation can synchronously destroy its captures without starting work.
Accidental discard produces a must-use diagnostic for missed `await` or
`async let` intent.

#### TASK-05 — `await` and `?` remain orthogonal

```carven
let value = await operation?;
```

This is equivalent to:

```text
(await operation)?
```

`await` handles suspension, value/cancelled completion, and cancellation
propagation. `?` handles nominal typed failure only.

#### TASK-06 — Async callable intrinsically admits cancellation

Cancellation is intrinsic to async completion. It requires no `Cancelled` entry
in a callable's failure set, which describes typed failures only.

### Async let and lexical children

**Maturity:** Accepted semantics; working grammar.

#### CHILD-01 — `async let` starts one lexical child

**Exact grammar remains a working spelling.**

```carven
async let remote = fetch_remote();
```

The binding starts a non-escaping child whose lifetime belongs to the lexical
owner scope.

#### CHILD-02 — Plain `async let` children are failure-independent

A child's early failure is saved for observation. It neither interrupts the
parent nor cancels other ordinary `async let` children. If `await child?`
propagates the failure and exits the owner, scope closure cancels and closes the
remaining children. `when_all` provides explicit sibling fail-fast behavior.

#### CHILD-03 — Normal exits require explicit result intent

On a normal return or fallthrough, each child must have been consumed or
explicitly cancelled to acknowledge abandoning its result. Otherwise the compiler
diagnoses the exit.

Failure and cancellation exits automatically close children without individual
source cancellation calls. The parent's original outcome remains primary.

```carven
async let remote = fetch_remote();

if cache_hit {
    cancel(remote);
    return cached;
}

return await remote?;
```

### Cancellation surface

**Maturity:** Accepted semantics; selected names and module placement remain working spellings.

#### CANCEL-01 — Cancellation authority follows structured ownership

A lexical owner may request cancellation of its children. Ambient cancellation
passes from parent to child. Ordinary observers gain no cancellation authority.

#### CANCEL-02 — `cancel(child)` is request-only

**Standard-module placement remains a working spelling.**

```carven
cancel(child);
```

Canonical symbol resolution identifies this compiler-known standard operation;
`cancel` is not a keyword, and a user function with that name remains an ordinary
call.

The operation requests cancellation without waiting, guaranteeing terminal
completion, destroying state, consuming the binding, or transferring ownership.
A later `await child` is valid. Exiting without observing the child after this
request expresses intentional result abandonment.

#### CANCEL-03 — Ambient request is forwarded, not automatically accepted at every `await`

`await` forwards ambient cancellation context. Each operation responds at its
own safe points under its contract. There is no forced cancellation completion
before or after every await, so adding an unrelated await does not itself change
cancellation control flow.

#### CANCEL-04 — Query and checkpoint are separate operations

**Names remain working spellings.**

```carven
if cancellation_requested() {
    return partial;
}

await cancellation_point();
```

`cancellation_requested() -> bool` queries without transferring control.
`await cancellation_point()` explicitly accepts a pending request and completes
as cancelled, or immediately completes with a value when no request exists.
Both may lower to flag loads and branches without allocating a task object.

#### CANCEL-05 — No cancellation keyword family in phase one

Phase one adds no `cancel`, `exit`, `stop`, `check cancellation`, `shield`,
`on cancel`, or `join` keyword. Source control uses `async`, `await`, `async let`,
`return`, and `throw`; compiler-known operations supply request, query, and
checkpoint behavior.

#### CANCEL-06 — No user cancellation source/handler/shield in phase one

**Deferred.**

Structured owners and the runtime hold cancellation sources in phase one.
User-created sources, arbitrary task control, handlers, and shields require
later contracts for UI control, shutdown, deadlines, tests, supervision, or C++
stop-token bridges.

### Lifetime and scope closure

**Maturity:** Accepted phase-one invariants; ownership transfer remains deferred.

#### OWN-01 — Every active operation has one accountable owner

Every active operation has exactly one accountable lifetime owner. This source
responsibility permits multiple internal pointers and references.

#### OWN-02 — Owner completion requires child lifetime closure

An owner may enter an async closing epilogue while children remain live. It
cannot publish completion, destroy its frame, or release child-reachable storage
until closure:

```text
request cancellation if needed
    -> wait for or prove child closure
    -> destroy child state
    -> publish parent value/failure/cancelled
```

Waiting may suspend; it does not block the thread.

#### OWN-03 — Result abandonment is not lifecycle abandonment

```text
abandon result != destroy running operation
```

Even when a child outcome is secondary or unobserved, its owner remains
responsible for cancellation, lifetime closure, and safe destruction.

#### OWN-04 — Active child has no ordinary immediate `drop`

```text
drop(cold operation)   -> safe, with must-use diagnostic
drop(active child)     -> rejected as immediate destruction
drop(closed state)     -> safe
```

A trusted backend may prove permanent quiescence through synchronous revocation.
That satisfies lifetime closure; ordinary destruction alone provides no such proof.

#### OWN-05 — Future non-joining work requires explicit ownership transfer

**Deferred.**

A future service, actor, or supervisor could receive ownership through an atomic
transfer of lifetime, shutdown cancellation, failure policy, and storage
responsibility. Ownerless detach is invalid. Phase one has no transfer, so all
children close within their current structured tree.

### When all

**Maturity:** Accepted.

#### ALL-01 — Source name is `when_all`

`when_all` is a compiler-known function-shaped operation, not a keyword or
contextual keyword. Its name follows the execution vocabulary and avoids the
collection predicate `all`; no `all` alias is provided.

#### ALL-02 — `when_all` is a cold fixed-size composite

Phase one accepts a statically known set of cold input operations. Construction
remains cold; starting the composite through `await` or `async let` starts all
children:

```carven
let pair = when_all(fetch_user(id), fetch_permissions(id)); // cold
let (user, permissions) = await pair?;                       // starts both
```

Arguments evaluate left to right. Children share one concurrent composition
boundary; argument order establishes no dependency or physical parallelism.
Already-started `async let` bindings are not inputs. Any future joining operation
for them needs a separate explicit ownership contract.

#### ALL-03 — Success returns an argument-ordered heterogeneous tuple

```carven
let (a, b, c) = await when_all(op_a(), op_b(), op_c())?;
```

Results follow argument order, independently of completion order:

```text
Operation<A, E1> + Operation<B, E2>
    -> Operation<(A, B), E1 | E2>
```

The tuple is a general static product with `(a, b, c)` syntax, not an async-only
container or an array/list form.

#### ALL-04 — First non-value closes the group; final failure outranks cancellation

The first child failure, child cancellation, or accepted ambient cancellation
requests sibling cancellation. The composite waits for every child to close
before choosing its outward completion:

```text
any failure     -> first committed failure
else cancelled  -> cancelled
else            -> argument-ordered value tuple
```

The first committed failure wins among failures, including when cancellation
arrived earlier. Bridges map cancellation-induced results to cancelled completion
rather than typed failure. Programs express business-error priority through
ordering or nesting.

#### ALL-05 — `when_all` owns and closes all internal children

The composite owns every internal child and closes all of them before publishing
an outcome. Lowering may flatten this ownership tree without a runtime group object.

### Competitive composition

**Maturity:** Accepted semantics; selected spellings and label grammar remain working.

#### ANY-01 — Provide `when_any` and `first_successful`, not `race`

**`first_successful` remains a working spelling.**

Use separate operations for first terminal completion and first successful value.
The phase-one surface has no `race` alias or control-flow `select` keyword;
`race` does not identify a consistent outcome and loser-lifetime policy across APIs.

#### ANY-02 — `when_any` selects the first terminal completion

```text
first(value | failure | cancelled) wins
```

Once committed, a winner cannot be replaced by a later loser outcome. Simultaneously
ready candidates use argument/start order as the tie-breaker. Iterative fairness
belongs to future select/stream design.

#### ANY-03 — `first_successful` selects the first value completion

Failures and cancellations remove individual candidates. The first value wins
and requests cancellation of losers. If all candidates close without a value:

```text
any failure -> first committed failure
else        -> cancelled
```

Accepted ambient cancellation ends the entire composite instead of continuing
the search for a value.

#### ANY-04 — Competitive losers are cancelled and lifetime-closed

```text
commit winner
    -> request cancellation of losers
    -> lifetime-close every loser
    -> publish winner
```

An unresponsive loser delays outward completion. Phase one does not transfer
ownership to return early. Backend synchronous revocation may satisfy closure
without an asynchronous wait.

#### ANY-05 — `when_any` success is a statically tagged branch sum

**Result semantics are accepted; label grammar remains a working spelling.**

```text
Operation<A, E1> + Operation<B, E2>
    -> Operation<Choice<A, B>, E1 | E2>
```

A successful result carries winner identity in a static branch sum. It is not a
task handle, `index + Any`, or dynamically erased result. Candidate labeled syntax:

```carven
let event = await when_any(
    packet: socket.read(),
    shutdown: wait_shutdown(),
)?;
```

#### ANY-06 — `first_successful` initially requires one normalized value type

```text
Operation<T, E1> + Operation<T, E2>
    -> Operation<T, E1 | E2>
```

Candidates share one normalized value type. Map heterogeneous candidates to a
common domain type explicitly, or use `when_any` for a tagged sum. Result shape
does not vary implicitly with coincidental type equality.

### Timeout

**Maturity:** Accepted semantics; source and failure-type spellings remain working.

#### TIME-01 — Timeout is a compiler-known operation, not syntax

**`timeout` remains a working spelling.**

```carven
let response = await timeout(fetch_response(), 2.seconds)?;
```

The compiler-known call constructs a cold composite that races the operation
against a deadline when started. It adds no `within` or `timeout` keyword.
Lowering may use a native deadline, linked timeout, event-loop timer, or explicit
timer child; no runtime timer task is required by the source form.

#### TIME-02 — Local timeout produces typed failure

**`Timeout` remains a working type name.**

```text
operation value first     -> value
operation failure first   -> propagate failure
operation cancelled first -> cancelled
deadline first            -> close operation; failure(Timeout)
ambient cancellation      -> close operation and timer; cancelled
```

The failure set is `E | Timeout`. A local deadline is distinct from ambient
cancellation. Once the deadline wins, the operation's later outcome cannot
replace the timeout failure.

### Source surface

**Maturity:** Accepted semantic roles with the per-form spelling maturity below.

| Form | Maturity |
| --- | --- |
| `async fn f() { ... }` | Async callable semantics accepted; exact declaration grammar working |
| `await operation?` | `await`/`?` composition accepted; exact grammar working |
| `async let child = operation;` | Child semantics accepted; exact grammar working |
| `cancel(child)` | Function-shaped compiler-known operation accepted; standard-module placement working |
| `cancellation_requested()` | Semantics accepted; name working |
| `await cancellation_point()` | Semantics accepted; name working |
| `when_all(a(), b())` | Name and function-shaped operation accepted |
| `when_any(left: a(), right: b())` | `when_any` semantics accepted; label grammar working |
| `first_successful(a(), b())` | Semantics accepted; name working |
| `timeout(operation, duration)` | Semantics accepted; name and `Timeout` type spelling working |

Canonical resolution identifies standard operations for dedicated semantic facts
and diagnostics. Their spellings remain ordinary function-shaped names.

### Compiler-owned facts and feature admission

**Maturity:** Draft; blocked by `OPEN-01` and `OPEN-02`.

An implementation slice must publish these source facts before lowering:

- async signatures and value, failure, and cancellation completion;
- cold construction, movement, single consumption, and must-use diagnostics;
- await evaluation, start, suspension, resumption, and completion disposition;
- lexical owners, children, observation, cancellation authority, and closure;
- exactly-once composite start, arbitration, secondary outcomes, and result shape;
- scope-closing control edges and the parent completion publication barrier;
- context inheritance and transition, including the same-thread restriction;
- captures, borrow admission, destruction order, and storage requirements;
- integration with name, type, call, access, failure, control, and availability facts.

SemIRProgram owns these facts. Promise types, substitutions, destructors, and
library behavior implement their consequences.

### Selected lowering

**Maturity:** No lowering selected; selection is deferred under `DEFER-08`.

Candidate mechanisms include C++ coroutines, explicit state machines, specialized
frames, and sender bridges. Each must preserve source behavior, compiler facts,
diagnostics, and cost boundaries. Storage retains only state required across
suspension, completion, or C++ interoperation boundaries. Ownership flattening,
wrapper elimination, and native cancellation/deadline support are permitted
when they preserve those requirements.

## Open decisions

**Next discussion:** `OPEN-01`

### OPEN-01 — What execution context does a same-thread operation inherit?

- **Status:** Active
- **Depends on:** `TASK-01`, `TASK-02`, `CANCEL-03`, `OWN-01`, `OWN-02`
- **Question:** The context model determines capture, continuation placement,
  inline completion, same-thread progress, and submission failure.
- **Constraints:** Scheduler is an execution mechanism rather than operation identity; `async` does not imply
  a thread pool, parallelism, or migration; phase one remains same-thread.
- **Options:** The following coupled axes remain unresolved:
  1. cold construction records caller context, or start inherits the structured owner's current context;
  2. continuation resumes in awaiting context, completion context, or an explicitly selected context;
  3. direct completion may synchronously resume parent on the starting stack, or must enqueue;
  4. deep synchronous completion chains use unrestricted recursion, trampolining, or another bounded mechanism;
  5. a same-thread queue guarantees FIFO, eventual progress, another fairness level, or none;
  6. allocator, clock, deadline, and ambient cancellation are wholly or partly context properties;
  7. same-thread scheduler submission can fail, and if so whether that failure is nominal.
- **Closure condition:** Select a coherent same-thread context and continuation model, then validate it against
  nested awaits, immediate completion, reentrancy, deep chains, cancellation, and submission failure.

### OPEN-02 — Which values and borrows may live across suspension, and where may the frame live?

- **Status:** Blocked
- **Depends on:** `OPEN-01`
- **Activation condition:** Execution-context and continuation semantics are closed.
- **Question:** The suspension boundary determines lifetime safety, destruction
  order, frame storage, visible cost, and allocation failure.
- **Constraints:** Carven defines the frame-lifetime guarantee. Borrow admission
  requires proof of validity across suspension; reject borrows without that proof.
- **Options:** The design must close all of these dimensions:
  1. operand evaluation, temporary destruction, and typed-failure propagation around suspension;
  2. locals/captures moved into the frame versus referents held by an external owner;
  3. whether Read/Write borrows can cross a potentially suspending `await`;
  4. whether a narrow borrow subset is admitted when the compiler proves owner coverage;
  5. how cancellation and the closing epilogue extend child-reachable storage;
  6. caller/scope frame storage versus compiler/runtime allocation;
  7. whether `async` sufficiently exposes frame-storage cost and which phase-one
     contract handles allocation failure.
- **Closure condition:** Define a diagnostic admission rule, destruction order, storage contract, and examples
  covering success, failure, cancellation, and owner exit.

## Deferred work

### DEFER-01 — Advanced suspension and frame control

- **Reason deferred:** Phase one needs a conservative, diagnosable borrow and storage boundary before exposing
  general lifetime proof or user-controlled frame placement.
- **Depends on:** `OPEN-02`
- **Reactivation condition:** The phase-one frame contract is implemented and a concrete API requires more
  general borrow proof, custom allocation, or explicit storage control.

This includes general borrow-across-suspension proof beyond the conservative admission boundary, custom
allocator selection, and user control over frame placement or allocation policy.

### DEFER-02 — Dynamic composition and async abstractions

- **Reason deferred:** Fixed operations and lexical composition must close before runtime-sized or iterative
  composition introduces new ownership and fairness rules.
- **Depends on:** `OPEN-01` and `OPEN-02`
- **Reactivation condition:** A real program cannot be expressed by fixed-size combinators and supplies a
  complete lifetime, cancellation, result, and fairness contract.

This includes dynamic/runtime-sized task groups, async closures, generators, streams, and loop-oriented
`select`.

### DEFER-03 — Shared observation

- **Reason deferred:** The phase-one operation is move-only and single-consumer; sharing changes result
  storage, observation, cancellation authority, and lifetime.
- **Depends on:** `TASK-03`, `OPEN-01`, and `OPEN-02`
- **Reactivation condition:** A concrete API requires multiple observers and can define repeated observation,
  result retention, cancellation authority, and closure.

This includes shared tasks, multi-consumer futures, and repeated `await`.

### DEFER-04 — User cancellation control

- **Reason deferred:** Phase one obtains cancellation authority from structured ownership and does not need an
  arbitrary user-created control plane.
- **Depends on:** `CANCEL-01` through `CANCEL-06`
- **Reactivation condition:** UI shutdown, service control, tests, deadlines, or C++ interoperation require a source-level
  cancellation source and can specify propagation, handlers, shielding, and lifetime.

This includes user cancellation sources/tokens, custom cancellation handlers, and shields. `CANCEL-06`
remains the phase-one authority.

### DEFER-05 — Ownership transfer and supervision

- **Reason deferred:** Phase one keeps every started child inside one structured ownership tree.
- **Depends on:** `OWN-01` through `OWN-05`
- **Reactivation condition:** A service, actor, or supervisor use case requires work to outlive the current
  scope and can atomically transfer lifetime, shutdown, failure, and storage responsibility.

This includes daemon work, explicit detach-like transfer, supervisor/service/actor ownership, and background
runtime ownership. Ownerless work remains invalid; `OWN-05` is the phase-one authority.

### DEFER-06 — Async resource disposal

- **Reason deferred:** Phase one can express explicit cleanup without selecting a generic async-disposal
  protocol or new control construct.
- **Depends on:** `OPEN-01`, `OPEN-02`, and a concrete async resource API
- **Reactivation condition:** Repeated resource APIs require one reusable disposal contract and can state
  failure, cancellation, ordering, and scope-exit behavior.

This direction owns any generic async resource-disposal construct.

### DEFER-07 — Cross-thread async and scheduler surface

- **Reason deferred:** Same-thread async can be designed independently; migration introduces value movement,
  sharing, happens-before, synchronization, affinity, and shutdown obligations.
- **Depends on:** Cross-thread concurrency
- **Reactivation condition:** The concurrency proposal defines a concrete cross-thread value and synchronization contract
  for an actual executor or provider use case.

This direction includes cross-thread resume, physical parallel execution, Send/Sync-like capability,
scheduler/executor source APIs, source-visible scheduler hops, thread affinity, priority, fairness, and
cross-thread completion carriers.

### DEFER-08 — Provider/lowering selection and explicit async C++ interoperation

- **Reason deferred:** Provider/lowering selection and async `import(cpp)`/`export(cpp)` implementation require
  closed source semantics, execution context, suspension lifetime, and feature-admission facts.
- **Depends on:** `OPEN-01` and `OPEN-02`; cross-thread concurrency for
  cross-thread candidates
- **Reactivation condition:** The source contract and compiler facts form an actionable vertical slice; a
  provider or lowering may then be selected without leaking experimental types into public artifacts.

Evaluate provider candidates against the accepted operation, completion, failure,
cancellation, lifetime, and context contracts. Public C++ async ABI and import
bridges/export façades remain part of this deferred selection. Store reading
material and experimental results in the external archive.

#### Bridge scope and constraints

- Carven operation/completion and C++ coroutine/awaitable bridges in both directions;
- Carven operation and C++26 `std::execution` sender bridges in both directions;
- platform I/O, event loops, thread pools, and third-party runtime providers;
- exception, cancellation, scheduler, allocator, generated representation, public consumer surface, ABI, and
  downstream build conversion;
- C++ exceptions cannot cross a no-exception Carven frame; an exception-enabled bridge catches and maps them
  to declared typed failure;
- third-party task, sender, scheduler, socket, and allocator types do not enter phase-one public generated ABI;
- downstream builds explicitly select, pin, and link a provider; the compiler does not become a second package
  manager;
- ordinary generated C++ remains inspectable and debuggable behind a Carven-owned bridge contract.

#### Bridge design questions

- Which C++ awaitables/senders can be imported safely, and how are value/error/stopped signatures declared?
- How does a C++ exception set map statically to nominal failure?
- How does Carven cancellation map bidirectionally to `std::stop_token` or a library token?
- How are scheduler affinity, thread migration, and callback lifetime verified?
- May a bridge allocate or erase types, and how is cost visible in the source/build contract?
- Is the C++ consumer API a blocking bridge, callback, awaitable, sender, or separate layered surfaces?
- Does ABI stop at a C adapter or include a C++ contract within one compiler/toolchain domain?
- Who owns provider shutdown, outstanding work, process lifetime, and the test harness?

#### Bridge evidence

Every bridge is an independent product slice and must validate:

- lossless value, typed-failure, and cancellation mapping;
- exactly-once completion and operand evaluation;
- frame, operation, and provider lifetime;
- scheduler/context transition;
- no-exceptions and exception-enabled boundaries;
- generated C++20/C++23 compile, link, and run behavior;
- dependency, license, version pin, and downstream build instructions;
- no private backend type leaks through public surfaces unless the consumer contract explicitly accepts it.

Compare candidate bridges using generated code size, compile time, completion
fidelity, lifetime, and diagnostics. Selection also requires the source and
compiler prerequisites above.

## Implementation

Implementation is not yet actionable. `OPEN-01` and `OPEN-02` block a coherent grammar/SemIRProgram/control/lifetime
slice; `DEFER-08` keeps provider/lowering selection deferred until that slice exists.

Once unblocked, each relevant decision ID must map through grammar, SyntaxProgram, SemIRProgram,
verification, TargetUnit lowering, runtime, C++ interoperation boundaries, diagnostics, tests, and
permanent documentation. Scope-closing control edges and the parent-completion
publication barrier must be explicit compiler facts. Cross-thread work also
requires the memory-model and threading contracts.

## Validation

Feature admission must cover:

- cold operation does not start early and accidental discard emits a must-use diagnostic;
- exactly-once operation start, consume, and completion;
- normal-exit child observation or explicit cancellation intent;
- cancellation request remains distinct from cancelled completion;
- an unresponsive child delays owner/composite completion;
- `when_all` evaluation, start, result order, failure precedence, and lifetime closure;
- `when_any` winner stability, tie-break, and loser secondary outcomes;
- `first_successful` skips failed/cancelled candidates;
- timeout, ambient cancellation, and child completion races;
- parent frame destruction only after every owned child is lifetime-closed;
- synchronous backend revoke satisfies the complete lifetime-closed postcondition;
- rejection of active drop, orphan work, implicit detach, repeated await, and cross-thread resume;
- direct/inline completion reentrancy and deep-chain behavior;
- temporary, borrow, frame, and destruction behavior around suspension;
- generated C++20/C++23 compile, link, and run checks under no-exceptions configuration.

Tests assert Carven observable semantics and representation invariants. They do not fix private generated
spelling, a runtime class, heap-allocation count, or a third-party type. Cost claims require benchmarks and
artifact inspection in addition to semantic tests. Bridge-specific evidence remains under `DEFER-08` and may
be collected without turning a candidate into an implementation choice.
