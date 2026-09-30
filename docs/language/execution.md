# Printing, entry points, and tests

[Language](README.md)

This page defines observable output, program entry, tests, assertions, and
selected diagnostics. Compiler invocation and process reporting are covered by
the [CLI](../toolchain/cli.md).

- [Printing](#printing)
- [Entry points and tests](#entry-points-and-tests)
- [Diagnostics](#diagnostics)

## Printing

`print`, `println`, `eprint`, and `eprintln` are builtin
callables; they need no import and follow ordinary name lookup and shadowing.
They accept one or more Read values and return `void`. `void`, entry arguments,
and character-iteration views are not printable. Arguments are evaluated once,
from left to right. Values are separated by one space; `println` and `eprintln`
append one newline.
`println()` and `eprintln()` also accept no argument to write a newline. Output
goes to stdout or stderr respectively. No typed failure or `?` is required.
As with interpolation, scalar Read values are saved and String Read values alias
their owners. Text views retain their backing throughout argument evaluation
and printing.

Direct printing is admitted in functions executed in the static stage, `const` blocks and `const test` for
the types supported by static execution. Static execution delivers
output synchronously to the compiler host; runtime calls use the runtime streams. Declaring a function
does not itself execute it. Optional precomputation does not produce
static-stage output or remove required runtime printing. Printing follows the
same argument, separator, newline, and text rules in both stages. Output bytes
consume the root's cumulative text-work budget. Completed output remains observable
if later execution fails.

Text is printed verbatim, including embedded NUL bytes. Scalars use their default
C++ format representation; a Carven `char` prints its UTF-8 text. Formatting is
explicit through existing interpolation, for example `println(f"{value:04}")`.
Text arguments are not interpreted as format strings: `println("{value}", 3)`
prints `{value} 3` followed by a newline.

Direct printing displays logical data structure independently of interpolation's
formatting protocol. Structures show their source type name and fields in
declaration order. Enums show `Type::Case` and parenthesized payload values.
Arrays and slices show bracketed elements; integer ranges show their bounds
and `..` or `..=`.

Nonempty structures, sequences, and enum payloads use multiline layout. Each
field or element starts on a new line, indented four spaces per level, and ends
with a comma. Closing delimiters occupy their own line at the enclosing level.
Empty structures and sequences remain `Type {}` and `[]`; cases without payloads,
scalars, and ranges remain on one line without a trailing comma. Layout is
independent of line width and content length.

```text
Order {
    price: Money {
        cents: 1250,
    },
    names: [
        "a",
        "b",
    ],
}
```

Nested text is double-quoted, escaping quotes, backslashes, newline, carriage
return, tab, and NUL. Nested characters use single quotes and escape a single
quote. Top-level text retains the verbatim behavior above.

Native results use C++ type classification. Arithmetic values display their
scalar values; `char32_t` uses character display, while `char8_t`, `char16_t`, and
`wchar_t` display numeric code units. `std::string_view` and the runtime `String`
type use quoted text. `char*` and `const char*` use the text rules above;
null values display `nullptr`, and non-null values require readable
NUL-terminated storage. Other pointers convertible to `const void*` display an
address or `nullptr`. Other external types, function pointers, and callable values
display `<opaque>`.
Class values display their type name, including when nested in another value.

Structural display never invokes a custom formatter, stream insertion operator,
or getter, including for nested fields. `println(value)` selects structural
display; `println(f"{value}")` first performs explicit formatting. Read access and
backing requirements apply to the complete printed value; display adds no owning
copy or transfer.

Structural output expands at most eight levels (the root has depth zero), shows
at most 64 elements per array or slice, and retains at most 16,384 UTF-8 bytes
before a truncation marker, including layout whitespace. Omitted content is
marked `...`; a sequence omission occupies an element line ending in a comma.
These bounds also apply to structural values in assertion explanations, but not to verbatim
top-level text. Display is diagnostic text, not a serialization format.

Buffering and flushing follow the selected C++ standard-library facilities,
with no extra flush per call. Native formatting or output failure terminates
through the runtime's non-throwing contract.

Printing values can use an expected signature, for example
`let output: fn(str, i32) -> void = println;`, and pass through ordinary callable
views. The expected signature fixes the parameter count and types.

## Entry points and tests

A compilation may contain zero or one program entry. A function named `main`
and a file containing top-level executable statements each define an entry.
Multiple entries are diagnosed at their source locations. Generating C++ does
not require an entry; executing a program requires one.

Top-level statements execute in source order as one implicit entry body. Module
declarations may appear between them and retain their usual meaning: in
particular, a top-level `const` is a module constant. Top-level `let` and `var`
bindings are entry locals; module functions cannot capture them. The implicit
entry introduces no callable source name and has no parameters or declared
outward failures; it infers outward failures from its body. Its statements
follow ordinary function-body rules, including
result inference, access, cleanup, and handling failures. These language rules
are shared by native compilation and interpretation.

An explicit `main` function's module path, module domain, and declaration
visibility do not affect entry selection. It
accepts no parameters or one untyped Read parameter representing command-line
arguments; ordinary function parameter rules apply elsewhere. An entry with
outward failures must declare an explicit `throw` contract, including a
`private` entry. Its body must stay within that declared failure set.

Normal completion produces process status zero; a declared Carven result, if
present, is not a process exit status. A typed failure that escapes either entry
produces the host C++ `EXIT_FAILURE` status and one report on stderr containing
its type and structural payload. The entry call completes ordinary local cleanup
before the wrapper reports the failure. The wrapper retains the failure payload
through reporting and destroys it before process completion. Catching a failure
and completing normally produces status zero and no failure report.

```carven
struct ConfigError {}
fn load_config() throw ConfigError { throw ConfigError {}; }
fn main() throw ConfigError { load_config()?; }
```

This program completes with a failure process status and reports:

```text
main.cv:3:1: error: failure 'main.ConfigError' escaped the program entry
  failure: ConfigError {}
  note: program exited with a failure status
```

Native execution locates the report at the entry declaration, since a failure
value carries no source position. The interpreter reports the same payload under
`CV-INTERPRET-EXECUTION` and locates it at the `throw` statement with its call
path.

The command-line parameter is an opaque entry-only value. Its C++
runtime representation is not a Carven sequence contract and does not make the
parameter indexable or iterable.

A test is a module-local body with no parameters or result. An explicit test name
must be unique within its module and cannot be `main`. Tests participate in
parsing and semantic analysis regardless of whether the compiler is asked to
emit test artifacts. A test must handle every failure.

`const test "name" { ... }` explicitly selects static execution. The
string is optional for both `test` and `const test`; anonymous failures report
the test's file, line, and column. Explicit names must be unique within a module.
Its body uses the static-execution operation and type subset, including direct
`const fn` calls, calls through local bindings of named `const fn`, printing, and
test operations. Unsupported operations are diagnosed when executed. Each static test executes once
after body construction during semantic analysis, regardless of test artifact
selection. Tests follow the compilation batch's module order and source order
within each module. Each test has a fresh execution budget and local storage. Failed-check diagnostic
text consumes the same cumulative text-work budget as output and text construction.
Passing static tests generate no test functions or runtime runner entries.
Ordinary `test` bodies retain runtime execution.

Static `check` failures are compilation errors and execution continues. Failed
`require` and `fail` stop the current test, including nested Carven calls; the
next static test still executes. Execution errors and resource exhaustion also
stop the current test. Test operations in a `const` initializer have
no active test and are rejected when executed. Static execution validates the
executed semantic operations; generated C++ and native behavior require runtime
tests.

`assert`, `check`, `require`, and `fail` are builtin callables using ordinary
name lookup.
Local bindings, module declarations, and explicit imports shadow builtin names
in every expression position. A C++ namespace wildcard is a fallback for otherwise
unresolved names; it does not hide known builtins. An explicit native selection can
shadow a builtin. These operations are available in helpers and lambdas:

```text
assert(condition);
assert(condition, message);
check(condition);
check(condition, message);
require(condition);
require(condition, message);
fail();
fail(message);
```

The `condition` must have the exact Carven type `bool`; the optional `message`
must have Carven type `str` or `String`. `assert` diagnoses invalid counts,
conditions, and messages with `CV-TYPE-CALL-ARITY`, `CV-TYPE-CONDITION-BOOL`, and
`CV-TYPE-MISMATCH`. Test operations use `CV-TEST-ARGUMENT-COUNT`,
`CV-TEST-CONDITION-TYPE`, and `CV-TEST-MESSAGE-TYPE`.

A builtin used as a value requires an expected concrete `fn(...) -> void`
signature, for example `let stop: fn() -> void = fail;`. It then follows ordinary
callable-view borrowing and failure-widening rules. No testing context argument
is exposed to source code. The runner supplies the current test context for
`check`, `require`, and `fail` along the synchronous Carven call chain. Using these
operations without an active test violates the runtime contract. Native execution
reports the operation's source position and aborts; interpreted execution
diagnoses the same position.

Direct `assert`, `check`, and `require` calls evaluate their condition exactly
once. Only a false condition evaluates the optional message, once and after
condition observation. `fail` always evaluates its optional message. Skipped
messages remain subject to semantic and type checking. Binding a builtin to a
callable value retains ordinary eager argument evaluation at indirect call sites;
the callee receives already evaluated values.

`assert` requires no test context and is always enabled, independently of native
build configuration and `NDEBUG`. Native failure reports to stderr and aborts the
process, without ordinary stack cleanup. Interpreted failure stops the whole
execution, including any remaining tests. A failure in the static stage emits `CV-ASSERT`
and stops the current evaluation. Assertions are not typed failures and cannot
be recovered by `try`. When a native assertion fails in a test, the report
includes its module and either its explicit test name or its source location.

A failed `check` reports and falls through. A failed `require`
reports and exits the whole current test, while a successful `require` falls
through. `fail` reports and exits the whole current test. Test exit is distinct
from return, loop transfer, and failure transfer, and is not caught by `try`.
It propagates through Carven helpers and callable views. Normal local cleanup runs before the
runner continues to the next test. External C++ calls do not participate in this
transport; a test stop cannot unwind through arbitrary native callbacks.

Each failed operation reports the original `.cv` display origin, the 1-based
line and column of its operation name, its operation kind, and an optional runtime
message. For a builtin bound as a callable value, the source origin is its
binding expression. Direct `assert`, `check`, and `require` calls additionally
report the complete condition source: the original UTF-8 byte slice of the
condition expression span,
including parentheses, whitespace, line breaks, and comments. `fail` reports
without a condition. The runtime reporter controls the presentation of these
records. Default native and interpreted test reports identify failed cases by
module and either an explicit test name or the anonymous test's file, line, and
column, then finish with passed and failed case counts on stderr.
Multiple failed checks in one test count as one failed case. Successful cases
have no individual report; source printing retains its original stream. Native
custom reporters retain control of their output and receive no default summary.
Reports are emitted when the operation fails. Each failure starts with
`file:line:column: error: description`. Indented fields contain the active
`test` context (module and explicit name or source location), `condition`,
`operands`, and `message` when
present. Each failure carries its own test context. Multi-line fields use an
indented block; an explicitly empty message appears as `message: ""`.
Static-stage diagnostics retain their error codes and source excerpts and use
the same condition, operand, and message layout.
A final note identifies a stopped test or aborted execution. An aborted run has
no completion summary.

A runtime trap uses the same layout. Division or remainder by zero, an
out-of-width shift, an out-of-bounds index, and an invalid scalar conversion
report `file:line:column: error: description`, the active `test` context, and
`note: execution aborted`. An index trap adds `index` and `length` fields. A
direct native support call uses the C++ position supplied by its runtime API.
Generated checks and interpreted checks use the Carven operation's position.
A trap ends the interpreted run, including remaining tests, without a completion
summary.

An outer comparison in a direct `assert`, `check`, or `require` condition reports
both operand spellings and structural values on failure. An outer `&&` or `||`
reports the two Boolean subexpressions;
the skipped operand is marked `<not evaluated>`. An operand whose displayed value
repeats its source spelling, such as a literal, is omitted; a condition with no
remaining operand has no `operands` field. Parentheses preserve this
behavior. Nested operations are evaluated normally; explanations do not recursively
trace their internals or search for a first differing field. Indirect builtin
calls and other condition forms retain their condition/message reporting.

Explanation collection uses the original evaluation and comparison, preserving
sequencing, snapshots, short circuiting, propagation, and cleanup. It does not
reevaluate operands or invoke formatters. Failed values are rendered before the
optional message expression runs, so mutations from that message cannot rewrite
the explanation. Successful conditions do not render operand values. Runtime
reporters receive a borrowed `explanation` string valid for the synchronous
report callback. Constant-test diagnostics include the same explanation.

## Diagnostics

The following table lists selected semantic diagnostics in the current compiler:

| Code | Severity | Condition |
| --- | --- | --- |
| `CV-CONST-CYCLE` | Error | Constants or static stages form a dependency cycle |
| `CV-CONST-EXPORTED-TYPE` | Error | An exported module constant omits its explicit type |
| `CV-CONST-ADMISSION` | Error | A required expression or a `const fn` capability proof encounters an unsupported operation |
| `CV-CONST-EVALUATION` | Error | Static execution cannot produce a supported result |
| `CV-CONST-LIMIT` | Error | Static execution exceeds a resource budget |
| `CV-CONST-TEST` | Error | A static test reports failure, or a test operation executes without an active static test |
| `CV-CPP-IDENTIFIER` | Error | A C++ API path or global provider name cannot be represented by generated C++ |
| `CV-CPP-API-PATH-COLLISION` | Error | A function and namespace require the same prefix in the public C++ API tree |
| `CV-EFFECT-CATCH-ALTERNATIVE-UNREACHABLE` | Warning | A catch alternative cannot match a remaining protected failure |
| `CV-EFFECT-CATCH-ARM-UNREACHABLE` | Warning | A catch arm cannot match a remaining protected failure |
| `CV-EFFECT-CATCH-NON-EXHAUSTIVE` | Error | A catch leaves a protected failure unhandled |
| `CV-EFFECT-THROW-PUBLISHED` | Error | An explicit entry or published callable has failures without a `throw` contract |
| `CV-FLOW-MISSING-RETURN` | Error | A reachable path of a value-returning callable omits its result |
| `CV-FLOW-TRANSFER-BOUNDARY` | Error | `return`, `break`, or `continue` crosses a value-control branch or a `const` block |
| `CV-FLOW-UNREACHABLE-MATCH-ARM` | Warning | A match arm pattern is fully covered by preceding unguarded arms; the primary location is that pattern span |
| `CV-LAMBDA-CAPTURE-UNUSED` | Warning | An explicit lambda capture is unused |
| `CV-LINT-UNUSED-IMPORT` | Warning | An import selects no uniquely referenced binding |
| `CV-LINT-UNUSED-LOCAL` | Warning | A named local binding is unused |
| `CV-LINT-UNUSED-PARAMETER` | Warning | A named parameter is unused |
| `CV-LINT-RETURN-COPY` | Warning | A returned owner containing `String` is copied where Take would be admitted |
| `CV-MATCH-DUPLICATE-ALTERNATIVE` | Error | An or-pattern contains a repeated or subsumed alternative |
| `CV-TEST-ARGUMENT-COUNT` | Error | A builtin test operation has the wrong argument count |
| `CV-TEST-CONDITION-TYPE` | Error | A `check` or `require` condition is not exactly `bool` |
| `CV-TEST-MESSAGE-TYPE` | Error | A test message is neither `str` nor `String` |
| `CV-TYPE-VISIBILITY-LEAK` | Error | A declaration surface exposes a narrower nominal identity |

The table is a partial lookup for current diagnostics. Human-readable messages,
notes, formatting, colors, and incidental ordering are presentation.
Warnings do not make an otherwise valid program fail.

For unused diagnostics, a reachable reference counts as a use and `_` is never
an unused candidate. A list import is used when any selected binding is
uniquely referenced.
