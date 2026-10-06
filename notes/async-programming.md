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

## Implementations and libraries

stdexec describes itself as a C++26 reference implementation of `std::execution`.
It contains three layers:

- the standard `std::execution` model and operations;
- the `stdexec` spelling used by the reference implementation;
- non-standard `exec::` and `nvexec::` extensions for scopes, tasks, I/O,
  thread pools, and GPU execution.

Its extensions provide implementation examples for environment propagation,
operation-state ownership, cancellation, coroutine adapters, and specialization.

Other libraries provide useful contrasts:

| Project | Useful subject of study |
| --- | --- |
| async_simple | Lazy coroutines, futures, executors, and cooperative cancellation |
| libcoro | Coroutine tasks, I/O scheduling, task groups, and thread migration |
| Boost.Asio | Coroutine adapters over an established I/O and executor model |
| cppcoro | Coroutine-first tasks, shared tasks, async primitives, and cancellation tokens |

Compare libraries by following one operation through construction, start,
completion, cancellation, and destruction. Ask who owns each transition and
which execution context may perform it.

## References

### C++ coroutine language rules

- C++20 draft N4861: [coroutine transformation](https://timsong-cpp.github.io/cppwp/n4861/dcl.fct.def.coroutine),
  [await protocol and restrictions](https://timsong-cpp.github.io/cppwp/n4861/expr.await),
  [coroutine return](https://timsong-cpp.github.io/cppwp/n4861/stmt.return.coroutine), and
  [destructor restrictions](https://timsong-cpp.github.io/cppwp/n4861/class.dtor).
- C++ working draft: [coroutine definitions](https://eel.is/c++draft/dcl.fct.def.coroutine),
  [`co_await`](https://eel.is/c++draft/expr.await),
  [`coroutine_handle`](https://eel.is/c++draft/coroutine.handle.resumption).

### Completion, composition, and structured lifetime

- C++ working draft: [async operation requirements](https://eel.is/c++draft/exec.async.ops),
  [`when_all`](https://eel.is/c++draft/exec.when.all),
  [schedulers](https://eel.is/c++draft/exec.sched), and
  [async scopes](https://eel.is/c++draft/exec.scope).
- WG21 papers: [P2175R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2175r0.html),
  [P2300R10](https://www9.open-std.org/JTC1/SC22/WG21/docs/papers/2024/p2300r10.html),
  [P3149R11](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3149r11.html),
  [P3552R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3552r3.html), and
  [P4007R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4007r3.pdf).

### Implementation examples

- [stdexec reference implementation](https://github.com/NVIDIA/stdexec): standard-facing facilities and extensions.
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
