# Async Programming: Control, Execution, and Lifetime

Async syntax makes sequential reasoning possible in a world where work may
finish later. That is its great ergonomic success, but also the source of many
bad mental models. An `await` looks like a function call even though it may
split control flow, move execution elsewhere, and keep state alive after the
current stack has unwound.

The most useful way to reason about async code is to keep three questions
separate:

| Question | What it asks |
| --- | --- |
| Control | Who suspends, who resumes, and where does the result continue? |
| Execution | Which resource runs the work and its continuation? |
| Lifetime | Who owns the running state, and when is it safe to destroy? |

A coroutine answers part of the control question. It does not, by itself,
answer the other two.

## Async operation lifecycle

It helps to begin with a deliberately plain lifecycle:

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

C++20 supplies a language transformation for suspension and resumption. It
does not supply a task type, event loop, scheduler, I/O system, cancellation
model, or structured-concurrency policy.

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
copies, suspension state, and locals that survive a suspension point. An
implementation may allocate additional storage for it; allocation elision is
an optimization opportunity, not the source-level meaning. A reference copied
into the frame is still only a reference and does not extend its referent's
lifetime.

The promise controls the caller-facing object, initial and final suspension,
`co_return`, exception handling, and optional `await_transform`. This is why
the presence of `co_await` alone does not tell us whether a task is hot or cold
or how its result is represented.

The await protocol is compact:

```text
await_ready()
  true  -> continue directly to await_resume()
  false -> mark the coroutine suspended
           call await_suspend(current_handle)
           resume later
           continue through await_resume()
```

`await_suspend` may return `void`, `bool`, or another coroutine handle. The
handle form supports symmetric transfer: one coroutine can hand control
directly to another without first returning through an external scheduler.

An awaiter may publish the suspended handle to another thread. That thread can
resume the coroutine before `await_suspend` has returned, so code that has
published the handle cannot continue to assume exclusive access to the frame.
The standard also places conditions on cross-execution-agent resumption, and
concurrent resumption of one coroutine can cause a data race.

Finally, reaching `final_suspend` is not the same as destroying the frame.
Destruction remains an ownership operation and must not race with a possible
resume or external callback.

## Suspension and persistent state

Suspension separates runtime behavior from persistent runtime state. Values
used only before a suspension point need not become part of a coroutine frame.
Values, ownership obligations, destruction state, and control positions needed
after resumption must survive after the current stack can unwind, so some
continuation or operation representation has to carry them.

The same distinction applies to terminal completion. A synchronously completed
child may transfer value, failure, or cancellation directly through control
flow. If completion arrives after its parent suspends, the selected result and
the state required to resume the parent must persist until observation. This
does not prescribe a general task object, heap allocation, or one universal
completion carrier; it identifies the lifetime that a selected backend must
realize.

Kotlin's specified CPS transformation makes this boundary concrete: a
suspendable function receives a continuation, and a suspendable lambda becomes
a state machine whose fields preserve locals needed across suspension. C# task
semantics similarly record an asynchronous exception in a faulted task until
an `await` observes and rethrows it. Both cases turn sequential-looking source
control into stored state only where temporal separation requires it.

Failure transport raises the same question about which distinctions must
survive for later observation.

## Logical tasks and threads

A logical task is the causal path currently being advanced through an async
operation tree, together with context such as cancellation, scheduling,
allocation, tracing, or deadlines. It is not an OS thread, a single coroutine
frame, or every child that the frame has spawned.

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

The parent cannot finish while a child can still access the parent's frame,
locals, buffers, or other owned resources. Before the parent closes, each
child must become terminal or its ownership must move to a longer-lived owner.

This resembles RAII, but async cleanup exposes an important mismatch: an
ordinary C++ destructor cannot suspend to wait for a child. Async scopes often
need an explicit asynchronous join, or a contract that makes destroying an
active scope invalid. The invariant is more general than a particular
`join()` call—the owner must establish quiescence before reclaiming state.

Detached work is not ownerless work. A runtime, service, process, background
scope, or self-owned state still has to own it and define shutdown, error
observation, and resource limits.

Coroutines also make borrowed lifetime bugs easy to hide. A frame preserves
objects stored in it, not the targets of `string_view`, `span`, raw pointers,
iterators, references, or `this`. Capturing coroutine lambdas are especially
subtle because captures belong to the closure object rather than being
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
may use exceptions, error codes, `expected`, or a tagged result. The important
property is not the spelling but a closed set of outcomes with clear ownership
after each one.

## Composition semantics

`when_all` is not merely “run these at once.” It defines start order, result
shape, error arbitration, cancellation propagation, and lifetime closure. One
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
underlying work has stopped. Calling both behaviors “timeout” hides a material
semantic difference.

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

The operation state must outlive completion. A sender advertises completion
signatures; a receiver supplies completion operations and an environment. The
environment is an open-ended place for schedulers, stop tokens, allocators,
domains, and other contextual queries.

This is a protocol, not a required runtime layout. It does not inherently need
virtual dispatch, heap allocation, or one executor type, and a lazy sender
graph can be specialized for a CPU pool, an I/O backend, or a GPU. Its cost is
primarily conceptual and compile-time complexity: customization and deeply
nested template types can become a language within the language.

Coroutines and senders are not exclusive alternatives. An adapter can make an
operation protocol awaitable, while a coroutine-backed task can expose an
operation protocol to non-coroutine consumers.

## Implementations and libraries

The sender/receiver model specified by P2300 is represented in the C++
execution control library through schedulers, senders, receivers, operation
states, environments, completion signatures, and the value, error, and stopped
completion channels.

[stdexec](https://github.com/NVIDIA/stdexec) is the main implementation specimen
for that model and describes itself as a C++26 reference implementation of
`std::execution`. It is important to distinguish three layers when studying it:

- the standard `std::execution` model and operations;
- the `stdexec` spelling used by the reference implementation;
- non-standard `exec::` and `nvexec::` extensions for scopes, tasks, I/O,
  thread pools, and GPU execution.

The extensions are valuable experiments, but their presence in stdexec does
not make them standard facilities. The implementation is most useful for
studying lazy composition, environment propagation, scheduler customization,
operation-state ownership, cancellation, coroutine interoperation, and how a
generic protocol specializes without requiring one runtime layout.

Other libraries provide useful contrasts:

| Project | Useful subject of study |
| --- | --- |
| [async_simple](https://github.com/alibaba/async_simple) | Lazy coroutines, futures, executors, and cooperative cancellation |
| [libcoro](https://github.com/jbaldwin/libcoro) | Coroutine tasks, I/O scheduling, task groups, and thread migration |
| [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/composition/cpp20_coroutines.html) | Coroutine adapters over an established I/O and executor model |
| [cppcoro](https://github.com/lewissbaker/cppcoro) | Coroutine-first tasks, shared tasks, async primitives, and cancellation tokens |

Library vocabulary alone does not establish semantics. A useful comparison
follows one operation through construction, start, completion, cancellation,
and destruction, then asks who owns each transition and which execution context
may perform it.

## Common misconceptions

- `co_await` does not mean “switch to a background thread”; the awaiter decides
  whether to suspend and who resumes the coroutine.
- A coroutine frame does not extend the lifetime of objects reached through
  references or views.
- A successful stop request is not a stopped completion.
- A `when_any` result does not prove that losing work disappeared.
- A timeout does not necessarily stop the operation it stopped observing.
- A single-threaded event loop still permits reentrant control flow.
- Structured concurrency does not forbid spawning; it gives spawned work an
  owner and a closure boundary.
- Destroying a coroutine handle destroys a frame, not necessarily the external
  operation that may still hold or use it.

## Sources

The primary language and library references behind this article are:

- C++ working draft: [coroutine definitions](https://eel.is/c++draft/dcl.fct.def.coroutine),
  [`co_await`](https://eel.is/c++draft/expr.await),
  [`coroutine_handle`](https://eel.is/c++draft/coroutine.handle.resumption),
  [async operation requirements](https://eel.is/c++draft/exec.async.ops),
  [`when_all`](https://eel.is/c++draft/exec.when.all),
  [schedulers](https://eel.is/c++draft/exec.sched), and
  [async scopes](https://eel.is/c++draft/exec.scope).
- WG21 papers: [P2175R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2175r0.html),
  [P2300R10](https://www9.open-std.org/JTC1/SC22/WG21/docs/papers/2024/p2300r10.html),
  [P3149R11](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3149r11.html),
  [P3552R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3552r3.html), and
  [P4007R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4007r3.pdf).
- The [stdexec reference implementation](https://github.com/NVIDIA/stdexec)
  and its distinction between standard-facing facilities and extensions.
- [C++ Core Guidelines coroutine-lambda
  guidance](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rcoro-capture).
- Cross-language materialization references: the
  [Kotlin coroutine specification](https://kotlinlang.org/spec/asynchronous-programming-with-coroutines.html)
  and [.NET async exception behavior](https://learn.microsoft.com/en-us/dotnet/csharp/asynchronous-programming/).
