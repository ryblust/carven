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

`-h` and `--help` print command help. `-V` and `--version` print exactly
`carven v0.1.0`. With no arguments, `carven` prints the top-level
help and succeeds.

## Source inputs

A compile invocation requires one or more explicitly named source files. The
compiler analyzes that complete batch and does not discover additional files
while resolving imports.

Input paths are relative UTF-8 paths using `/` separators and a `.cv` extension.
After lexical normalization, the path without its extension becomes the source
module path. For example:

```text
src/main.cv       -> src.main
crafts/json/io.cv -> crafts.json.io
```

Every derived component must be a valid Carven identifier and cannot be a
language keyword. Input paths cannot lexically escape the invoking working
directory. Symbolic links are resolved by the host filesystem; the compiler
does not require their targets to remain within that directory. CLI inputs in
the `crafts.std` module domain are rejected because that domain is reserved for
toolchain-provided sources. Two inputs cannot derive the same canonical module
path.

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

The generated artifact roles and logical paths are defined by
[Toolchain and artifacts](toolchain.md#artifact-paths). After successful
compilation and generation, Carven writes the complete artifact set in
logical-path order: it creates parent directories, truncates existing files,
and writes their new contents. It neither removes stale or unrelated files nor
checks whether the root is isolated or safe for a particular build target. The
first I/O failure stops the write; files earlier in the order may already have
changed. Directory isolation, stale cleanup, failure protection, and
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
supplies the generated runner function through the private companion header;
its consumer owns the process entry point and may pass a custom reporter. The
exact paths and companion boundary are defined by
[Toolchain and artifacts](toolchain.md#artifact-paths).

## Linkage domain

`--linkage-domain=<value>` supplies opaque caller identity for the deterministic
private generated namespace. The equals sign is required. The value must be
nonempty, and the option may be specified at most once.

When the option is omitted, the driver derives a path domain from the
lexically-normalized absolute artifact root. The artifact root is the output
directory, the current working directory for the default output `.`, and the
current working directory as a virtual root for `--stdout`. Explicit and path
domains have distinct identities even when their text is equal.

A logical generation target must reuse its domain across edits. Different
targets whose generated objects may enter the same linked image must use
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

The driver compiles Carven into artifacts only. It does not invoke a downstream
C++ compiler or linker and accepts no C++ language-standard option.

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
