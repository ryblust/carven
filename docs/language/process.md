# Processes

`std::process` provides host-process queries and child-process execution. The
provider is shared by generated programs and semantic execution. Piped and
inherited-stdio processes support Windows, macOS, and Linux.

## Host process

`arguments() -> Vector<String> throw ProcessError` returns the current host
process's arguments without the executable name. Empty arguments remain empty.
`executable() -> String throw ProcessError` returns its absolute executable path.
Both queries read the compiler process during static or interpreted execution;
they do not substitute an interpreted entry function's argument list.

`find_executable(name: str) -> String throw ProcessError` resolves an explicit path
or searches `PATH` for a bare name. The result is absolute and preserves the
selected symbolic-link name, so compiler drivers such as `clang++` retain their
behavior. Missing executables and invalid host text report a system error.

`operating_system() -> OperatingSystem` identifies the provider's operating
system as `Windows`, `MacOS`, `Linux`, or `Other`. These queries use the same
provider in native, static, and interpreted execution. Argument and executable
queries support Windows, macOS, and Linux; other platforms report `ENOTSUP`.

## Synchronous commands

`environment(name: str) -> String throw ProcessError` reads a UTF-8 environment
value. Missing and empty values both return an empty string.

`run(program: str, arguments: [String]) -> i32 throw ProcessError` starts a child
with inherited stdin, stdout, and stderr, then waits for its exit status.
`run_to(program, arguments, output: str)` redirects stdout to a file, replacing
its contents. An empty output path inherits stdout. Arguments are passed
separately, without a shell. Process startup and wait failures throw
`ProcessError::System(code)`; a nonzero program exit remains an ordinary result.

`run_with(program, arguments, options: RunOptions)` additionally selects the
child's working directory. `RunOptions` has `directory: str` and `output: str`
fields; empty values inherit the current directory and stdout. The output file
is relative to the caller's directory. The caller's working directory remains
unchanged. All three entries support static and interpreted execution.

```carven
import std::process using run_with;

let status = run_with("git", ["status", "--short"], {
    directory: "checkout",
    output: "status.txt"
})?;
```

## Piped processes

Import `Process` and `ProcessError` from `std::process`.
`Process::spawn(program: str, arguments: [String])` starts a child with separate
arguments, searches PATH for a bare program name, and inherits the current
working directory, environment, and stderr. Embedded NULs are rejected.
The returned handle owns the child and its stdin/stdout communication endpoint.
Copies share that child; they do not spawn another process.
`Process::spawn_in(program, arguments, directory)` selects the child's working
directory when creating the child. It leaves the calling process's directory unchanged;
an empty directory inherits the current directory. Arguments and executable lookup
otherwise follow `spawn`.

| Operation | Completion |
| --- | --- |
| `Process::spawn(program, arguments)` | A process handle, or `ProcessError::System(code)` |
| `Process::spawn_in(program, arguments, directory)` | A process handle started in the supplied directory |
| `Process::spawn_inherited(program, arguments)` | A child inheriting stdin, stdout, and stderr |
| `child.read(limit)` | Cold operation yielding `ReadResult<Vector<u8>>::Data(bytes)` or `End`; require a positive limit |
| `child.write_all(&&bytes)` | Cold operation consuming bytes and completing after every byte is written |
| `child.close_input()` | Send EOF after writes finish |
| `child.wait()` | Cold operation returning the exit code, or `128 + signal` for signal termination |
| `child.terminate()` | Request immediate termination of the child |

Read/write/wait operations report system failures through `ProcessError::System`.
One read, one write, and one wait can be pending simultaneously. Overlapping
operations in the same direction report `EBUSY`. Reads return at most 64 KiB,
regardless of a larger limit. Byte chunks preserve arbitrary bytes and need
caller-owned framing. Stderr remains independent of the protocol stream.

Operations integrate with the ordinary same-thread driver, timers, cancellation,
and lexical child closure. Windows uses byte pipes and cancellable native writes;
pending pipe writes do not block the execution driver. Cancelling a read does not
consume pending input.
Cancelling a write cannot undo bytes already sent. Cancelling a wait does not
terminate the child. Exit waits observe termination within bounded polling
intervals; already observed exit status is retained for subsequent waits.

Close input to let a child reading through EOF finish. Repeated close calls succeed.
A peer that has already disconnected also satisfies input closure; other system
errors and an active write remain failures. After protocol shutdown,
await `wait()` to observe and reap it. Dropping the final native handle terminates
and reaps an unreaped child; cold and active operations retain their source holder
and native state until their lifetime ends. Descendant processes are not managed.
Operations marked `const fn` support explicit static execution. Static effects
use the compiler process's environment and require host effects to be enabled.
Native handles remain local to execution and cannot be frozen as constants.
Native execution and semantic execution use the same provider; the semantic
executor advances asynchronous operations on its execution thread.

## Process groups

`ProcessGroup::new()` creates a shared group. `group.spawn_inherited(program,
arguments)` starts a child and appends it to the group. `group.wait()` blocks until
one child exits, removes it, and returns `ProcessExit { index: usize, status: i32 }`. The
index addresses the group's current insertion order before removal. Waiting on an
empty group reports a system error. Calls use separate arguments without a shell.

The group owns its children; copies share the group. Callers can keep task
metadata in the same order as those children. Dropping the last group owner
releases its remaining handles.
`available_parallelism()` returns a positive host concurrency hint; callers choose
their own limit. Groups provide child lifecycle operations; callers own their scheduling policy.
