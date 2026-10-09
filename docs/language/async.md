# Async functions and lexical children

[Language](README.md)

Async functions produce cold operations consumed by prefix `await` or started as
lexical children. Source and native producers share the completion and lifetime
contracts below.

## Cold operations and await

```carven
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
have the same type even when produced by different functions. Write
`AsyncOperation<T>` for an operation without nominal failures, or
`AsyncOperation<T, throw Error + OtherError>` for its complete completion contract.
The type can also be inferred from a cold call. Inside a parameterized
declaration, `AsyncOperation<T, throw E>` uses an independent success type and
[failure-set parameter](failures.md#failure-set-parameters).
`AsyncOperation<void, throw ()>` and `AsyncOperation<void>` both complete successfully with
the unit value `()` and have no nominal failure alternatives. Cancellation remains
a separate completion.

`await` starts and consumes an operation exactly once. A direct await executes a
dependency in the current logical task; it does not implicitly yield. Execution
continues until the dependency actually suspends or completes. A named operation
does not need `&&` for await or child startup. Ordinary ownership transfer still uses
`&&`. Operations cannot be copied or captured. Ordinary owning bindings,
assignment, record fields, Tuple slots, and fixed arrays can hold and transfer
cold owners under the same Take and lifetime rules.
An unused named cold owner is destroyed at scope exit without executing its body.
A bare cold-operation expression statement, such as `next(4);`, is rejected.
Synchronous functions may accept and return typed cold owners when analysis can
prove their retained backing remains valid. Use a Take parameter to transfer
the complete owner:

```carven
fn relay(&&pending: AsyncOperation<i32>) -> AsyncOperation<i32> => &&pending;
```

An async successful result cannot itself contain an operation.

`await action()?` awaits completion before propagating its nominal failure.
Ordinary `try`/`catch` handles those declared failures. Cancellation is a separate
completion and does not add a nominal type to the failure set. Await is admitted
in async bodies and static roots, blocks, and tests. Structured expressions,
arguments, returns, and loop steps may contain await and preserve source evaluation order.

## Cold composition

The builtin composition operations `when_all`, `when_any`, and
`first_successful` use ordinary name lookup and require no import. Each takes
cold operations and returns a cold operation. They require a direct call and
cannot be used as callable values.

```carven
async fn main() {
    let results = await when_all(next(4), next(8));
    println(results.0, results.1);
}
```

| Operation | Successful result | Selection |
| --- | --- | --- |
| `when_all(a, b, ...)` | `(A, B, ...)` in input order | Every input succeeds |
| `when_any(a, b, ...)` | `(usize, T)` | First terminal completion |
| `first_successful(a, b, ...)` | `(usize, T)` | First successful completion |

Join preserves nested products and unit slots. An empty join succeeds with `()`;
one input produces a singleton Tuple. Selection requires at least one input and
a common successful type. Its index identifies the original input position.
Failure sets contain the union of the input failures; cancellation is separate.

Inputs are evaluated and transferred once in argument order. Stored operations
require Take (`&&`); lexical children cannot be passed. Construction retains the
inputs' backing without starting them. Discarding the unstarted composite
destroys its inputs without executing them. A later input-expression failure
also destroys the earlier inputs without starting them.

At execution, all inputs start in argument order before any child is dispatched.
Already terminal candidates are selected by input position; otherwise the first
published completion commits selection. Join requests sibling cancellation after
a selected failure or cancellation. Its first selected nominal failure commits
the outward failure. Child cancellation keeps selection open for a nominal
failure; without one, the closed group completes as cancelled.

`when_any` commits the selected terminal completion. `first_successful` continues
after failed or cancelled candidates. If none succeeds, it forwards the first
selected nominal failure, or cancellation if every candidate cancelled.

A pending parent cancellation is accepted before startup and before outward
commitment. Every composite closes its children before delivering the selected
outcome. Cancellation requests alone do not release borrowed backing. After
commitment, later cancellation and loser failures do not replace the outcome.
Result assembly follows ordinary value-transfer requirements.

## Deadlines

`timeout(operation, duration)` consumes one cold operation and a standard
`Duration`. It preserves the protected operation's successful type and adds the
standard `Timeout` and `RangeError` types to its nominal failures. These are
ordinary standard-library types. The builtin follows the same ordinary lookup
and direct-call rules as other composition operations. Duration literals and the
builtin require no type imports; import a type only when naming it in source,
such as a parameter annotation or a failure declaration.

Construction retains the protected input without executing it or reading the
clock. At execution, pending cancellation is accepted first. A nonpositive
interval then selects `Timeout` without starting protected work or reading the
clock. A positive interval calculates a fixed monotonic deadline before starting
either child. Unrepresentable deadline arithmetic produces `RangeError`.

The protected operation is queued before the timer. Terminal publication selects
one candidate; an already terminal pair gives the protected operation priority.
Pending parent cancellation is checked before selection and after observing the
selected completion, before committing the outward outcome. A protected winner
forwards its completion, including supported immovable native results. A timer
winner produces `Timeout`. Both children close before delivery. Once committed,
later cancellation requests and losing outcomes cannot replace the result.
Noncooperative protected work can delay closure beyond the deadline.

```carven
import std::time.duration using RangeError;
import std::time.timeout using Timeout;

async fn limited() -> i32 throw Timeout + RangeError {
    return await timeout(next(4), 100ms)?;
}
```

Native clock reads and timer providers require generated execution.

## Lexical children and cancellation

`async let child = operation;` queues the operation's initial execution at the
same-thread loop tail and gives the current lexical scope responsibility for its
lifetime. A child cannot be copied or transferred. It may be awaited once. Every normal
path out of its owning scope must observe it, close it, or use `cancel child;`.

`cancel child;` makes an idempotent cancellation request and returns immediately.
It does not release retained backing. Scope closure waits until its children have
finished before destroying their backing or completing the parent. On failure or
cancelled exits, closure requests cancellation automatically. Return and other
exit operands are evaluated before closing children; taking retained backing in
such an operand remains invalid. A callee has its own child scope and does not
wait for siblings started by its caller, even though direct awaits share the
same logical cancellation task.

`cancel` is a reserved keyword. Its statement accepts a named lexical child:

```carven
async let child = next(8);
cancel child;
```

It cannot be used as a callable, shadowed by a bare source name, or applied to a
cold operation. Parenthesized operands and arbitrary expressions are not admitted.

The following builtin callables are available without imports:

| Operation | Behavior |
| --- | --- |
| `cancellation_requested()` | Read the current task's request; false outside async execution |
| `cancellation_point()` | Cold void operation accepting a pending request as cancelled completion |
| `yield_once()` | Cold void operation that queues the current task continuation at the FIFO tail when executed |
| `await wait_ready(first, second, ...)` | Wait for a child's terminal completion; return its input position and committed cancellation bit as `(usize, bool)` |
| `await close(child)` | Request cancellation, wait for completion, discard its outcome, and release its active backing loan |

Async builtin names follow ordinary lookup. Local bindings, module declarations,
Carven imports, and explicit native selections may shadow them. Opened C++
namespaces provide fallback for names not otherwise resolved. The selected
declaration determines the call contract in both synchronous and async bodies.
Each async builtin requires a direct call; taking one as a callable value,
capturing it, or passing it as a function argument is rejected.

Constructing a cold yield or cancellation checkpoint does not execute it. The
checkpoint reads the request when started or awaited. Yield does not implicitly
accept cancellation. A synchronous helper called by an async body observes that
body's cancellation context. Cancellation is cooperative: a child that never
reaches an accepting operation can delay scope closure.

## Terminal waiting and explicit closure

`wait_ready` and `close` require direct `await` operands and named lexical
children belonging to the current activation. They cannot be stored as operations or callable
values. Candidates may have different result types and declared failure sets.

```carven
async let first = next(4);
async let second = next(8);
let (index, cancelled) = await wait_ready(first, second);
if index == 0 {
    await close(second);
    if cancelled { await close(first); } else { println(await first); }
} else {
    await close(first);
    if cancelled { await close(second); } else { println(await second); }
}
```

`wait_ready` requires at least one candidate. Each must be definitely unconsumed;
duplicates and candidates consumed on only some paths are rejected. Waiting
borrows children and does not consume a result, release backing, or satisfy
normal-exit observation. Repeated waiting may select the same child.

The index and cancellation bit describe the same terminal snapshot. A false bit
means success or nominal failure; ordinary await receives that outcome. If any
candidates are already terminal, the lowest input position wins. Otherwise the
first terminal publication commits the selection, even if other children finish
before the parent resumes.

`close` discards success, nominal failure, or child cancellation. After draining,
the child is consumed and its active loan closes. Closing an already consumed
child is a no-op, including after conditional observation. A cold operation is
not a valid operand.

Neither operation accepts parent cancellation. A pending request remains visible;
closing continues until completion and provider quiescence. Neither suppresses
traps, execution limits, or native protocol violations. Noncooperative children
can delay either operation indefinitely. Static execution follows these same
selection and closure rules.

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

Parameterized async functions obey these same rules. Known invalid shapes are
rejected when the definition is checked, including borrowed fields inside a
parameterized record or Tuple. A requirement depending on an unknown type
parameter is checked again against each concrete instance. An unknown type does
not establish Copy or independent backing. Known invalid arguments and completion
types at an async call inside a parameterized function are rejected even if that
function is never instantiated.

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
types and cross-thread resume remain outside this admission boundary.

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
`carven::runtime::execution::AsyncOperation<Result, Failures...>`. Its source result and
failure declarations describe completion, not operation construction. Native
imports undergo the same async signature checks even without a source body.
`export(cpp) async fn` exposes a source async function through its module's existing
C++ API header with the same cold AsyncOperation representation.

A native host owns its event pump and provider resources, passes an explicit
context to the exported root, and calls `drive_root(operation, &pump)`. Context
Read is a const-reference borrow. The native caller keeps its holder and hidden
resources alive until the cold owner is dropped or execution and child closure
finish. Source loans check the holder; they do not prove native fd, buffer, or
callback lifetimes. The provider maps events to Completion, releases external
reachability, and queues the existing continuation before source execution resumes.
Requesting cancellation does not establish callback quiescence: provider-reachable
state remains alive until no pending or executing callback can access it.
Native provider awaiters use `ResumeContinuation` from the shared execution entry,
`carven/runtime/execution/execution.hpp`.
They bind the suspended coroutine, query its cancellation request, and enqueue
resumption through the supplied driver after committing completion. The provider
keeps the continuation alive through dispatch and performs event delivery on the
driver thread. The source entry wrapper supplies a default timer service; an
external I/O event pump remains the native host's responsibility. A host with
pending builtin timers also provides `wait_until(i64_deadline)` in the runtime's
steady-clock nanosecond domain, coordinating its external events and the nearest
timer in one blocking wait. Native requests use `TaskContext::request_cancel()`
on the driver thread so parked timer cancellation is notified.

The time craft supplies `std::time.duration`, `std::time.clock`, and
`std::time.sleep`. Its waits produce ordinary cold operations and accept
cancellation; importing those modules adds
library APIs under the existing async rules.

Successful `char` values received by await are checked as Unicode scalar values,
including discarded awaits and direct completion returns. Failure and cancelled
completion transfer before that value check. Destroying an unstarted cold owner
does not receive a result.

Composition checks each selected successful `char` input before retaining it in
a product or committing a result. This also applies to discarded products and
joins whose later inputs fail. Native C++ types keep their declared native contract.
