# Command-line interface

The `carven` command runs an explicit batch of `.cv` source files, generates
C++ artifacts, and inspects frontend representations. This
document defines invocation, input paths, output writes, and process behavior.

## Invocation

```text
carven <source-file>... [-- <arguments>...]
carven compile [options...] <source-file>...
carven interpret [--trace] [--max-steps N] <source-file>... [-- <arguments>...]
carven dump tokens <source-file>
carven dump ast <source-file>
```

`carven --help`, `carven compile --help`, and `carven interpret --help`
print help (`-h` is also accepted). `carven --version` and `carven -V` print
`carven v<version>` followed by a newline. With no arguments, `carven` prints
the top-level help and succeeds.

## Native execution

A bare source invocation compiles the batch to C++, compiles and links a native
program, then executes it. Native header lookup includes the working directory. The batch must have a program entry point. Carven
reports a missing entry before invoking the native compiler. Multiple entries
are rejected during semantic analysis.

Arguments before `--` belong to Carven; arguments after it are passed unchanged
to the program, including `--help` and strings containing shell syntax. Native
execution accepts source paths only before the separator. Artifact destinations
and test-emission options belong to `compile`.

Native execution currently supports POSIX hosts. `CXX` selects one compiler
executable name or path, defaulting to `clang++`; its value is not split into
shell words. Carven requests C++20 and locates Crafts beside an installed binary
or in the source checkout containing the development binary. Generated files
and the executable live in a unique temporary directory removed when the driver
returns after execution or a handled failure. Child processes inherit the working directory and standard streams.
Carven returns the native compiler's failure status or the program's exit
status; termination by signal yields `128 + signal`.

## Interpretation

Interpretation is an experimental subset intended for demonstrations and teaching.
Its supported operations retain ordinary Carven semantics. Native compilation
remains the path for the full language and C++ integration; interpreter support
expands when concrete teaching or execution needs justify it. This subset does not
limit language-required compile-time evaluation, which has its own admission rules.

`interpret` parses and analyzes the same explicit source batch as native execution.
Required constant initializers, compile-time printing, and `const test` execute
through the normal analysis pipeline. The interpreter then checks the entry and
its transitive direct callees against its execution subset and executes the
published semantic operations. Admission covers all branches of those bodies.
Unused functions still receive ordinary language checks; they do not have to
belong to the interpreter subset. No C++ artifacts or native executable are written.

The current subset supports integer, bool, char, str, and owning String locals;
supported structs and fixed arrays; direct Carven calls; local mutation; conditional
control, loops and supported matching; builtin printing and formatting. It shares
the shared structured executor's operation support, but ordinary calls do not require
`const fn`. Runtime integer operations use the language's wrapping rules; required
constant arithmetic remains checked. A const function called at runtime also uses
runtime arithmetic and output behavior.

Native headers and source fragments, native calls, floating operations, callable
values, typed failures, Write parameters,
slices in executed bodies, and entry argument values are not yet supported.
Unsupported uses report `CV-INTERPRET-ADMISSION`; interpretation does not fall back
to native compilation. The entry must currently take no parameters. Arguments
after `--` are ignored by such an entry, as in native execution. This mode adds no
language syntax, global-variable behavior, or interpreter-only builtin names.

`--trace` reports executed statement locations and function calls and successful
returns to stderr, indented by call depth. It describes interpreted execution after
analysis, not constant evaluation, and does not record every expression or variable
value. Program stderr shares that stream. Combined stdout/stderr display order is
not a complete execution log.

`--max-steps N` supplies a nonnegative decimal step budget, defaulting to 100,000.
Steps charge expression evaluation, statements, and loop progress using the shared
executor; nested calls share the root budget. It does not limit elapsed time or
blocking output. Existing per-value, call-depth, aggregate, and cumulative text-work
limits also apply. Each required constant root keeps its own analysis budget;
this option changes only interpreted execution. Repeated options are errors.

Execution errors report `CV-INTERPRET-EXECUTION` with source locations and call
context; exhausted budgets report `CV-INTERPRET-LIMIT`. Completed output remains
observable. Invocation, admission, and execution failures return status 1; normal
completion returns 0, following the existing entry-result convention.

## Source inputs

A source invocation requires one or more explicitly named source files. The
compiler analyzes that complete batch; imports resolve among those inputs.

Input paths use UTF-8, `/` separators, and a `.cv` extension. Relative paths
determine module identities after lexical normalization:

```text
src/main.cv       -> src.main
crafts/json/io.cv -> crafts.json.io
```

Absolute input paths are accepted for files in a `crafts/` directory. The path
starting at the first `crafts/` component determines the canonical module path:
`/opt/carven/crafts/carven/std/utf/text.cv` becomes `crafts.carven.std.utf.text`.
The import `std::utf.text` selects that official module. Relative input paths retain
their hierarchy, so `crafts/carven/std/utf/text.cv` names the same module.

Relative paths must stay within the working directory. Absolute paths must
follow the Crafts convention above. Paths are normalized lexically;
symbolic links are resolved by the host filesystem when opening files.

Every derived component must match `[A-Za-z_][A-Za-z0-9_]*`; language keywords
are permitted as module components. Two inputs cannot derive the same canonical
module path.

## Artifact destinations

For `compile`, one destination mode is always active, and at most one may be selected
explicitly. With no destination option, output is written below the current
directory.

| Option | Destination |
| --- | --- |
| `-o <dir>` | Output below `<dir>` |
| `--output-dir <dir>` | Output below `<dir>` |
| `--stdout` | Human-oriented inspection output on standard output |

The long filesystem option also accepts `--output-dir=<dir>`. Repeating or
mixing destination options is an error. The default mode does not create an
implicit output directory.

After successful compilation and generation, Carven writes all generated
C++ headers and sources below the destination in logical-path order:
it creates parent directories, truncates existing files,
and writes their new contents. The first I/O failure stops the write; files earlier
in the order may already have changed. Directory isolation, stale cleanup, failure protection, and
content-stable incremental promotion belong to the caller or build system.

`--stdout` selects no filesystem sink. It prints every artifact in canonical
order with a heading of this form:

```text
==> logical/path <==
```

The headings make this an inspection format. Compile the files written by a
filesystem destination when building generated C++.

## Test emission

Test emission is omitted by default. One explicit mode may be selected:

| Option | Generated test artifacts |
| --- | --- |
| `--tests=default` | Module test functions, the generated runner header, and the default test entry |
| `--tests=external` | Module test functions and the generated runner header, without a generated entry |

The two options are mutually exclusive and cannot be repeated. Test emission
does not suppress a source `main`; the downstream build chooses which generated
translation units form an application or test executable. External mode
supplies the generated runner function through
`carven/generated/carven-test-runner.hpp`; its consumer owns the process entry
point and may pass a custom reporter.

## Linkage domain

`--linkage-domain=<value>` supplies opaque caller identity for the deterministic
private generated namespace. The equals sign is required. The value must be
nonempty, and the option may be specified at most once.

When the option is omitted, the driver derives a path domain from the
lexically-normalized absolute artifact root. The artifact root is the output
directory, the current working directory for the default output `.`, and the
current working directory as a virtual root for `--stdout`. Explicit and path
domains have distinct identities even when their text is equal.

For consistent generated identity across edits, a logical generation target
reuses its domain. Different targets whose generated objects may enter the same
linked image must use
different domains. Moving the output root changes the CLI default; callers that
need identity across output layouts or checkouts must provide an explicit
value.

Source text, module membership, source order, and source locations are not
linkage-domain inputs. The linkage domain does not alter Carven nominal
identity or define a public C++ ABI.

## Diagnostics and status

Invalid invocation, input loading failure, source errors, and artifact-sink
failure produce diagnostics on standard error and a nonzero status. Source
warnings are printed on standard error while a successful compilation and
materialization still return zero.

For `compile`, native compilation, linking, and C++ language selection belong
to the consuming build.

## Syntax inspection

The developer commands inspect one source file without performing semantic
analysis or producing artifacts:

```shell
carven dump tokens path/to/file.cv
carven dump ast path/to/file.cv
```

`dump tokens` prints the token stream after lexing. Lexical diagnostics are
reported on standard error and make the command fail. `dump ast` lexes and
parses the source, then prints the syntax tree when both stages succeed. Dump
formatting is intended for inspection and may change between compiler versions.

## Compile-time program output

Required constant execution and `const test` may use the builtin print operations.
The driver sends `print`/`println` to stdout and `eprint`/`eprintln` to stderr.
With `--stdout`, all compile-time program output goes to stderr so stdout contains
only generated artifacts. Output is emitted as execution proceeds, including on
compilations that later fail. Parsing and dump commands do not execute tests.
An incremental build that reuses generated artifacts does not rerun Carven or
replay compile-time output.
