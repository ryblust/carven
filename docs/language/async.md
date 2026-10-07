# Async functions and lexical children

[Language](README.md)

Async functions produce cold operations consumed by prefix `await` or started as
lexical children. Source and native producers share the completion and lifetime
contracts below.

## Cold operations and await

```carven
import std::async using yield_once;

async fn next(value: i32) -> i32 {
    await yield_once();
    return value + 1;
}

async fn main() {
    let operation = next(4);
    println(await operation);
    async let child = next(8);
    println(await child);
}
```

Calling an `async fn` constructs an operation without executing its body. The
function's `-> T` is its successful completion type. Its declared `throw` set is
delivered when awaited. Operations with equal success and solved failure types
have the same type even when produced by different functions. The operation type
is inferred; it has no source type spelling.

`await` starts and consumes an operation exactly once. A direct await executes a
dependency in the current logical task; it does not implicitly yield. Execution
continues until the dependency actually suspends or completes. A named operation
does not need `&&` for await or child startup. Ordinary ownership transfer still uses
`&&`; operations cannot be copied, reassigned, captured, or stored in aggregates.
An unused named cold owner is destroyed at scope exit without executing its body.
A bare cold-operation expression statement, such as `next(4);`, is rejected.
Private synchronous functions may return an inferred cold operation when analysis
can prove its retained backing remains valid. An async successful result cannot
itself be an operation.

`await action()?` awaits completion before propagating its nominal failure.
Ordinary `try`/`catch` handles those declared failures. Cancellation is a separate
completion and does not add a nominal type to the failure set. Await is admitted
in async bodies and static roots, blocks, and tests. Structured expressions,
arguments, returns, and loop steps may contain await and preserve source evaluation order.

## Lexical children and cancellation

`async let child = operation;` queues the operation's initial execution at the
same-thread loop tail and gives the current lexical scope responsibility for its
lifetime. A child cannot be copied or transferred. It may be awaited once. Every normal
path out of its owning scope must either observe it or call `cancel(child)`.

`cancel(child)` makes an idempotent cancellation request and returns immediately.
It does not release retained backing. Scope closure waits until its children have
finished before destroying their backing or completing the parent. On failure or
cancelled exits, closure requests cancellation automatically. Return and other
exit operands are evaluated before closing children; taking retained backing in
such an operand remains invalid. A callee has its own child scope and does not
wait for siblings started by its caller, even though direct awaits share the
same logical cancellation task.

The compiler-defined operations are selected by importing `std::async`:

| Operation | Behavior |
| --- | --- |
| `cancel(child)` | Request cancellation of the named lexical child |
| `cancellation_requested()` | Read the current task's request; false outside async execution |
| `cancellation_point()` | Cold void operation accepting a pending request as cancelled completion |
| `yield_once()` | Cold void operation that queues the current task continuation at the FIFO tail when executed |

These operations require resolved direct calls; taking an intrinsic as a callable
value, capturing it, or passing it as a function argument is rejected.

Constructing a cold yield or cancellation checkpoint does not execute it. The
checkpoint reads the request when started or awaited. Yield does not implicitly
accept cancellation. A synchronous helper called by an async body observes that
body's cancellation context. Cancellation is cooperative: a child that never
reaches an accepting operation can delay scope closure.

## Retained access and admission

Cold operations and active children retain the backing needed by their inputs.
Write access remains nonexclusive and scalar mutation may continue; taking or
reconstructing retained owner storage is rejected until its loan closes. A
cancellation request alone does not close that loan. Observation closes the active
loan while the lexical scope retains responsibility for the child state.

Async inputs admit scalar Read, recursively pure Carven aggregate/enum Read,
scalar Write, owning Take values without external referents, and opaque native
Read holders, also when contained in Carven aggregates. Native Read borrows
retain the source holder through the same lifetime loan as cold Write; taking or
replacing it before completion or closure is rejected. Cold native borrows require
known source backing, such as a named holder. Opaque native temporaries and
reference query results do not establish that backing. Borrowed text, slices,
pointers, callable views, and SIMD Read inputs are excluded. Unknown native owning
Take inputs are also excluded.

Native local operations follow the ordinary declared interoperation contract.
Carven checks source-visible loans, destructive access, and escaping backing.
Native Write is checked as potentially invalidating its selected storage. Native
providers own hidden referent lifetimes, retention, reentry, and value
validity. The compiler does not inspect native implementation code. Result storage
supports immovable values in place and preserves observable source transfer and
cleanup.

Executed synchronous helpers in an async body may not transitively stop a test
through `require` or `fail`. Constructing a cold operation is distinct from
executing its body for this effect check.

The current driver is single-threaded and nonreentrant, with bounded native
stack use for deep await chains. It does not promise preemption, fairness, or
bounded cancellation latency. `async fn main()` accepts zero parameters and is
driven to completion by a private entry wrapper. Normal completion returns process
status zero regardless of its source result; failure or cancelled completion is
reported after child closure.

Async tests, class receivers, async callable values, arbitrary external awaitable
types, cross-thread resume, combinators, and timeouts remain outside this admission
boundary.

## Static parameters and execution

Async source functions can declare `const` parameters using the ordinary
[static parameter rules](functions.md#static-parameters). Static arguments select
a checked residual body; generated calls and signatures contain only runtime
inputs. They do not become retained cold captures. The source function retains
one result and failure contract across its instances.

`const async fn` also admits execution in the static stage:

```carven
const async fn next(value: i32) -> i32 => value + 1;
const answer = await next(4);
```

Construction evaluates inputs once and stores a cold operation; await consumes
it and executes its body in the same semantic execution domain. Stored operations,
unused cold owners, Write access to live local backing, and nominal failure
propagation follow the ordinary ownership and evaluation rules. A private
synchronous `const fn` may return an inferred cold operation. Its ordinary body
cannot use await; static roots, `const` blocks, and `const test` bodies can.
Operations are execution-local values and cannot be frozen as constants.

Static execution admits marked source callees, lexical children, and the async
intrinsics. Child startup and explicit yield use a FIFO queue; a direct await
remains in the current task. Cancellation is requested separately from acceptance
at a checkpoint. Children close before their borrowed local backing is released,
using the same source rules as runtime execution.

All tasks in a static root share its execution limits and memory domain. Native
provider bodies and real I/O are outside static execution capability. Runtime
calls to the same source definition use C++ coroutines.

## Native providers and hosts

`import(cpp) async fn` declares a native function that returns a cold
`carven::runtime::async::Operation<Result, Failures...>`. Its source result and
failure declarations describe completion, not operation construction. Native
imports undergo the same async signature checks even without a source body.
`export(cpp) async fn` exposes a source async function through its module's existing
C++ API header with the same cold Operation representation.

A native host owns its event pump and provider resources, passes an explicit
context to the exported root, and calls `drive_root(operation, &pump)`. Context
Read is a const-reference borrow. The native caller keeps its holder and hidden
resources alive until the cold owner is dropped or execution and child closure
finish. Source loans check the holder; they do not prove native fd, buffer, or
callback lifetimes. The provider maps events to Completion, releases external
reachability, and queues the existing continuation before source execution resumes.
Native provider awaiters use `ResumeContinuation` from the async runtime entry.
They bind the suspended coroutine, query its cancellation request, and enqueue
resumption through the supplied driver after committing completion. The provider
keeps the continuation alive through dispatch and performs event delivery on the
driver thread. The source entry wrapper has no external event pump.

Successful `char` values received by await are checked as Unicode scalar values,
including discarded awaits and direct completion returns. Failure and cancelled
completion transfer before that value check. Destroying an unstarted cold owner
does not receive a result.

The [source/native fixture](../../tests/interop/async/bridge.cv) exercises explicit
context borrowing, real pipe I/O, EOF, typed failure propagation, and cancellation
closure through the generated API.

[Source behavior tests](../../tests/language/async/core.cv),
[payload interoperation](../../tests/interop/async), and the
[benchmark protocol](../../benchmarks/async/README.md) record the executable
acceptance evidence and current performance.
