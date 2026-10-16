# Command-line interface

This reference defines `carven` invocation, source collection, artifact output,
and process behavior.

## Invocation

```text
carven [--tests] [--timings] <source-file>... [-- <arguments>...]
carven compile [options...] [<input>...]
carven check [options...] [<input>...]
carven interpret [--tests] [--timings] [--trace] [--max-steps N] <source-file>... [-- <arguments>...]
carven dump [tokens|ast] [--timings] <source-file>
```

`carven --help`, `carven compile --help`, `carven check --help`,
`carven interpret --help`, and `carven dump --help` print help (`-h` is also
accepted). Top-level help lists commands; each command has its own usage and
options. `carven --version` and `carven -V` print `carven v<version>` followed by
a newline. With no arguments, `carven` prints the top-level help and succeeds.

## Source collection

By default, `check`, source compilation, native execution, and `interpret` combine
explicit application inputs with `.cv` and `.cpp` files collected recursively from the toolchain's
`crafts/carven/` and the working directory's optional `crafts/`. Other application
files must be named explicitly. The collector assigns module identities relative
to each Crafts root, deduplicates files by canonical path, and retains absolute
physical source paths for diagnostics and generated source mapping. It sorts the
batch by these paths. Distinct files with conflicting module identities are errors.
Directory symlinks are not recursively followed.

The installed layout is `<prefix>/bin/carven` with matching resources in
`<prefix>/crafts/carven/`. Discovery checks that the `carven` directory exists.
Development binaries locate Crafts in their containing source checkout, identified
by `src/carven.cppm`, `xmake.lua`, and `crafts/carven/`. Additional native libraries,
compiler flags, dependency downloads, and build scheduling belong to an external
build system.

### Installed Crafts

The project's `crafts/` directory holds installed source packages. Every `.cv`
and `.cpp` under the collected roots participates, including files in `examples/`
and `tests/`. Carven analyzes all `.cv` files, executes their static roots and
`const test` bodies, and emits runtime tests from owned modules when requested.
Native execution compiles collected `.cpp` files alongside generated implementations;
those sources include their headers. Official and third-party Crafts follow the
same rules.

Package authors or users select sources for the target platform and configuration
before placing them in `crafts/`. Packaging tools can prepare the same installed
layout. Installed sources must build together, with distinct module identities
and available native dependencies. Library sources must leave
program entry selection to the application. Keep independent examples, intentional
compilation-failure tests, and alternative build targets outside the collected
roots. Analysis errors and static-test failures in any collected module fail the
command, including modules the application does not import.

For custom integration, place external repositories outside the collected roots,
for example in `thirdparty/`. Supply selected `.cv` files explicitly to Carven.
Supply C++ sources, include paths, defines, compiler options, and libraries to the
native build through its ordinary target configuration. Explicit `.cv` inputs
follow the ordinary module-path rules and are combined with the automatically
collected Crafts.

## Native execution

Carven analyzes all collected modules, generates C++, compiles and links a native
program, then executes it. Program execution requires one entry point.
`carven --tests` compiles and runs ordinary runtime tests, requires at least
one runtime test, and leaves the program entry unexecuted. Static tests run during
analysis. Failed checks accumulate; `require` and `fail` stop the current test,
then later tests continue. Assertion failures and runtime traps abort the process.
Any test failure produces a nonzero exit status.
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
Windows native execution links Winsock for the installed TCP craft (`ws2_32.lib`
for MSVC-style drivers and `-lws2_32` otherwise).

Generated files and the executable reside in a unique temporary directory,
removed when the driver returns after execution or a handled failure. Carven
returns the native compiler's failure status or the program's exit status. On
POSIX, termination by signal yields `128 + signal`.
Native compilation failures identify the failed phase, compiler, and exit status
after the compiler's own diagnostics.

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
in a table with duration and percentage of total time. Durations use milliseconds
or seconds; values below display precision use `<0.1 ms` or `<0.1%`.
Lexing and parsing accumulate across sources. Semantic analysis includes
compile-time execution and static tests.
Checking, compilation, and runtime commands also report source collection.
Native runs report C++ compilation and linking. Native and interpreted runs label
their runtime phase `Execution`; `compile` ends with C++ artifact output.

Total time includes pipeline setup, diagnostics and command-resource cleanup.
Stage percentages use the unrounded durations. Semantic detail intervals are
included in the semantic total; these nested intervals must not be added to it.
Failed commands report the stages attempted. Native runs report the program's
exit code. Invalid command options
produce diagnostics without a timing report.

Timing reports use stderr and leave program and artifact streams intact.
`interpret --trace --timings` includes both execution traces and the final report.
For interpretation and native runs, `--` ends command-option parsing. Reports
are intended for human reading.

## Checking

`check` collects the fixed Crafts roots and analyzes the resulting source batch
through the same semantic pipeline as `compile`, including compile-time
execution and `const test` execution. Ordinary
functions and runtime tests receive semantic checks without execution. An entry
point is optional.

The command completes after semantic analysis. Without `-o`, it produces no
artifacts. `-o <dir>` saves retained semantics and a manifest for later C++
generation. Checking does not generate C++ or select a native linkage domain.
Native overload resolution, template instantiation, and native type properties
are checked by the C++ compiler.

`check` accepts source inputs or a retained semantic directory. It requires at least
one input and accepts `--source` mappings, `-o <dir>`, `--timings`, and standalone
`--help` and `-h`. Invocation, input, and analysis
errors return status 1; success returns 0, including when warnings are reported.
Successful checks print `carven: check passed` on stderr. With `--timings`, the
timing report adds the duration to this success message instead of printing it twice.

## Interpretation

Interpretation executes Carven's semantic operations using runtime arithmetic and
execution rules. It diagnoses an unsupported operation when execution reaches it.
Native C++ implementations require native execution.

`interpret` collects and analyzes sources normally, including execution of
`const` initializers, blocks, and tests. It then executes the published program,
checking operation support after operands complete. Program execution requires
one entry: top-level executable statements or `main`. Declaration-only and empty
files remain valid for `check` but supply no entry.

Unused functions receive ordinary language checks; only `const fn` declarations
require executor capability checking before execution. Source functions called
during static execution must be declared `const fn`. Interpretation writes no C++
artifacts or native executable.

`interpret --tests` selects ordinary runtime tests instead of the program entry.
It requires at least one runtime test and does not execute top-level statements or
`main`. Static tests still execute during analysis. Runtime tests run in canonical
module order and source order within each module, each with fresh local storage
and an independent execution budget. `check` failures accumulate;
`require` and `fail` stop the current test through helper calls and cannot be caught
as typed failures. Unsupported operations and exhausted budgets stop the current
test; later tests still run. An assertion failure or runtime trap aborts execution,
skips remaining tests, and produces no pass/fail summary. Otherwise the command
reports failures and a pass/fail summary to stderr. It returns 1 if any test fails.
Interpreter and compile-time diagnostics show up to eight call sites and report
the number of additional sites omitted.

Interpretation uses the shared structured executor for supported Carven calls,
local storage, aggregates, text, control flow, and typed failures. Integer
arithmetic follows the language's wrapping rules. Floating arithmetic and
formatting use the compiler host's native environment and standard library,
within execution budgets.

The interpreter supports input configuration, reads, clock sampling, waits, and
deadline composition. Explicit `const` execution performs the same supported host work
during analysis in `check`, `compile`, native-run preparation, and `interpret`.
Ordinary calls perform it during program execution. In one compiler process,
the input cursor preserves prefetched bytes, EOF, and system errors from static
roots into subsequent interpretation.

Collected `.cpp` files, C++ header imports, and native source fragments do not
prevent interpretation. They remain native build inputs; the interpreter does
not compile or load them. Reached native operations and callable implementations
outside the supported subset report `CV-INTERPRET-ADMISSION` at the operation.
C++ global initialization and destruction do not run. Programs that depend on
those effects require native execution.
The interpreted entry must take no parameters. Arguments after `--` are ignored
by such an entry.

`--trace` reports executed statement locations and function calls and successful
returns to stderr, indented by call depth. It describes interpreted execution after
analysis, not compile-time execution, and does not record every expression or variable
value. Program stderr shares that stream. Combined stdout/stderr display order is
not a complete execution log.

`--max-steps N` supplies a nonnegative decimal step budget, defaulting to 100,000.
Steps charge expression evaluation, statements, and loop progress using the shared
executor; nested calls share the root budget. It does not limit elapsed time,
input waiting, timer waiting, or blocking output. Existing per-value, call-depth, aggregate, and cumulative text-work
limits also apply. Each compile-time root keeps its own analysis budget;
this option changes only interpreted execution, with a fresh budget for each
runtime test. Repeating `--max-steps`, `--trace`, or `--tests` is an error.

Assertions, test reports, and runtime traps use the runtime report layout with
source locations and call context. Other execution errors report
`CV-INTERPRET-EXECUTION`; exhausted budgets report `CV-INTERPRET-LIMIT`. Completed
output remains observable. Invocation, admission, and execution failures return
status 1; normal completion follows the entry-result convention.

## Source inputs

Native execution, `check`, and `interpret` require one or more explicit `.cv`
inputs. Source compilation requires positional sources or mappings, unless its
input is a retained artifact directory.

Source paths use UTF-8, `/` separators, and a `.cv` extension. Relative paths
determine module identities after lexical normalization:

```text
src/main.cv       -> src.main
crafts/json/io.cv -> crafts.json.io
```

The project `crafts/` directory is the package root: `json::parser` selects
`crafts.json.parser`. The reserved `std::` prefix selects the standard-library
modules of the official `carven` craft, such as `crafts.carven.std.utf`.

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

`compile` and `check` accept repeatable
`--source=<canonical-module>=<path.cv>` arguments alongside positional inputs.
Both commands require at least one positional input or mapping.
A mapping supplies module identity independently of its physical path, for example
`--source=app.main=/work/src/main.cv`. Mapped invocations select their complete
owned input set and load the toolchain sources without discovering project Crafts.
Identical physical files are deduplicated. Conflicting identities for one file
and duplicate module identities across different files are rejected.

## Artifact destinations

`check -o <dir>` writes retained semantics and a manifest. `compile` writes C++
and a manifest, defaulting to the current directory. Use `-o <dir>` to select its
directory or `--stdout` to inspect C++. Select at most one destination.

| Option | Destination |
| --- | --- |
| `-o <dir>` | Generated artifacts below a directory |
| `--stdout` | Human-oriented inspection output on standard output |

Repeating or mixing destination options is an error. The default mode does not
create an implicit output directory.

After successful analysis or generation, Carven writes the operation's artifacts
and `carven/manifest.json` below the destination: it creates parent directories,
truncates existing files, and writes their new contents. The first I/O failure stops the write; files earlier
in the order may already have changed. Directory isolation, stale cleanup, failure protection, and
content-stable incremental promotion belong to the caller or build system.

`--stdout` selects no filesystem sink. It displays C++ artifacts belonging to explicit
source inputs, or owned inputs from a supplied artifact directory, in canonical order.
Dependencies remain as includes. In source compilations, all collected sources
participate in analysis. Shared interfaces are displayed when they
contain an explicit input, so an interface can include declarations from other
modules. Requested test runner and entry artifacts are also displayed.
Each artifact has a heading of this form:

```text
==> logical/path <==
```

The headings make this an inspection format. Compile the files written by a
filesystem destination when building generated C++.

Compilation generates implementations for its owned inputs. Explicit sources and
project Crafts are owned; automatically loaded toolchain sources are dependencies.
Supplying an installed source explicitly makes it an owned input for that invocation.

`compile` also accepts an existing analysis directory. With that input
alone, it generates C++ from the retained program without parsing or analyzing
sources or repeating static execution. Ordinary and test generation can share
the same analysis result:

```sh
carven check main.cv -o build/checked
carven compile build/checked -o build/program
carven compile build/checked --tests -o build/tests
```

An artifact directory containing only standard-library sources can accompany new
source inputs. Its checked definitions supply generic and staged bodies; new
instances and checks involving consumer types belong to the consuming analysis.
The new batch cannot redefine those standard modules. The build system owns
dependency freshness, including changes to source files and the compiler executable.

`carven/manifest.json` describes the operation's inputs, outputs, ownership,
runtime-test count, and cacheability. Analysis that observes host input, time, or
waiting reports `cacheable: false`. `--stdout` writes no manifest or semantic IR.

Use staging paths and check the exit status before publishing outputs. Changing
an owned source batch invokes analysis using the selected standard definitions;
all selected modules participate, including their static effects and tests.

## Test emission

Test emission is omitted by default. One explicit mode may be selected:

| Option | Generated test artifacts |
| --- | --- |
| `--tests` | Module test functions, the generated runner header, and the generated test entry |
| `--tests=runner` | Module test functions and the generated runner header, without a generated entry |

Test mode options are mutually exclusive and cannot be repeated. `--tests`
suppresses the program entry wrapper so all generated sources
can be linked into one test executable. Compilation permits an empty test suite;
commands that run tests require at least one runtime test. Runner mode retains
the program entry wrapper; the consuming build owns entry selection. Runner mode
supplies the generated runner function through
`carven/generated/carven-test-runner.hpp`; its consumer owns the process entry
point and may pass a custom reporter.

## Toolchain sources

The driver locates the matching `crafts/carven/` inventory from the compiler
installation. These modules have a toolchain-owned generated
identity independent of application domains and native build-cache keys.
Project files are identified by their actual source ownership, including project
Crafts whose module names begin with `crafts.carven`.

Consumers use checked toolchain definitions or analyze toolchain sources, then
generate the interfaces and generic instances they require. Explicitly owned
toolchain sources also generate implementations. Toolchain program entries and
interfaces merging toolchain and application modules are rejected.

## Diagnostics and status

Invalid invocation, input loading failure, source errors, and artifact-sink
failure produce diagnostics on standard error and a nonzero status. Invocation
errors include a command help hint. Source
warnings are printed on standard error while a successful compilation and
materialization still return zero.

Source diagnostics are printed in source order within each file. Each function,
test, and `const` block reports its first error, so one run shows every
independent body error. A body that depends on the inferred result or failures
of a failed function reports nothing until that function is corrected. Failure
contracts, ownership, and constant tests are checked only after every body is
accepted.

A diagnostic names the rejected facts: expected and actual types, undeclared
failure types, or the nearest similarly spelled name. Labels on one source line
share that line. Trailing `note:` lines add context, and `help:` lines suggest a
source change for the rejected contract.

Carven-rendered diagnostics use color when standard error supports terminal
styling, unless `NO_COLOR` is nonempty or `TERM=dumb`. Carven does not add styling
when standard error is redirected.

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
human inspection.

## Compile-time program output

Compile-time execution, including `const {}` blocks and `const test`, may
use the builtin print operations.
The driver sends `print`/`println` to stdout and `eprint`/`eprintln` to stderr.
With `--stdout`, all compile-time program output goes to stderr so stdout contains
only generated artifacts. Output is emitted as execution proceeds, including on
compilations that later fail. Parsing and dump commands do not execute tests.
An incremental build that reuses generated artifacts does not rerun Carven or
replay compile-time output.
