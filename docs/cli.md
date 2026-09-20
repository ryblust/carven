# Command-line interface

The `carven` command runs `.cv` source files with fixed Crafts roots, checks source
batches, generates C++ artifacts, and inspects frontend representations. This
document defines invocation, input paths, output writes, and process behavior.

## Invocation

```text
carven [--tests] [--timings] <source-file>... [-- <arguments>...]
carven compile [options...] <source-file>...
carven check [--timings] <source-file>...
carven interpret [--tests] [--timings] [--trace] [--max-steps N] <source-file>... [-- <arguments>...]
carven dump [tokens|ast] [--timings] <source-file>
```

`carven --help`, `carven compile --help`, `carven check --help`,
`carven interpret --help`, and `carven dump --help` print help (`-h` is also
accepted). Top-level help lists commands; each command has its own usage and
options. `carven --version` and `carven -V` print `carven v<version>` followed by
a newline. With no arguments, `carven` prints the top-level help and succeeds.

## Source collection

`check`, `compile`, native execution, and `interpret` combine explicit application
inputs with `.cv` and `.cpp` files collected recursively from the toolchain's
`crafts/carven/` and the working directory's optional `crafts/`. Other application
files must be named explicitly. The collector assigns module identities relative
to each Crafts root, deduplicates files by canonical path, and sorts the batch by
path spelling. Distinct files with conflicting module identities are errors.
Directory symlinks are not recursively followed.

The installed layout is `<prefix>/bin/carven` with matching resources in
`<prefix>/crafts/carven/`. Development binaries locate Crafts in their containing
source checkout. Additional native libraries, compiler flags, dependency
downloads, and build scheduling belong to an external build system.

### Installed Crafts

A Craft contains library sources prepared to build together. The project's
`crafts/` directory holds installed Crafts. All `.cv` and `.cpp` files under the
collected roots participate, including files in `examples/` and `tests/`
subdirectories. Import declarations resolve module references within the collected
batch. Official and third-party Crafts use the same rules.

A Craft may combine `.cv`, `.cpp`, and C++ headers. Carven analyzes every collected
`.cv`, executes required constant evaluation and static tests, and selects all
ordinary tests in test mode. Native execution compiles every collected `.cpp`
alongside generated implementations. C++ headers are included by those sources.
Documentation and other resources may accompany the package.

Installed sources must build together in the selected environment, with distinct
module identities and available native dependencies. Library sources must leave
program entry selection to the application. Keep independent examples, intentional
compilation-failure tests, and alternative build targets outside the collected
roots. Analysis errors and static-test failures in any collected module fail the
command, including modules the application does not import.

For custom integration, place external repositories outside the collected roots,
for example in `thirdparty/`. Supply selected `.cv` files explicitly to Carven.
Supply C++ sources, include paths, defines, compiler options, and libraries to the
native build. Explicit `.cv` inputs follow the ordinary module-path rules and are
combined with the automatically collected Crafts.

## Native execution

Carven analyzes all collected modules, generates C++, compiles and links a native
program, then executes it. Program execution requires one entry point.
`carven --tests` compiles and runs ordinary runtime tests, requires at least
one runtime test, and leaves the program entry unexecuted. Static tests run during
analysis. Assertion failures produce a nonzero exit status; later tests continue.
Native header lookup includes the generated directory, both Crafts include roots, and the
working directory. Before `--`, arguments are source paths, `--tests`, or
`--timings`; arguments after it are passed unchanged to the executable.
Artifact output and external test-runner options belong to `compile`.

`CXX` selects one compiler executable name or path, defaulting to `clang++`.
GCC-style drivers use `-std=c++20`; `cl` and `clang-cl` use MSVC-style arguments
and separate temporary object files. The compiler, SDK, and linker must be
available in the current environment. POSIX and Windows process adapters invoke
children directly, inheriting the working directory, environment, and standard
streams. `CXX` is not split into shell words.

Generated files and the executable reside in a unique temporary directory,
removed when the driver returns after execution or a handled failure. Carven
returns the native compiler's failure status or the program's exit status. On
POSIX, termination by signal yields `128 + signal`.

## Timing reports

`--timings` enables a human-readable report on standard error for `check`,
`compile`, `interpret`, `dump`, and direct native execution:

```shell
carven check --timings main.cv
carven compile --timings main.cv -o emit
carven interpret --timings main.cv
carven dump --timings main.cv
carven --timings main.cv -- argument
```

The report shows the outcome, total wall-clock duration, and the stages executed,
with aligned durations in milliseconds or seconds. Lexing and parsing accumulate
across sources. Semantic analysis includes constant evaluation and static tests.
Checking, compilation, and runtime commands also report source collection.
Native runs report C++ compilation and linking. Native and interpreted runs label
their runtime phase `Execution`; `compile` ends with C++ artifact output.

Total time includes pipeline setup, diagnostics and command-resource cleanup, so
it can exceed the sum of stage durations. Failed commands report the stages
attempted. Native runs report the program's exit code. Invalid command options
produce diagnostics without a timing report.

Timing reports use stderr and leave program and artifact streams intact.
`interpret --trace --timings` includes both execution traces and the final report.
For interpretation and native runs, `--` ends command-option parsing. Reports
are intended for human reading.

## Checking

`check` collects the fixed Crafts roots and analyzes the resulting source batch
through the same semantic pipeline as `compile`, including required constant
evaluation and `const test` execution. Ordinary
functions and runtime tests receive semantic checks without execution. An entry
point is optional.

The command completes after semantic analysis and produces no artifacts. Native
overload resolution, template instantiation, and native type properties are
checked by the C++ compiler.

`check` requires at least one source file. It accepts `--timings` and standalone
`--help` and `-h`. Invocation, input, and analysis errors return status 1;
success returns 0, including when warnings are reported. Successful checks print
`carven: check passed` on stderr. With `--timings`, the timing report adds the
duration to this success message instead of printing it twice.

## Interpretation

Interpretation executes a subset of Carven's semantic operations. Native C++
integration requires compiled execution. The interpreter applies runtime arithmetic
and execution rules to supported operations and rejects unsupported capabilities.

`interpret` uses the same fixed Crafts roots, resource lookup, source sorting, and
deduplication as native execution. Other application files remain explicit inputs.
Required constant initializers, `const {}` blocks, and `const test` execute
during analysis. The interpreter then checks the entry and
its transitive direct callees against its execution subset and executes the
published semantic operations. Program execution requires one entry: top-level
executable statements or `main`. Declaration-only and empty files remain valid
for `check`, but do not provide a runtime entry.
Admission covers all branches of those bodies.
Unused functions still receive ordinary language checks; they do not have to
belong to the interpreter subset. No C++ artifacts or native executable are written.

`interpret --tests` selects ordinary runtime tests instead of the program entry.
It requires at least one runtime test and does not execute top-level statements or
`main`. Static tests still execute during analysis. Runtime tests run in canonical
module order and source order within each module, each with fresh local storage
and an independent execution budget. All selected test bodies and their transitive
callees pass admission before any runtime test executes. `check` failures accumulate;
`require` and `fail` stop the current test through helper calls and cannot be caught
as typed failures. Later tests still run after assertion failures, execution errors,
or exhausted budgets. The command reports failures and a pass/fail summary to stderr
and returns 1 if any test fails. Ordinary helpers need not be `const fn`.

The subset supports numeric, bool, char, str, and String locals; supported structs,
enums and fixed arrays; typed failures and recovery; direct Carven calls; local
mutation; conditional control, loops and matching; builtin printing and
formatting. It uses the shared structured executor. Ordinary calls do not require
`const fn`. Runtime integer operations use the language's wrapping rules; required
constant arithmetic remains checked. A const function called at runtime also uses
runtime arithmetic and output behavior. Floating operations use the compiler host's
native environment. Floating printing and formatting use the host standard library,
including dynamic width and precision within execution budgets.

The driver rejects collected `.cpp` files. Admission rejects C++ header imports
and source fragments in any collected module, including unimported modules.
Native calls, callable values, Write parameters, slices in executed bodies, and
entry argument values also report `CV-INTERPRET-ADMISSION`. The entry must take
no parameters. Arguments after `--` are ignored by such an entry, as in native
execution.

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
this option changes only interpreted execution, with a fresh budget for each
runtime test. Repeating `--max-steps`, `--trace`, or `--tests` is an error.

Execution errors report `CV-INTERPRET-EXECUTION` with source locations and call
context; exhausted budgets report `CV-INTERPRET-LIMIT`. Completed output remains
observable. Invocation, admission, and execution failures return status 1; normal
completion returns 0, following the existing entry-result convention.

## Source inputs

Native execution, `check`, `compile`, and `interpret` require one or more explicit
`.cv` inputs. The compiler analyzes these inputs together with all collected Crafts
modules; imports resolve within that batch. Explicit and discovered files are
deduplicated by canonical path.

Input paths use UTF-8, `/` separators, and a `.cv` extension. Relative paths
determine module identities after lexical normalization:

```text
src/main.cv       -> src.main
crafts/json/io.cv -> crafts.json.io
```

The project `crafts/` directory is the package root: `json::parser` selects
`crafts.json.parser`. The reserved `std::` prefix selects the standard-library
modules of the official `carven` craft, such as `crafts.carven.std.utf.text`.

Explicit absolute Crafts paths are also accepted. For files collected from a
known Crafts root, that root determines the module identity independently of the
installation prefix. Other absolute inputs use the path starting at the first
`crafts/` component: `/opt/packages/crafts/json/parser.cv` becomes
`crafts.json.parser`. Explicit aliases of collected files retain their identity.

Application paths must be relative and stay within the working directory.
Their hierarchy is normalized lexically. Canonical filesystem paths identify
duplicate files.

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

`--stdout` selects no filesystem sink. It displays artifacts belonging to explicit
source inputs, in canonical order. Dependencies remain as includes; all collected
sources still participate in analysis. Shared interfaces are displayed when they
contain an explicit input, so an interface can include declarations from other
modules. Requested test runner and entry artifacts are also displayed.
Each artifact has a heading of this form:

```text
==> logical/path <==
```

The headings make this an inspection format. Compile the files written by a
filesystem destination when building generated C++.

## Test emission

Test emission is omitted by default. One explicit mode may be selected:

| Option | Generated test artifacts |
| --- | --- |
| `--tests`, `--tests=default` | Module test functions, the generated runner header, and the default test entry |
| `--tests=external` | Module test functions and the generated runner header, without a generated entry |

Test mode options are mutually exclusive and cannot be repeated, including aliases.
Default test mode suppresses the program entry wrapper so all generated sources
can be linked into one test executable. Compilation permits an empty test suite;
commands that run tests require at least one runtime test. External mode retains
the program entry wrapper; the consuming build owns entry selection. External mode
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
failure produce diagnostics on standard error and a nonzero status. Invocation
errors include a command help hint. Source
warnings are printed on standard error while a successful compilation and
materialization still return zero.

Carven-rendered diagnostics use color when standard error supports terminal
styling, unless `NO_COLOR` is nonempty or `TERM=dumb`. Carven does not add styling
when standard error is redirected.

For `compile`, native compilation, linking, and C++ language selection belong
to the consuming build.

## Syntax inspection

The developer commands inspect one source file without performing semantic
analysis or producing artifacts:

```shell
carven dump path/to/file.cv
carven dump tokens path/to/file.cv
carven dump ast path/to/file.cv
```

Without a kind, `dump` prints tokens followed by the syntax tree, separated by
`Tokens` and `AST` headings. It loads and lexes the source once. `--timings`
reports source loading, lexing, and parsing (when attempted) on stderr, including
on failure. Invalid options do not produce a timing report.

`dump tokens` prints the token stream after lexing. Lexical diagnostics are
reported on standard error and make the command fail. `dump ast` lexes and
parses the source, then prints the syntax tree when both stages succeed. In the
combined dump, lexical errors stop parsing, and parsing errors leave the already
printed tokens intact without printing an AST. Dump formatting is intended for
inspection and may change between compiler versions.

## Compile-time program output

Required constant execution, including `const {}` blocks and `const test`, may
use the builtin print operations.
The driver sends `print`/`println` to stdout and `eprint`/`eprintln` to stderr.
With `--stdout`, all compile-time program output goes to stderr so stdout contains
only generated artifacts. Output is emitted as execution proceeds, including on
compilations that later fail. Parsing and dump commands do not execute tests.
An incremental build that reuses generated artifacts does not rerun Carven or
replay compile-time output.
