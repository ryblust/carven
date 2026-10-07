# Time standard library

The time craft provides signed intervals, monotonic time points, and cancellable
waiting. Import the module responsible for the capability being used.

| Module | Responsibility |
| --- | --- |
| `std::time.duration` | `Duration`, `RangeError`, and checked unit construction |
| `std::time.clock` | `TimePoint` with clock sampling, checked shifts, and comparison |
| `std::time.sleep` | Cold `sleep` and `sleep_until` operations |
| `std::time.timeout` | Nominal `Timeout` failure for `timeout` |

```carven
import std::time.duration using RangeError;
import std::time.sleep using sleep;

async fn main() throw RangeError {
    let delay = 250ms;
    await sleep(delay)?;
}
```

## Intervals and time points

`Duration` is an ordinary struct with a signed `i64` `nanoseconds` field. Every
field value is valid, including zero and the full negative range. `nanoseconds`
constructs it directly. `microseconds`, `milliseconds`, and `seconds` check their
scaling before multiplication and throw `RangeError` on overflow. These functions
support static execution. Integer duration literals such as `100ms` and `1s`
construct the same ordinary `Duration` type and check scaling at compilation.
Literals require no import; explicit `Duration` annotations and constructors
use the ordinary type import. Dynamic values use checked constructors such as
`milliseconds(value)?`. Nanoseconds describe the representation, not wakeup
accuracy.

`TimePoint` is an ordinary class whose private field holds a point on the
monotonic clock. It does not expose the clock epoch or represent calendar time.
`TimePoint::now()` reads this clock. `TimePoint::after(duration)` reads it once
and performs checked interval addition.

`point.shifted(duration)` adds an interval to the stored point without reading
the clock. `point.is_before(other)` compares points. Both are `const fn`;
obtaining a time point from the clock requires runtime execution.
`point.has_passed()` compares a point with the current clock.
`RangeError` reports unrepresentable additions.

```carven
import std::time.clock using TimePoint;
import std::time.duration using RangeError;
import std::time.sleep using sleep_until;

async fn main() throw RangeError {
    let deadline = TimePoint::after(100ms)?;
    await sleep_until(deadline);
}
```

## Waiting

Both waits produce ordinary cold void operations. Constructing `sleep(duration)`
does not read the clock or register a timer. Relative timing begins when the
operation executes, after its initial cancellation checkpoint. A nonpositive
duration succeeds immediately after that checkpoint without deadline arithmetic
or an implicit yield. Positive intervals use checked deadline addition and may
throw `RangeError`.

`point.wait()` is a synchronous factory for a cold operation. It copies the
stored deadline without retaining the point, reading the clock, or registering
a timer. `sleep_until(point)` provides the same absolute waiting capability from
the sleep module. Both accept pending cancellation before checking whether the
deadline is already due, and preserve the private representation.

Normal completion is no earlier than the selected deadline. Dispatch may delay
delivery; waiting does not promise punctual execution. Cancellation is a separate
completion, outside the nominal failure set. Timer registrations are removed
before continuations are queued, and explicit child `close` waits for that
cleanup before releasing retained backing.

The shared execution runtime owns clock access, waiting operations, timer
registration, deadline polling, and cancellation delivery. Its public C++ entry is
`carven/runtime/execution/execution.hpp`. The time library owns durations, time points,
checked arithmetic, and their source APIs; its native adapter calls
`carven::runtime::execution::now` and `sleep_until`. The native source entry supplies
the execution driver automatically. It runs ready work on the same thread and
blocks to the nearest timer only when idle.
There is no background timer thread. Real clocks and timer providers execute in
generated C++; the semantic interpreter cannot execute their native bodies.

## Deadlines

`std::time.timeout` defines `Timeout`, the nominal deadline failure used by the
builtin `timeout(operation, duration)`. Import `Timeout` when naming it
in a failure declaration or catch. The builtin and duration literals require no
imports. Ordinary declarations and local bindings take precedence over builtin
names, as they do for `print`.

```carven
import std::time.duration using RangeError;
import std::time.timeout using Timeout;

async fn load() -> i32 => 7;

async fn main() throw Timeout + RangeError {
    println(await timeout(load(), 100ms)?);
}
```

The returned cold operation preserves the protected operation's successful type
and adds `Timeout` and `RangeError` to its failures. Execution accepts pending
cancellation before selecting a deadline. A nonpositive interval produces
`Timeout` without starting protected work; a positive interval calculates one
fixed deadline before starting either child. The protected operation is the
first candidate. Its selected completion is preserved; a timer winner produces
`Timeout`. Parent cancellation is checked after observing the candidate and
before outward commitment. Both children close before delivery, so an
unresponsive protected operation can delay return beyond the deadline.

## Native hosts

The native boundary uses signed 64-bit nanoseconds from the steady-clock epoch.
The native clock uses a signed 64-bit representation whose ticks contain a
positive whole number of nanoseconds. Clock readings must fit the signed
nanosecond domain. Conversion uses checked integer arithmetic and rounds a
waiting deadline to the later native tick.

An explicit host passed to `drive_root(operation, &pump)` continues to provide
`poll(driver)` and `wait()`. When timers are pending, it also provides
`wait_until(i64_deadline) -> bool`. This deadline uses the same steady-clock epoch
and nanosecond representation as the runtime. The host waits for external events
or that deadline in one operation; it returns true after an event, timeout, or
other wake that permits the driver to poll again. A wait-only host cannot drive
pending timers.

Native cancellation uses the current task's `request_cancel()` on the driver
thread. It records intent and marks timer cancellation work for the next poll;
writing the request flag directly does not notify parked timers. Polling handles
host cancellation before blocking. Closing retains registrations and borrowed
resources until all relevant completions have become quiescent.
