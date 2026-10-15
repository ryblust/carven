# Time and waiting

[Language](README.md) · [Standard capabilities](standard.md)

Carven provides signed intervals, monotonic time points, and cancellable waits
through `std::time`. Import the types and callables being used from that module.
`Duration`, `RangeError`, `Timeout`, and `TimePoint` are declared in the standard
source module.

Importing an operation binds that operation; naming a type in an annotation or a
failure declaration requires its own binding. Duration literals require no type
binding.

## Intervals

`Duration` is a nominal struct with one public
`nanoseconds: i64` field. Every field value is valid, including zero and the full
negative range. Construction, copying, mutation, and field access use ordinary
struct rules. `RangeError` is an empty nominal struct used for unrepresentable
unit scaling and time-point addition.

| Function from `std::time` | Result | Failure |
| --- | --- | --- |
| `nanoseconds(value: i64)` | `Duration` with the supplied field value | None |
| `microseconds(value: i64)` | `Duration` scaled by 1,000 | `RangeError` |
| `milliseconds(value: i64)` | `Duration` scaled by 1,000,000 | `RangeError` |
| `seconds(value: i64)` | `Duration` scaled by 1,000,000,000 | `RangeError` |

Scaling checks the result's signed `i64` range before multiplication. These
functions are `const fn`.
Duration literals, such as `100ms`,
construct the same nominal type and check scaling at compilation. Dynamic values
use checked functions such as `milliseconds(value)?`. Nanoseconds describe the
representation, not wakeup accuracy.

```carven
import std::time using { Duration, milliseconds };

const delay: Duration = milliseconds(250)?;
```

## Monotonic time points

`TimePoint` is a standard-source class with a private monotonic-clock
point. It does not expose the clock epoch or represent calendar time. Its values
use ordinary copying, access, and lifetime rules.

| Operation | Result and behavior | Failure |
| --- | --- | --- |
| `TimePoint::now()` | `TimePoint`; sample the clock once | None |
| `TimePoint::after(duration: Duration)` | `TimePoint`; sample once and add the interval | `RangeError` |
| `point.shifted(duration: Duration)` | `TimePoint`; add the interval without reading the clock | `RangeError` |
| `point.is_before(other: TimePoint)` | `bool`; compare stored points | None |
| `point.has_passed()` | `bool`; compare the stored point with the current clock | None |
| `point.wait()` | Snapshot the stored deadline into a cold `Operation<void>` | None |

Checked addition rejects results outside signed `i64` nanoseconds. These
operations support `const` execution. Clock sampling observes the monotonic clock
of the selected execution host. Comparisons and waits require points from the
same clock domain. A frozen point sampled during compilation retains the compiler
host's reading; callers cannot assume it is meaningful on another host or after
the clock epoch changes, such as across a reboot.

## Execution capabilities

Time operations support static execution, interpretation, and native execution,
including clock sampling, waiting, and `timeout`. Static execution uses the
compiler process's clock and timers; interpreted and native calls use their
executing process. Ordinary calls remain at runtime even when their arguments
are known.

Clock reads and completed waits belong to the selected host stage. Cold operation
construction alone does not register a timer. Operations cannot be frozen as
module constants; static roots can construct and await them locally. Execution
budgets do not impose an elapsed-time limit.

## Waiting

Import `sleep` and `sleep_until` from `std::time`. Both return ordinary cold
void operations. Starting or awaiting them consumes the cold owner.

| Function | Argument | Nominal failure |
| --- | --- | --- |
| `sleep(duration: Duration)` | Relative interval | `RangeError` |
| `sleep_until(point: TimePoint)` | Absolute monotonic deadline | None |

Constructing `sleep(duration)` does not read the clock or register a timer.
Relative timing begins when the operation executes, after its initial
cancellation checkpoint. A nonpositive duration succeeds immediately after that
checkpoint without deadline arithmetic or an implicit yield. Positive intervals
use checked deadline addition and may throw `RangeError`.

`point.wait()` is a synchronous factory for a cold operation. It copies the
stored deadline without retaining the point, reading the clock, or registering
a timer. `sleep_until(point)` supplies the same absolute waiting capability.
Both accept pending cancellation before checking whether the deadline is already
due. Creating or discarding an unstarted operation performs no waiting.

```carven
import std::time using { TimePoint, RangeError, sleep, sleep_until };

async fn main() throw RangeError {
    await sleep(250ms)?;
    let deadline = TimePoint::after(100ms)?;
    await sleep_until(deadline);
}
```

Normal completion is no earlier than the selected deadline. Dispatch may delay
delivery; waiting does not promise punctual execution. Cancellation is a separate
completion outside the nominal failure set. Timer registrations are removed
before an operation completes, and explicit child `close` waits for cancellation
cleanup before releasing retained backing. Each stage uses its host's monotonic clock
and preserves this completion contract.

## Deadlines

`timeout(operation, duration)` is a builtin from `std::time`. It consumes one
cold operation and a `Duration`, preserves the operation's successful type, and
adds `Timeout` and `RangeError` to its nominal failures. `Timeout` is an empty
nominal struct. The call requires a direct function invocation; it cannot be
stored as a callable value. Transfer a named cold owner with `&&`; lexical
children cannot be passed.

Construction retains the protected input without executing it or reading the
clock. At execution, pending cancellation is accepted first. A nonpositive
interval selects `Timeout` without starting protected work or reading the clock.
A positive interval calculates a fixed monotonic deadline before starting either
child. Unrepresentable deadline arithmetic produces `RangeError`.

The protected operation is queued before the timer. Terminal publication selects
one candidate; an already terminal pair gives the protected operation priority.
Pending parent cancellation is checked before selection and after observing the
selected completion, before committing the outward outcome. A protected winner
forwards its completion, including supported immovable native results. A timer
winner produces `Timeout`. Both children close before delivery. Once committed,
later cancellation requests and losing outcomes cannot replace the result.
Noncooperative protected work can delay closure beyond the deadline.

```carven
import std::time using { timeout, RangeError, Timeout };

async fn load() -> i32 => 7;

async fn main() throw Timeout + RangeError {
    println(await timeout(load(), 100ms)?);
}
```

## Native hosts

Generated entries run ready work on the same thread and wait for the nearest
timer when idle. There is no background timer thread.

The native boundary uses signed 64-bit nanoseconds from the steady-clock epoch.
Native clock ticks contain a positive whole number of nanoseconds, and clock
readings must fit the signed nanosecond domain. Conversion uses checked integer
arithmetic and rounds a waiting deadline to the later native tick.

The [native async host contract](async.md#native-providers-and-hosts) defines
`EventSource`, polling, waiting, and retained resources. Native cancellation uses
the current task's `request_cancel()` on the driver thread. It records intent and
notifies parked timers; writing the request flag directly does not perform that
notification. Closing retains registrations and borrowed resources until the
relevant completions become quiescent.
