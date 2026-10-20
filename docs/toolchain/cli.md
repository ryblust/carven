# Command-line interface

This reference defines `carven` invocation, source collection, artifact output,
and process behavior.

## Invocation

The examples assume `carven` is on `PATH`. Relative source paths use the current
directory. Source paths determine their module identities as described below.

```text
carven build [<source.cv>]
carven run [<source.cv>|<target>] [-- <arguments>...]
carven compile [options...] [<input>...]
carven check [options...] [<input>...]
carven interpret [--tests] [--timings] [--trace] [--max-steps N] <source-file>... [-- <arguments>...]
carven dump [tokens|ast] [--timings] <source-file>
```

`carven --help`, `carven build --help`, `carven run --help`,
`carven compile --help`, `carven check --help`,
`carven interpret --help`, and `carven dump --help` print help (`-h` is also
accepted). Top-level help lists commands; each command has its own usage and
options. `carven --version` and `carven -V` print `carven v<version>` followed by
a newline. With no arguments, `carven` prints the top-level help and succeeds.

Windows Unicode command-line arguments are converted to UTF-8. On POSIX,
arguments, including the executable name, must be valid UTF-8. Invalid text is
an input error. Empty arguments are preserved.

## Project builds

`carven build main.cv` builds a native executable at `build/main` (`build/main.exe`
on Windows). `carven run main.cv -- arguments...` builds and runs it. An explicit
`.cv` input selects this single-source plan independently of any project script.
The output name comes from the source filename without `.cv` and follows the
standard builder's target-name rules.

Without an explicit `.cv` input, `carven build` and `carven run [target]` execute
the current directory's `build.cv` as an ordinary top-level program through the
interpreter. The file is required; commands do not search parent directories.
A build script can have any filename:
`carven interpret foo.cv` executes its standard-library build calls directly.

A script calls `std::build.build(plan)` to construct native targets and return
output records. Under `carven run`, each call also selects and runs an executable
before returning. Under `carven build` or another host command, such as
`interpret`, it only builds. Plans describe executable and static-library
targets, source sets, native compilation and link options, and target dependencies.

The build function selects the single executable output for `carven run`;
static libraries do not participate in selection. Multiple executable outputs
require `carven run <target>`. All declared targets build before selection and
execution. Each call completes before the next script statement. Arguments following `--` pass unchanged, including empty strings and
shell characters. A failed build or nonzero program exit reports a build error
and status 1. Source paths without a command are rejected with usage guidance.

## Source collection

`check`, source compilation, and `interpret` analyze the explicitly supplied
sources and their required modules from the matching toolchain's `crafts/carven/`
inventory. They read and parse toolchain sources only when the source dependency
closure reaches them.
They do not scan the project's `crafts/` directory. The collector deduplicates
files by canonical path and retains absolute physical source paths for diagnostics
and generated source mapping. It sorts the batch by these paths. Distinct files
with conflicting module identities are errors. Directory symlinks are not
recursively followed when collecting toolchain sources.

The installed layout is `<prefix>/bin/carven` with matching resources in
`<prefix>/crafts/carven/`. The compiler resolves its real executable path and uses
the `crafts/` directory beside its binary directory. Missing resources report an
incomplete installation. Development binaries locate Crafts in their containing
source checkout, identified by `src/carven.cppm` and `crafts/carven/`. All source
commands use this resolver.

### Installed Crafts

The project's `crafts/` directory holds installed source packages. `std::build`
collects its `.cv` sources for application targets and compiles its `.cpp`
providers alongside generated implementations. Scripts pass their own project
Craft dependencies as explicit inputs, like other interpreted programs. These inventories include files in `examples/` and `tests/`.
Carven analyzes every supplied `.cv` file and its required toolchain dependencies,
executes their static roots and `const test` bodies, and emits runtime tests from
explicitly supplied modules when requested.

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
follow the ordinary module-path rules and are combined with the matching
toolchain sources.

## Native execution

`carven run` builds through the standard library and executes the native binary.
One executable entry is required per target. Native headers include generated,
installed Craft, project Craft, and project roots. The default C++ driver comes
from `CXX`, otherwise `clang++` on PATH. It is one executable path, not a shell
command. An unset or empty `CXX` uses the default. Environment values use Unicode
on Windows and valid UTF-8 on POSIX. The toolchain supplies GCC/Clang-style
C++20 arguments.

Generated code, objects, and dependency records remain in the plan's output
directory (`build/` by default). Native output paths resolve from that directory;
absolute output paths are used directly. Child processes inherit the working directory,
environment and standard streams and are launched without a shell. POSIX signal
termination returns `128 + signal`. `compile --tests` generates native test
artifacts for a consumer build; `interpret --tests` runs supported tests directly.

## Timing reports

`--timings` enables a human-readable report on standard error for `check`,
`compile`, `interpret`, and `dump`:

```shell
carven check --timings main.cv
carven compile --timings main.cv -o emit
carven interpret --timings main.cv
carven dump --timings main.cv
```

The report shows the outcome, total wall-clock duration, and the stages executed,
in a table with duration and percentage of total time. Durations use milliseconds
or seconds; values below display precision use `<0.1 ms` or `<0.1%`.
Lexing and parsing accumulate across sources. Semantic analysis includes
compile-time execution and static tests.
Checking, compilation, and runtime commands also report source collection.
Interpreted runs label their runtime phase `Execution`; `compile` ends with C++
artifact output. Build and run do not accept a top-level `--timings` option.

Total time includes pipeline setup, diagnostics and command-resource cleanup.
Stage percentages use the unrounded durations. Semantic detail intervals are
included in the semantic total; these nested intervals must not be added to it.
Failed commands report the stages attempted. Invalid command options
produce diagnostics without a timing report.

Timing reports use stderr and leave program and artifact streams intact.
`interpret --trace --timings` includes both execution traces and the final report.
For interpretation and native runs, `--` ends command-option parsing. Reports
are intended for human reading.

## Checking

`check` collects the matching toolchain sources and analyzes the resulting source
batch through the same semantic pipeline as `compile`, including compile-time
execution and `const test` execution. Ordinary
functions and runtime tests receive semantic checks without execution. An entry
point is optional.

The command completes after semantic analysis. Without `-o`, it produces no
artifacts. `-o <dir>` saves retained semantics and a manifest for later C++
generation. Checking does not generate C++ or select a native linkage domain.
Native overload resolution, template instantiation, and native type properties
are checked by the C++ compiler.

`check` accepts source inputs or a retained semantic directory. It requires at least
one input and accepts `-o <dir>`, `--timings`, and standalone
`--help` and `-h`. Invocation, input, and analysis
errors return status 1; success returns 0, including when warnings are reported.
Successful checks print `carven: check passed` on stderr. With `--timings`, the
timing report adds the duration to this success message instead of printing it twice.

## Interpretation

Interpretation executes Carven's semantic operations using runtime arithmetic and
execution rules. It diagnoses an unsupported operation when execution reaches it.
Standard capabilities have implementations in the semantic executor. Other
native C++ implementations require native execution or an explicit hosting binding.

`interpret` collects and analyzes sources normally, including execution of
`const` initializers, blocks, and tests. It then executes the published program,
checking operation support after operands complete. Program execution requires
exactly one entry across its inputs: top-level executable statements or `main`.
`check` and `compile` can process several modules with independent entries.
Declaration-only and empty files supply no entry.

Unused functions receive ordinary language checks; only `const fn` declarations
require executor capability checking before execution. Source functions called
during static execution must be declared `const fn`. Interpretation itself emits
no C++; a program can explicitly call `std::compiler` or `std::build` to create artifacts.

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

C++ header imports and native source fragments do not prevent interpretation.
The interpreter does not compile or load native source. Its internal capability
adapter executes the compiled Standard Craft functions, fields, methods, and conversions.
Other reached native operations and callable implementations outside the supported
subset report an execution diagnostic at the operation. Unlinked native providers'
global initialization and destruction do not run. Programs that depend on those
effects require native execution.
The interpreted entry must take no parameters. Arguments after `--` are not
passed to an entry parameter. `std::process.arguments` queries the interpreter
host's complete argument list, excluding its executable name.

`--trace` reports executed statement locations and function calls and successful
returns to stderr, indented by call depth. It describes interpreted execution after
analysis, not compile-time execution, and does not record every expression or variable
value. Program stderr shares that stream. Combined stdout/stderr display order is
not a complete execution log.

Ordinary CLI execution has no default cumulative work quota. `--max-steps N`
sets a nonnegative decimal step budget; zero permits no steps. Steps charge
expression evaluation, statements, and loop progress using the shared executor;
nested calls share the root budget. This does not limit elapsed time, input
waiting, timer waiting, or blocking output. Per-value size, aggregate depth,
and call-depth limits still apply. Compile-time roots and hosted calls retain
their own budgets. This option changes only interpreted execution, with a fresh
budget for each runtime test. Repeating `--max-steps`, `--trace`, or `--tests`
is an error.

Assertions, test reports, and runtime traps use the runtime report layout with
source locations and call context. Other execution errors report
`CV-INTERPRET-EXECUTION`; exhausted budgets report `CV-INTERPRET-LIMIT`. Completed
output remains observable. Invocation, admission, and execution failures return
status 1; normal completion follows the entry-result convention.

## Source inputs

`check` and `interpret` require one or more explicit `.cv` inputs.
Source compilation requires positional sources, unless its
input is a retained artifact directory.

Source paths use UTF-8, `/` separators, and a `.cv` extension. Project-root-relative
paths determine module identities:

```text
src/main.cv       -> src.main
crafts/json/io.cv -> crafts.json.io
```

The project `crafts/` directory is the package root: `json::parser` selects
`crafts.json.parser`. The reserved `std::` prefix selects the standard-library
modules of the official `carven` craft, such as `crafts.carven.std.utf`.

Explicit absolute paths to matching toolchain sources are also accepted. The
known toolchain root determines their module identity independently of the
installation prefix. Explicit aliases of collected files retain their identity.

Application paths may be relative or absolute. Both forms are resolved through
the filesystem and assigned to their source root. The root-relative path
determines the module identity. Application sources must resolve within the
working directory. Canonical filesystem paths identify duplicate files.

Every derived component must match `[A-Za-z_][A-Za-z0-9_]*`; language keywords
are permitted as module components. Two inputs cannot derive the same canonical
module path.

`compile`, `check`, and `interpret` use positional inputs to select the complete
owned source set. Project Crafts needed by that batch must be named explicitly. Module
identities follow the source paths.

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

Source compilation generates implementations for the loaded source closure,
including required toolchain modules. Explicit inputs select runtime tests and
`--stdout` output. Supplying an installed source explicitly selects it by the same
rules.

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
Those retained modules keep their external implementation ownership when the
consumer is retained or compiled again. An artifact used alone preserves the
original program's source roots and implementation ownership.
The new batch cannot redefine those standard modules. The build system owns
dependency freshness, including changes to source files and the compiler executable.

`carven/manifest.json` describes the operation's loaded inputs, outputs,
runtime-test count, and cacheability. Each input records explicit-root selection
as `owned`, toolchain identity as `standard`, and implementation ownership outside
this generation as `external`. Analysis that observes host input, time, or
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

Consumers use checked toolchain definitions or analyze required toolchain sources.
Source analysis generates those modules' implementations; checked dependency
providers supply their own implementations. Both paths generate the interfaces
and generic instances the consumer requires. Toolchain program entries and
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
