# Command-line interface

The `carven` command compiles an explicit batch of `.cv` source files, inspects
frontend representations, and reports its version and command help. This
document defines invocation, input paths, output writes, and process behavior.

## Invocation

```text
carven [options...] <source-file>...
carven dump tokens <source-file>
carven dump ast <source-file>
```

`-h` and `--help` print command help. `-V` and `--version` print
`carven v<version>` followed by a newline. With no arguments, `carven` prints
the top-level help and succeeds.

## Source inputs

A compile invocation requires one or more explicitly named source files. The
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

One destination mode is always active, and at most one may be selected
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

This output is intended for inspection rather than as a machine protocol or a
direct C++ compiler input stream.

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

Native compilation, linking, and C++ language selection belong to the consuming
build.

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
formatting is developer inspection output rather than a stable machine
protocol.
