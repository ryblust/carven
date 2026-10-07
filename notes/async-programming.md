# Coroutines, execution, and lifetime

Async syntax expresses work that may finish later as sequential control flow.
An `await` may suspend execution, move its continuation elsewhere, and retain
state after the current stack has unwound. Three questions describe that behavior:

| Question | What it asks |
| --- | --- |
| Control | Who suspends, who resumes, and where does the result continue? |
| Execution | Which resource runs the work and its continuation? |
| Lifetime | Who owns the running state, and when is it safe to destroy? |

A coroutine supplies suspension and resumption. Its surrounding protocol
defines execution and lifetime.

## Async operation lifecycle

```text
description or task value
          |
          | await, connect + start, or spawn
          v
   running operation
          |
          | one terminal completion
          v
    value | error | stopped
```

The description says what could run. The operation state represents one
particular run. A terminal completion ends the protocol for that run; the owner
may destroy its state only once the contract guarantees that no callback,
continuation, or execution agent can touch it again.

Completion may happen inside `start` before the call returns. The protocol is
still asynchronous because its caller cannot assume either synchronous or
deferred completion.

Operations also differ in when they begin. A cold or lazy operation starts
when it is awaited or explicitly started. A hot or eager operation may already
be running when its handle reaches the caller. Dropping a cold description can
mean discarding work that never began; dropping a handle to hot work demands a
separate answer about who still owns the running state.

## C++20 coroutine mechanisms

C++20 supplies a language transformation for suspension and resumption. Task
types, event loops, schedulers, I/O, cancellation, and structured-concurrency
policies are supplied by the surrounding program or library.

A function containing `co_await`, `co_yield`, or `co_return` becomes a
coroutine. Its return type and parameters select a `promise_type` through
`std::coroutine_traits`. Conceptually, the transformation performs this work:

```text
copy parameters into coroutine state
construct the promise
produce the caller-facing object with get_return_object()
co_await initial_suspend()
run the function body
route an escaping exception to unhandled_exception()
co_await final_suspend()
let an owner destroy the coroutine state at a valid time
```

The coroutine state, often called the frame, contains the promise, parameter
copies, suspension state, and locals that survive a suspension point. Its storage
may be allocated or elided according to the implementation and program. Reference
parameters retain their referents' existing lifetime requirements.

The promise controls the caller-facing object, initial and final suspension,
`co_return`, exception handling, and optional `await_transform`. Its policies
determine hot or cold start and result representation.

The await protocol is compact:

```text
await_ready()
  true  -> continue directly to await_resume()
  false -> mark the coroutine suspended
           call await_suspend(current_handle)
           continue when resumed, possibly immediately
           continue through await_resume()
```

`await_suspend` may return `void`, `bool`, or another coroutine handle. A `false`
boolean result resumes the awaiting coroutine immediately. The handle form
supports symmetric transfer: one coroutine can hand control directly to another
without first returning through an external scheduler.

An awaiter may publish the suspended handle to another thread. That thread can
resume the coroutine before `await_suspend` has returned, so code that has
published the handle cannot continue to assume exclusive access to the frame.
The standard also places conditions on cross-execution-agent resumption, and
concurrent resumption of one coroutine can cause a data race.

Finally, reaching `final_suspend` is not the same as destroying the frame.
Destruction remains an ownership operation and must not race with a possible
resume or external callback.

### Frame storage and allocation elision

Coroutine allocation elision depends on visible creation, ownership, and
destruction. P0981's HALO examples expose the coroutine ramp, return-object
construction, owner moves, await protocol, and handle destruction to native
analysis; they do not require inlining the whole coroutine body or executor.
LLVM describes storing a frame in its caller when the caller creates, uses, and
destroys it under RAII. An awaiter can borrow the handle while a local owner keeps
frame destruction at the caller's lifetime exit. Generated artifacts establish
whether that representation permits elision in a particular build.

Scheduling can obscure the relationship between completion and destruction.
P2477R3 examines that limit and proposes a `promise_type::must_elide` interface
for controlling allocation elision from lifetime knowledge supplied by the
program. This is a proposed interface, not a C++20 guarantee or a mechanism used
by Carven's current runtime. Source lifetime proofs still need a native
representation that preserves their ownership boundary.

## Semantic compilation and library protocols

A semantic compiler can analyze source owners, lifetime exits, failure edges,
suspension sites, and statically bounded children before choosing a target
representation. A C++ library receives composition and lifetime intent through
calls, types, and protocol contracts. Both approaches can specialize concrete
result types and eliminate wrappers when the available facts permit it.

The surrounding protocol still provides a caller-facing object, promise and
awaiters, completion delivery, handle ownership, and the cancellation and
execution support its contract requires. These responsibilities can use private
concrete types, templates, or reusable library abstractions. Generated artifacts
and workload measurements establish their costs.

Concrete sender types expose fixed composition shape, completion types,
and cancellation-token properties to templates. P2175 explains both cancellation
elimination for a statically unstoppable caller and in-place stop storage under
structured lifetime, avoiding shared-state allocation and reference counting.
Native C++ optimizers can also reduce live state and elide some coroutine
allocations. These opportunities are shared with semantic compilation.

Carven's additional source facts are lexical child ownership, all lifetime-exit
edges, single consumption, retained cold-operation loans, access admission across
suspension, and nominal failure propagation. It can enforce those facts before
native generation and specialize a same-thread runtime under an explicit
execution contract. Generated C++20 coroutines still have native frame and
allocation constraints; stronger cost guarantees require artifact inspection
and workload evidence.

Primary references: [P2175R0, section 5.10](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2175r0.html)
and [P2300R10](https://www9.open-std.org/JTC1/SC22/WG21/docs/papers/2024/p2300r10.html).

## Closing before destruction

Async closure may need to suspend while children stop. It must run while every
object those children can still reach remains alive. `co_return` exits the body,
destroying its automatic objects before the final suspend point. An earlier
nested scope can end a borrowed local's lifetime even sooner. A lowering must
therefore arrange closure before each relevant lifetime exit, preserve backing
storage elsewhere, or prove that no child can still access it.

An ordinary destructor cannot be a coroutine, so it cannot supply a hidden
asynchronous join. Final suspension can transfer control to a continuation, but
its expression is required not to be potentially throwing. Cancellation and
external callback revocation belong to the operation protocol.

Native C++20 `co_await` cannot occur inside an exception handler. A bridge that
catches exceptions and then needs asynchronous cleanup can first map the error
to retained state and perform cleanup after leaving the handler. A source
language's typed-failure branch need not become a native C++ exception handler.

## Suspension and persistent state

Suspension separates runtime behavior from persistent runtime state. Values
used only before a suspension point need not become part of a coroutine frame.
Values, ownership obligations, destruction state, and control positions needed
after resumption must survive after the current stack can unwind, so some
continuation or operation representation has to carry them.

The same distinction applies to terminal completion. A synchronously completed
child may transfer value, failure, or cancellation directly through control
flow. If completion arrives after its parent suspends, the selected result and
the state required to resume the parent must persist until observation. Its
representation can be specialized to the completion and lifetime contract.

Kotlin's specified CPS transformation makes this boundary concrete: a
suspendable function receives a continuation, and a suspendable lambda becomes
a state machine whose fields preserve locals needed across suspension. C# task
semantics similarly record an asynchronous exception in a faulted task until
an `await` observes and rethrows it. Both cases turn sequential-looking source
control into stored state only where temporal separation requires it.

## Logical tasks and threads

A logical task is the causal path currently being advanced through an async
operation tree, together with context such as cancellation, scheduling,
allocation, tracing, or deadlines. Its identity can span coroutine frames and
execution resources.

One logical task can resume on several threads. One thread can advance many
logical tasks. Thread-local state therefore does not automatically become
task-local state, and moving a continuation between threads is a semantic
choice rather than a consequence of suspension.

Awaiting a child directly often suspends the parent while the child advances.
It creates sequential async control flow, not necessarily sibling concurrency.
Starting several operations with `when_all`, or spawning work before the
parent continues, introduces overlap and therefore a lifetime relationship.

## Structured concurrency and ownership

Structured concurrency makes the lifetime tree resemble the control-flow
tree:

```text
parent operation or async scope
├── child A
├── child B
└── child C
```

Before reclaiming its frame, locals, buffers, or other resources, the parent
establishes that every child has stopped accessing them. This may require an
asynchronous join or synchronous revocation. Ownership transfer is another
option when a longer-lived owner also retains every resource the child needs.
Terminal completion suffices only when its protocol guarantees that access has
ended.

Detached work belongs to a runtime, service, process, background scope, or
self-owned state with defined shutdown, error observation, and resource limits.

A frame retains its stored objects. The targets of `string_view`, `span`, raw
pointers, iterators, references, and `this` need separate lifetime coverage.
Coroutine lambda captures belong to the closure object rather than being
automatically promoted into the coroutine frame. If the closure dies after
the first suspension, later access can be a use-after-free.

## Cancellation protocol

Cancellation has at least three distinct moments:

```text
request      someone says the result is no longer wanted
acceptance   the operation observes the request at a safe point
completion   cleanup finishes and the operation reports a terminal outcome
```

A stop token provides a channel for requests. An operation may poll it or
register a callback. The callback itself has a concurrency contract: it can run
synchronously on the thread requesting stop, and registration, invocation, and
unregistration may race.

Cancellation is usually cooperative. An operation may not support it, may
finish before seeing it, may delay acceptance until an invariant is restored,
or may need asynchronous cleanup after accepting it. A successful
`request_stop()` changes stop state; it does not prove that the target has
stopped.

Propagation is policy too. A parent request may flow to children, one child's
failure may or may not stop siblings, and cleanup may be shielded from an
ambient request. None of these choices comes from the coroutine language
mechanism.

The sender/receiver vocabulary in `std::execution` makes three terminal
channels explicit: `set_value`, `set_error`, and `set_stopped`. Other systems
may use exceptions, error codes, `expected`, or a tagged result. Each protocol
defines its completion channels and ownership after delivery.

## Composition semantics

`when_all` defines start order, result shape, error arbitration, cancellation
propagation, and lifetime closure. One
structured design starts every child, requests sibling stop after an error or
stopped completion, and waits for every child to finish before completing the
outer operation.

That distinction gives “fail fast” two meanings. A group can request sibling
stop as soon as the first failure arrives while still delaying its own return
until every child is safe to destroy. An uncooperative child can therefore
delay a structurally safe failure result.

`when_any` makes the losing operations the central problem:

```text
winner becomes known
  ├── request stop and drain every loser
  ├── transfer losers to an outer scope
  └── detach them to another explicit owner
```

Different libraries choose different branches, so the name alone does not
describe the lifetime guarantee. Tie-breaking when several candidates are
already complete is another policy decision.

A timeout is the same shape with a timer as one candidate. The winner needs a
linearization rule; the loser needs cancellation and drainage or an ownership
transfer. A deadline can stop observation immediately without proving that the
underlying work has stopped. A timeout contract specifies whether return waits
for that work to close or transfers its ownership.

## Execution context

An execution resource is where work runs: an event loop, thread pool, strand,
GPU stream, or the inline caller. A scheduler is an interface for asking that
resource to run a unit of work. Neither concept is the identity of the logical
task.

Starting an operation, completing a leaf, delivering a completion signal, and
resuming a continuation can all happen in different contexts. Returning to an
original context requires a runtime to preserve and restore affinity; a
coroutine does not do it automatically.

A single-threaded event loop removes many data races but not reentrancy. Inline
completion can resume user code inside a call that appeared to initiate work,
and long chains of symmetric or inline transfer need a stack-growth policy.
Schedulers likewise do not imply fairness, backpressure, or resource bounds
unless their contracts say so.

## Sender/receiver protocols

The core sender/receiver lifecycle separates description, binding, and start:

```text
sender
  | connect(sender, receiver)
  v
operation state
  | start(state), once
  v
set_value(...) | set_error(error) | set_stopped()
```

In the C++ execution protocol, the operation state must remain alive until the
completion operation begins. The receiver may destroy it during completion, so
the producer must finish accessing that state before invoking the completion
handler. A sender advertises completion signatures; a receiver supplies
completion operations and an environment. The environment is an open-ended place for schedulers, stop tokens, allocators,
domains, and other contextual queries.

The protocol supports different runtime layouts. A lazy sender graph can be
specialized for a CPU pool, an I/O backend, or a GPU. Customization and deeply
nested template types introduce API and compilation complexity; allocation,
dispatch, and runtime costs depend on the implementation and workload.

An adapter can make an operation protocol awaitable, while a coroutine-backed
task can expose an operation protocol to non-coroutine consumers.

### Completion boundaries and coroutine adaptation

The current execution protocol requires child operations to complete before
their parent. Its async lifetime ends when the completion operation begins;
the receiver may destroy operation state during that call. The producer cannot
continue to access invalid operation state after completion. A conforming sender
therefore cannot leave callbacks that will later access that state. A general
C++ awaitable or external callback provider may expose a weaker boundary and
needs explicit closure evidence before being admitted as a Carven operation.

The standard `when_all` starts every child in argument order, including children
whose start follows an inline error from an earlier child. Error and stopped
completion request sibling stop; all children complete before the aggregate
publishes a result. Error takes precedence over stopped, and the first committed
error is retained. The source-language ordering of argument evaluation remains
independent of that operation-start order.

The standard task and awaitable adapters illustrate why adaptation is a separate
contract. `task::promise_type::unhandled_stopped` destroys its coroutine frame
before reporting stopped, while `with_awaitable_senders` propagates stopped
without resuming the original coroutine. A Carven owner with active lexical
children must instead complete its potentially suspending closing epilogue before
destroying child-reachable storage. Its bridge must route cancellation into that
epilogue rather than adopt a direct frame-destruction path.

Primary references: [async operations](https://eel.is/c++draft/exec.async.ops),
[`when_all`](https://eel.is/c++draft/exec.when.all),
[`task::promise_type`](https://eel.is/c++draft/task.promise), and
[`with_awaitable_senders`](https://eel.is/c++draft/exec.with.awaitable.senders).

### Execution context and stack behavior

A receiver environment carries execution-time properties; sender attributes
describe properties of the work, including known completion schedulers. These
are different query surfaces. The current task obtains its starting scheduler
from the receiver environment and adapts awaited senders with `affine` unless
its selected scheduler is inline. `affine` restores completion to the receiver's
scheduler and can avoid scheduling when the correct affinity is already known.
This does not supply a language-wide fairness or preemption contract.

The generic sender-awaitable adaptation starts the operation from
`await_suspend`; value/error receivers resume the continuation directly. Inline
completion can therefore run on the initiation stack. A coroutine runtime that
requires bounded stack growth needs a handshake, trampoline, or another safe
transfer implementation. The generic `void` start/completion interface delegates bounded dispatch to the
implementation; an internal trampoline can provide it.

Primary references: [`task::state`](https://eel.is/c++draft/task.state),
[`task::promise_type`](https://eel.is/c++draft/task.promise),
[`affine`](https://eel.is/c++draft/exec.affine), and
[`as_awaitable`](https://eel.is/c++draft/exec.as.awaitable).

## Implementations and libraries

stdexec supplies standard-facing facilities under `stdexec::`, generic
extensions under `exec::`, and GPU facilities under `nvexec::`. The C++26
[N5015 editors' report](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/n5015.html)
records adoption of both coroutine task and counting-scope proposals. The current
NVIDIA `hello_coro` example uses `stdexec::task`; `exec::task`,
`exec::async_scope`, and Linux `exec::io_uring_context` have their own interfaces.
Compare the selected namespace, facility contract, and revision when choosing a
provider. Consumer-library availability remains separate from Carven's generated
C++20 baseline.

Primary references: [stdexec README](https://github.com/NVIDIA/stdexec/blob/main/README.md),
[task](https://eel.is/c++draft/exec.task),
[execution scopes](https://eel.is/c++draft/exec.scope), and
[spawn](https://eel.is/c++draft/exec.spawn).

Provider comparison follows construction, start, completion, cancellation, and
destruction. The linked versions exhibit these relevant choices:

| Provider | Mechanism and adaptation concern |
| --- | --- |
| Boost.Cobalt | Lazy `task`, eager `promise`, fixed composition, and explicit teardown. `race` can interrupt observation before the underlying operation closes. |
| Intel bare-metal senders | Interrupt-driven execution, static specialized operation storage, and no-exception completion. `when_any` selects value/error and waits for all candidates to complete. Interrupt delivery requires its own resume-context contract. |
| async_simple | `collectAny` defaults to leaving losers running. Termination requests and loser closure are separate operations. |
| libcoro | Coroutine tasks and schedulers. `when_any` retains loser controllers after returning the winner; the caller's borrowed backing still needs lifetime coverage. |
| Boost.Asio | A single-slot, non-sticky cancellation signal. A bridge supplies retained requests, independent registrations, and safe emit/registration order. |
| cppcoro | Coroutine-first tasks, shared tasks, async primitives, and cancellation tokens. |

Sources: Cobalt [task](https://raw.githubusercontent.com/boostorg/cobalt/boost-1.92.0/include/boost/cobalt/detail/task.hpp),
[race](https://raw.githubusercontent.com/boostorg/cobalt/boost-1.92.0/include/boost/cobalt/detail/race.hpp),
and [with](https://raw.githubusercontent.com/boostorg/cobalt/boost-1.92.0/include/boost/cobalt/with.hpp);
Intel [library](https://github.com/intel/cpp-baremetal-senders-and-receivers) and
[`when_any`](https://intel.github.io/cpp-baremetal-senders-and-receivers/#_when_any);
async_simple [cancellation](https://alibaba.github.io/async_simple/docs.en/SignalAndCancellation.html)
and [Collect.h](https://raw.githubusercontent.com/alibaba/async_simple/main/async_simple/coro/Collect.h);
[libcoro when_any](https://raw.githubusercontent.com/jbaldwin/libcoro/main/include/coro/when_any.hpp);
[Asio cancellation](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/core/cancellation.html).

A production bridge pins a provider version and verifies completion mapping,
request propagation, callback quiescence, and backing coverage together.

## Provider examples

Timer and local I/O examples expose registration, event delivery, result storage, and
resource closure independently of their surface composition API.

| Reference | Relevant mechanism |
| --- | --- |
| Cobalt [delay](https://github.com/boostorg/cobalt/blob/develop/example/delay.cpp) and [echo server](https://github.com/boostorg/cobalt/blob/develop/example/echo_server.cpp) | Asio steady timer, socket read/write, and explicit resource closure |
| NVIDIA [hello_coro](https://github.com/NVIDIA/stdexec/blob/main/examples/hello_coro.cpp) and [io_uring](https://github.com/NVIDIA/stdexec/blob/main/examples/io_uring.cpp) | Await sender completions in a task; timed scheduling and stopped completion through Linux providers on separate event threads |
| Intel [timer manager](https://github.com/intel/cpp-baremetal-senders-and-receivers/blob/main/include/async/schedulers/timer_manager.hpp) and [scheduler examples](https://intel.github.io/cpp-baremetal-senders-and-receivers/) | Platform timer/interrupt injection and specialized operation storage |
| async_simple [echo server](https://github.com/alibaba/async_simple/blob/main/demo_example/async_echo_server.cpp) and [Asio adapter](https://github.com/alibaba/async_simple/blob/main/demo_example/asio_coro_util.hpp) | Callback-owned result delivery and coroutine restoration |
| libcoro [scheduler example](https://github.com/jbaldwin/libcoro/blob/main/examples/coro_scheduler.cpp) | TCP request/response and manual or threaded event processing |
| cppcoro [I/O and networking examples](https://github.com/lewissbaker/cppcoro/blob/master/README.md#io_service) | Cancellable delay, socket operations, and joining an async scope; documented implementation uses Windows I/O |

The standard [generator contract](https://eel.is/c++draft/coro.generator) and
[MSVC implementation](https://github.com/microsoft/STL/blob/main/stl/inc/generator)
show a separate owner and borrowed iterator. Nested generators retain their owner
in the yielding frame and transfer to the parent at final suspension. The iterator
resumes the active generator when asked for another element. Ordinary `co_await`
is disabled. This supplies a useful ownership and native-transfer example;
external event registrations require their own cancellation and quiescence proof.

Asio's [timer cancellation contract](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/basic_waitable_timer/cancel.html)
preserves an already queued successful completion. A pending cancelled wait still
invokes its [completion handler](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/basic_waitable_timer/async_wait.html).
An adapter therefore retains handler-reachable state until that delivery is safe;
requesting cancellation alone does not release it. The native fixture below owns
its registrations directly, so removing one ends its external reachability.

The Carven timer fixture uses a real `steady_clock` timer on the
existing single-thread driver. An optional native root pump checks registrations
before queued dispatch and blocks on a pending deadline only when the queue is
empty. The timer fixes its completion, removes its registration, and enqueues its
existing continuation node. User execution occurs through the driver after that
handoff. The awaiter retains a bound `ResumeContinuation` for cancellation queries
and queued resumption. Cancellation reads the same task ancestry used by source
operations.

The [native fixture](../tests/interop/async/timer.hpp) owns timer registrations and
clock waiting. Its [contract cases](../tests/interop/async/timer.cpp) cover cold
construction, immediate and delayed completion, cancellation, closing, and release
before resumed execution. This fixture uses native operations and exposes no
source-level timer. Positive delays must produce a
representable steady-clock deadline. Zero and negative delays complete inline
unless cancellation is already requested. Large registration sets, cross-thread
resume, socket protocols, and external library callback teardown remain separate
evidence boundaries.

The [pipe fixture](../tests/interop/async/pipe.hpp) adds real POSIX nonblocking
`read_some` and `write_some` operations under the same root-pump contract. It
borrows a stable fd
and nonempty buffer; the native caller retains both until terminal delivery or
lexical closure. Each operation returns the byte count or a typed errno failure.
Partial transfers remain ordinary loop work for the caller.
[Read](https://pubs.opengroup.org/onlinepubs/9799919799/functions/read.html) selects
EOF only after buffered bytes are consumed;
[poll](https://pubs.opengroup.org/onlinepubs/9799919799/functions/poll.html) readiness
and hangup authorize another syscall rather than supplying its result.
[Write](https://pubs.opengroup.org/onlinepubs/9799919799/functions/write.html) retains
its native partial-transfer and SIGPIPE contract.

The OS accesses the buffer only during a synchronous nonblocking syscall; a
pending watch observes readiness and has no buffer-accessing completion handler.
Removing that watch therefore ends external reachability in this fixture.
EAGAIN retains the registration. Terminal selection removes it before queueing
the continuation, so later cancellation cannot revise a committed result. The
[contract cases](../tests/interop/async/pipe.cpp) cover buffered EOF, errors, pending
read and write, idle event waiting, cancellation closure, and rearming. A writer
thread in the idle-wait case accesses only the pipe; event collection and user
continuation remain on the driver thread. The fixture owns readiness snapshots
and blocking `poll`, without adding event state to Driver or TaskContext. It does
not by itself establish the source boundary or prove an external callback
library quiescent.

The [source/native bridge](../tests/interop/async/bridge.cv) exercises a native
host driving an exported source root that awaits imported native operations. The
host supplies the context and pump; imports forward the existing cold Operation
without an adapter activation. Non-snapshot Read borrows its source holder across
synchronous factories and async calls, including trivial native objects. Source
ownership checks retain that holder through completion or closure. The provider
and host remain responsible for hidden fd, buffer, and queue lifetimes. Native
errno maps to the source-owned IoError failure; cancellation and character
success use the existing completion consumers. The executable checks actual
context identity across nested awaits and saved cold operations.

The [source transfer loop](../tests/interop/async/transfer.cv) awaits actual read
counts, recognizes EOF, and advances its write cursor by each completed count.
The native host owns the buffers and pipe descriptors. Its contracts cover short
reads, real EAGAIN backpressure, nominal failure recovery, and cancellation closure.
The four-byte write requests establish pending writes; they do not establish OS
partial-write coverage.

A controlled pending measurement starts from an empty nonblocking pipe, reaches
EAGAIN, and injects one byte at the driver's idle boundary. Generated and native
Operation roots use the same external provider object and separate translation
units without LTO. Registration, event injection, wait, commit, and return intervals
include their clock reads and observation checks. This experiment measures event
recovery under those conditions; it does not measure blocked OS wakeup latency.

`const async` uses an execution-local cold descriptor in the existing semantic
executor. Construction acquires arguments; await consumes the descriptor through
ordinary invocation. The [decimal-code example](../tests/language/async/constant.cv)
uses the same typed failure and parsing bodies for compile-time fixtures and
runtime values. The [scheduling fixture](../tests/language/async/scheduling.cv)
uses the same source bodies for static and runtime FIFO, cancellation, and child
close contracts. Static tasks share a root budget and memory domain. This execution
uses no C++ constexpr coroutine support; native providers and real I/O require a
runtime host.

## References

### C++ coroutine language rules

- C++20 draft N4861: [coroutine transformation](https://timsong-cpp.github.io/cppwp/n4861/dcl.fct.def.coroutine),
  [await protocol and restrictions](https://timsong-cpp.github.io/cppwp/n4861/expr.await),
  [coroutine return](https://timsong-cpp.github.io/cppwp/n4861/stmt.return.coroutine), and
  [destructor restrictions](https://timsong-cpp.github.io/cppwp/n4861/class.dtor).
- C++ working draft: [coroutine definitions](https://eel.is/c++draft/dcl.fct.def.coroutine),
  [`co_await`](https://eel.is/c++draft/expr.await),
  [`coroutine_handle`](https://eel.is/c++draft/coroutine.handle.resumption).

- [P0981R0 — HALO: Coroutine Heap Allocation eLision Optimization](https://open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0981r0.html)
  (2018): visibility of coroutine creation, ownership, and destruction.
- [P2477R3 — Allow programmer to control coroutine elision](https://open-std.org/JTC1/SC22/WG21/docs/papers/2022/p2477r3.html)
  (2022): a proposed interface and the limits of native lifetime inference.
- [LLVM coroutine allocation elision](https://llvm.org/docs/Coroutines.html#avoiding-heap-allocations):
  caller-owned frame storage and conditional allocation/deallocation.

### Completion, composition, and structured lifetime

- C++ working draft: [async operation requirements](https://eel.is/c++draft/exec.async.ops),
  [`when_all`](https://eel.is/c++draft/exec.when.all),
  [schedulers](https://eel.is/c++draft/exec.sched), and
  [async scopes](https://eel.is/c++draft/exec.scope).
Historical papers explain design choices; the working draft supplies current
wording:

- [P2175R0 — Composable cancellation](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2175r0.html)
  (2020): cooperative requests, structured lifetime, in-place stop state, and
  cancellation-cost specialization.
- [P2300R10 — `std::execution`](https://www9.open-std.org/JTC1/SC22/WG21/docs/papers/2024/p2300r10.html)
  (2024): environment, completion, composition, customization, and coroutine interoperation.
- [P3149R11 — `async_scope`](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3149r11.html)
  (2025): counting-scope ownership and association/allocator-destruction ordering.
- [P3552R3 — Coroutine Task](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3552r3.html)
  (2025): task design, subsequently revised in the working draft.
- [P4007R3 — Open Issues in `std::execution::task`](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4007r3.pdf)
  (2026-05-01): informational discussion of allocation, error-return syntax,
  symmetric transfer, and fixes to earlier affinity/allocator designs.

### Implementation examples

- [stdexec reference implementation](https://github.com/NVIDIA/stdexec): standard-facing facilities and extensions.
- [Boost.Cobalt](https://github.com/boostorg/cobalt): coroutine tasks, promises, composition, and teardown.
- [Intel bare-metal senders/receivers](https://github.com/intel/cpp-baremetal-senders-and-receivers): embedded static operation and cancellation protocols.
- [async_simple](https://github.com/alibaba/async_simple): lazy coroutines, futures, executors, and cancellation.
- [libcoro](https://github.com/jbaldwin/libcoro): coroutine tasks, scheduling, and task groups.
- [Boost.Asio coroutine adapters](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/composition/cpp20_coroutines.html).
- [cppcoro](https://github.com/lewissbaker/cppcoro): tasks, async primitives, and cancellation tokens.

### Borrowing and cross-language state

- [C++ Core Guidelines coroutine-lambda guidance](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rcoro-capture).
- The [Kotlin coroutine specification](https://kotlinlang.org/spec/asynchronous-programming-with-coroutines.html)
  and [.NET async exception behavior](https://learn.microsoft.com/en-us/dotnet/csharp/asynchronous-programming/).

### Related reading

- [Failure models](failure-models.md): completion state and failure transport.
- [Compiler architecture](compiler-architecture.md): semantic knowledge and representation boundaries.
