# Graver

Graver formats Carven source using the compiler lexer and parser. Formatting
requires valid syntax; it does not resolve imports, type-check programs, or
execute compile-time functions and tests.

## Build and use

From the repository root:

```sh
./xmakew build graver
./xmakew test -g graver
./xmakew run graver input.cv
./xmakew run graver check src examples
./xmakew run graver write src examples
./xmakew run graver help check
```

Use `.\xmakew.ps1` on Windows. The executable accepts:

| Command | Behavior |
| --- | --- |
| `graver [FILE or -]` | Format one input to stdout; omitted input reads stdin |
| `graver check FILE/DIR ...` | List changed paths on stdout without writing |
| `graver check -` | Check stdin; report a difference as `stdin` |
| `graver write FILE/DIR ...` | Replace changed files silently |
| `graver help [COMMAND]` | Show general or command-specific help |

`check` and `write` require inputs; use `.` for the current directory. Stdin must
be used alone and cannot be written. Only the first argument recognizes command
names. Use `./help`, `./check`, or `./write` as filenames in default mode.
Dash-prefixed arguments, including `--help` and `--`, are literal paths.

Directory inputs recursively select `.cv` files, skipping nested hidden/build
directories and symlink entries. Explicit files may have any extension. Paths
are sorted and deduplicated; check reports use cwd-relative paths where possible.
Explicit file symlinks can be read but are rejected by write.

Exit status is `0` for success, `1` for check differences, and `2` for an error.
Diagnostics use stderr. All selected sources are validated before reporting
changes or writing. Each changed file is staged beside its destination, keeps
its permission bits, and is checked against its original bytes before replacement.
A later I/O failure can leave earlier files updated. The byte comparison does
not lock the file. Unchanged files are not rewritten.

## Style

Graver uses one fixed style: four-space indentation and a target line width of 100.

- Lists prefer one line when they fit and break at separators otherwise. Arrays
  containing constructions or nested arrays place each element on its own line.
- Named constructions with multiple fields expand one field per line.
  Constructions containing a nested construction or array also expand.
  Single-field and positional leaf constructions may remain on one line.
- Import selections attach braces to `::` and omit inner padding, as in
  `std::{vector, allocator}` and `using {from_utf8, to_string}`. Long
  selections put one name per line.
- Adjacent single-line constructions in an array align their columns and closing
  braces when their written types and field layouts match. Comments, blank lines,
  multiline rows, and differing layouts separate groups. Padding is omitted for
  a group if it would exceed the line width. Column widths use UTF-8 bytes.
- Function and closure bodies are expanded by default. Expression-bodied
  functions keep their `=>` syntax; formatting does not rewrite function bodies.
- Nonempty struct and enum declarations expand with one member per line.
  Empty declarations keep `{}`. Range operators use `0..7` spacing.
- Within an if-chain or a match/catch arm list, simple branch blocks may fit on
  one line. A complex body, comment, authored blank line, or multiline branch
  expands all nonempty block bodies in that group. Expression-only arms keep
  their syntax. Loop/test bodies and match/catch lists stay expanded.
- Adjacent top-level declarations of the same category stay together when both
  are single-line. Different categories or a multiline declaration require one
  separating blank line. Visibility modifiers do not change the category.
- Author-written blank lines retain their count, including inside blocks and at
  file boundaries. Spaces on blank lines are removed. Declaration-leading
  comments stay with the declaration, after any added separator.

Ordinary line endings become LF. Nonempty output ends in a newline. Token and
literal spelling, punctuation, comment text and token-gap position, interpolation
text/specifications, and fenced C++ content are preserved. Expressions inside
interpolation holes are formatted. Output is re-lexed, compared with the input,
and parsed before it is returned.

Width counts UTF-8 bytes, so non-ASCII text may wrap early. Indivisible tokens,
comments, C++ fragments, and type-argument lists may exceed the target.

## Tests and implementation

The `graver` group contains three test areas:

- `tests/internal/`: C++ boundary and repository-corpus tests.
- `tests/cli/`: the process harness and CLI scenarios.
- `tests/format/`: reviewed source-formatting examples.

Examples contain `input.cv` and `expected.cv`; they check exact output and
idempotence. CLI scenarios check exit status, both streams, and file changes
in isolated temporary directories.
Failed scenarios retain their files and captured streams. Repository `.cv` inputs
also exercise idempotence and horizontal-whitespace normalization.

See [implementation design](design.md) for component responsibilities
and ownership. Build targets are in `tools/graver/xmake.lua`.
