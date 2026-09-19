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
- Import selections attach braces to `::` and use one space inside single-line
  braces, as in `std::{ vector, allocator }` and `using { from_utf8, to_string }`. Long
  selections put one name per line. Single-line selections omit the trailing
  comma; multiline selections include it. An existing trailing comma does not
  force a line break.
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
literal spelling, punctuation other than import-list trailing commas, comment text
and token-gap position (allowing insertion/removal of those commas), interpolation
text/specifications, and fenced C++ content are preserved. Expressions inside
interpolation holes are formatted. Output is re-lexed, compared with the input,
and parsed before it is returned.

Width counts UTF-8 bytes, so non-ASCII text may wrap early. Indivisible tokens,
comments, C++ fragments, and type-argument lists may exceed the target.

## Tests

The `graver` group covers:

- `tests/internal/`: C++ boundary, formatting, and repository corpus tests.
- `tests/cli/`: the process harness and CLI scenarios.
- `tests/format/`: source-formatting inputs and expected outputs.

Examples contain `input.cv` and `expected.cv`; C++ tests discover them and call
the formatter directly in one process to check exact output and idempotence.
Repository `.cv` inputs exercise parsing, idempotence, and horizontal-whitespace
normalization. Invalid-input tests check lexical and syntax errors.

Each CLI scenario is registered separately with Xmake. The harness checks exit
status, both streams, and file changes in an isolated temporary directory.
Scenarios are declared in `tests/cli/cases.lua` and run by `tests/cli/cli.lua`.
Failed scenarios retain their files and captured streams. Run one scenario with
`./xmakew test graver-test-cli/mixed_write_failure`.

## Implementation

Components under `src/` have these responsibilities:

| Component | Responsibility |
| --- | --- |
| `graver.cpp` | Command selection, source loading, diagnostics, output |
| `source/` | Owned source bytes, compiler token buffer, trivia between tokens |
| `format/` | AST annotations, document construction, output validation |
| `layout/` | Text, breaks, indentation, groups, and width-based rendering |
| `files/` | Path collection, deduplication, file replacement |
| `batch/` | Ordered formatting, accumulated diagnostics, check reports, batch writing |

`xmake.lua` collects all components except the CLI entry in `graver-modules`,
which depends on `carven-modules`. The `graver` executable and
`graver-test-internal` consume these modules; `graver-test-cli` exercises the
executable. Both test targets belong to the `graver` group.

```text
CLI arguments -> input paths -> SourceManager
    -> format_batch -> format each source -> FormattedBatch
    -> stdout / check report / file replacement

format: source bytes -> lexer + trivia -> parser -> import comma normalization
    -> layout document -> rendered bytes -> multiline import commas
    -> token/comment comparison + parse validation
```

### Ownership and output

The CLI loads all sources before formatting. `Source::scan` owns bytes, compiler
tokens, and trivia; its immutable views remain valid while that object stays in
place. Token source IDs refer to the source manager used for parsing. The
formatter normalizes uncommented import-list trailing commas before layout. It
replaces their bytes with spaces, preserving the original AST offsets. Document
construction borrows that scanned source and the original AST. After alignment,
it adds trailing commas to multiline imports. Validation compares against the
original source, allowing only import-list trailing commas to differ.

`format_batch` runs serially in input order, collects diagnostics from failed
inputs, and returns a `FormattedBatch` only if every input succeeds. The batch
owns formatted strings and paths and borrows original bytes from `SourceManager`.
Keep the manager alive, unmoved, and unchanged through reporting and writing.
Batch file views end when the batch moves or is destroyed. `write_batch` checks
every destination before the first replacement; replacements remain per-file,
with the failure behavior described above.

### Layout construction

For N tokens, N+1 gaps hold whitespace and comments. The AST identifies operator
roles, type syntax, blocks, and groups; original tokens determine spelling and
order. A parser span can end inside a `>>` token, so range lookup maps that offset
to the containing token. The parser's synthetic entry function and block add no
layout: their statements participate directly in top-level spacing.

Token-indexed annotations describe blocks and grouping ranges. Compact
candidates are rendered, then lexer offsets reveal whether headers and bodies
span lines. Decisions move only from compact to expanded, and branches in the
same if-chain or match/catch arm list share the expansion decision. Branch spans
include their conditions or patterns. Once layouts settle, declaration spacing
and array-row alignment are applied. Alignment adds spaces without changing line
breaks and skips groups that would exceed the line width. The final output must
pass token/comment comparison and parse validation.

`Document` owns text and child IDs in an arena, caches flat widths, and renders
with an explicit stack. Fit checks include following material such as closing
punctuation. Indentation is emitted when text begins a line, keeping blank lines
free of generated spaces.
