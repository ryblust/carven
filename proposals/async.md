# Async operations and structured lifetime

- **Status:** Draft
- **Implementation:** Partial — cold operations, await, lexical children, static source execution, and same-thread native hosts
- **Scope:** Structured operation lifetime and deferred composition

## Implemented foundation

Carven establishes types, access, evaluation order, ownership, and lifetime before
C++ generation. Async adds delayed execution and potentially suspending closure
to those contracts. An async call constructs a cold operation; `await` consumes it
and `async let` starts a lexical child.

```carven
import std::async using yield_once;

async fn next(value: i32) -> i32 {
    await yield_once();
    return value + 1;
}

async fn main() {
    async let child = next(4);
    println(await child);
}
```

An operation's canonical type contains its successful result and solved nominal
failure set, independently of its producer. Completion selects value, nominal
failure, or cancellation. Cancellation is cooperative: a request records intent;
an accepting operation selects cancelled completion. Requests do not undo effects.

Children belong to the scope that starts them. Every normal exit observes each
child or states cancellation intent; failure and cancelled exits request
cancellation automatically. Exit operands are selected before closure. Closure
waits while child-reachable backing remains alive, then ordinary scope cleanup
and the selected exit proceed. An unresponsive child can delay closure.

The shared ownership analysis checks consumption, retained backing, destructive
access, and escaping loans. Published completion facts distinguish construction
from delayed execution and determine possible cancelled completion. Private
synchronous factories carry the relationships of their returned operation.

These source facts let generation select direct C++ expressions and remove
unnecessary state. C++ scopes, coroutine transfer, templates, and destruction
express the remaining work. Native optimization and source analysis provide
complementary evidence; generated artifacts and measured workloads establish cost.

`const async fn` uses the same source operations through the cooperative semantic
executor. Direct awaits retain the current task; lexical children and explicit
yields use a FIFO queue. Tasks share a root budget and memory domain. Runtime
execution uses C++20 coroutines and a single-threaded, nonreentrant driver. No
preemption, fairness, or bounded cancellation latency is established.

The [source reference](../docs/language/async.md) owns current admission and
observable behavior. [Ownership analysis](../docs/compiler/analysis/ownership.md),
[semantic execution](../docs/compiler/analysis/evaluation.md), and
[C++ realization](../docs/compiler/backend/realization.md#async-realization) own
their implementation boundaries.

## Native hosts and providers

A provider bridge must define start, completion mapping, cancellation, registration
lifetime, callback quiescence, and source backing coverage together. A notification
is sufficient for publication only when the producer can no longer access the
released state. Native root driving accepts an optional event pump. Its
`poll(driver)` selects completed registrations and queues continuations before
normal FIFO dispatch; `wait()` waits for an external condition when no work is
ready. The pump must not resume user code directly. The default root uses the same
ownership and completion implementation with the pump branch absent at compile
time. Providers retain a bound `ResumeContinuation` until dispatch; it queries
task cancellation and queues resumption without exposing queue-node state.

A [native steady-timer fixture](../tests/interop/async/timer.hpp) exercises this
boundary. It queries task cancellation, fixes the outcome,
removes the registration, and enqueues the continuation. A cancellation request
observed before timer commitment selects cancelled completion; requests after a
success commitment leave that result intact. Closure therefore waits for the
provider to release its registration before releasing child backing. The small
fixture uses a linear registration scan and establishes no general callback or
source-level timer contract. External
examples are recorded in [the research note](../notes/async-programming.md#provider-examples).

The [native pipe fixture](../tests/interop/async/pipe.hpp) uses the same pump for
POSIX nonblocking reads and writes. Readiness triggers a syscall; its byte count,
EOF, or errno selects the completion. EAGAIN keeps the registration. Partial
transfers belong to caller loops. The native caller keeps the fd and borrowed
buffer stable through terminal delivery or closure. Pending cancellation removes
the registration before queueing, while a committed result stays fixed. This
fixture establishes the native event and resource contracts.

Native async imports and exports use the existing callable execution kind,
cold-call operation, failure set, and Operation representation. A host can pass an
explicit native context to an exported source root and drive it with its own pump.
Non-snapshot Read has one source borrowing policy across synchronous factories,
async functions, and API boundaries. Cold calls retain the source holder until
completion or closure; C++ still owns hidden resource validity. The
[source/native fixture](../tests/interop/async/bridge.cv) exercises this chain.
Character success is validated at the shared await consumer after failure and
cancellation transfer, without inserting a second provider coroutine.

## Fixed composition

Fixed composition retains the same accountable lifetime owner and closing rule.
Its public API and tuple/result admission are deferred. The draft result and
lifetime choices are:

| Form | Result selection and lifetime |
| --- | --- |
| `when_all` | Start every child; deliver an argument-ordered heterogeneous tuple after all close |
| `when_any` | Commit one value/failure/cancelled winner; request and close losers before delivery |
| `first_successful` | Choose a successful child, continuing past failed or cancelled candidates |
| `timeout` | Select operation completion or a local deadline, then close operation and timer |

Argument evaluation and operation startup are separate ordering rules. A fixed
composition evaluates operands in source order, establishes their storage, and
starts each child in argument order. Starting later children still occurs when an
earlier child completes inline with failure. `when_all` requests sibling
cancellation after failure/cancelled completion; nominal failure takes precedence
over cancellation and the first committed failure is retained.

Competitive selection needs a stable commitment rule for simultaneous ready
candidates, an explicit branch result type, and secondary-outcome policy. A winner
cannot publish while losers can still reach borrowed backing. Tuple decomposition
must account for the complete result owner and every element. See
[tuples](tuples.md) for the product admission work.

Timeout uses a concrete monotonic-clock provider. Relative duration starts at
composite start; zero, negative, overflowed, and already-due durations need explicit
rules. A deadline failure is distinct from ambient cancelled completion. Selection
of a deadline does not bound return latency when closure waits for an unresponsive
operation.

## Separate design boundaries

Cross-thread execution requires value admission, synchronization, and visibility
contracts from [concurrency](concurrency.md). Runtime-sized groups, streams,
channels, shared tasks, supervisors, ownership transfer, general async resource
disposal, and custom frame allocation have no design selected here.

## Evidence

Tests establish accepted and rejected source uses, retained backing, lexical
closing order, cancellation request versus acceptance, coroutine context, native
in-place identity and transfer/destruction, and bounded native stack growth.
Equivalent C++ representations may satisfy these contracts.

The [benchmark protocol](../benchmarks/async/README.md) defines workloads,
measurement boundaries, comparison conditions, and recorded evidence. Performance
observations apply to their archived implementations and hosts.

## References

- [Async programming](../notes/async-programming.md): C++ mechanisms and provider comparisons with primary sources.
- [Source reference](../docs/language/async.md): current syntax and admission.
- [Ownership analysis](../docs/compiler/analysis/ownership.md): shared state and interprocedural queries.
- [C++ realization](../docs/compiler/backend/realization.md): evaluation, storage, and closing scopes.
