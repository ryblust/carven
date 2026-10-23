# `carven format`

`carven format` formats Carven source using the compiler lexer and parser. Formatting
requires valid syntax; it does not resolve imports, type-check programs, or
execute `const fn` calls or tests.

## Use

With `carven` on `PATH`, run these commands from your project directory:

```sh
carven format input.cv
carven format check src
carven format write src
carven format --help
carven format help check
```

Windows Unicode command-line arguments are converted to UTF-8. On POSIX,
arguments, including the executable name, must be valid UTF-8. Invalid text is
an input error. Empty arguments are preserved.

`carven format` starts the separate `carven-format` executable beside the running
compiler. It forwards arguments, the working directory, standard streams, and
exit status. The formatter accepts:

| Command | Behavior |
| --- | --- |
| `carven format [FILE or -]` | Format one input to stdout; omitted input reads stdin |
| `carven format check FILE/DIR ...` | List changed paths on stdout without writing |
| `carven format check -` | Check stdin; report a difference as `stdin` |
| `carven format write FILE/DIR ...` | Replace changed files silently |
| `carven format help [COMMAND]` or `--help` / `-h` | Show general or command-specific help |

`check` and `write` require inputs; use `.` for the current directory. Stdin must
be used alone and cannot be written. Only the first argument recognizes command
names and help flags. Prefix conflicting filenames with `./`, such as `./help`
or `./--help`. Other dash-prefixed arguments are literal paths.

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

`carven format` uses one fixed style: four-space indentation and a target line width of 100.

- Lists prefer one line when they fit and break at separators otherwise. Arrays
  with multiple elements containing constructions or nested arrays place each
  element on its own line. Singleton arrays follow the width rule.
- Named constructions with multiple fields expand one field per line.
  Positional constructions with multiple values expand when they contain a
  nested construction or array. Single-field and single-value constructions
  follow the width rule, including when nested.
- When wrapping single-argument calls, singleton arrays, and `=>` bodies, prefer
  a complete expression on the next line when it fits. Otherwise, the wrapper can stay
  beside the expression and let it own the indentation, as in `build({ ... })`.
  Attachment must fit the current line and preserve inner groups that fit on
  their own line. Boundary comments retain ordinary multiline layout.
- Precedence parentheses follow their expression's layout without adding
  another indentation level. Operator chains use one continuation level;
  nested operands retain their own groups, so a long boolean chain does not
  unnecessarily split its short comparisons.
- Import selections attach braces to `::` and use one space inside single-line
  braces, as in `std::{ vector, allocator }` and `using { from_utf8, to_string }`. Long
  selections put one name per line. Single-line selections omit the trailing
  comma; multiline selections include it. An existing trailing comma does not
  force a line break.
- Import declarations remain separate and retain their written order, including
  selection names and authored groups.
- Adjacent single-line constructions in an array align their columns and closing
  braces when their written types and field layouts match. Comments, blank lines,
  multiline rows, and differing layouts separate groups. Padding is omitted for
  a group if it would exceed the line width. Column widths use UTF-8 bytes.
- Function and closure block bodies expand by default. Expression-bodied
  functions keep their `=>` syntax. Formatting does not rewrite function bodies.
  Empty parameter and argument lists stay `()`.
  Continuation indentation begins at a line break, without adding indentation
  to the preceding operand's nested calls.
- Nonempty struct and enum declarations expand with one member per line.
  Empty declarations keep `{}`. Range operators use `0..7` spacing.
- Statement `if` blocks always expand, including empty branches. Simple `if`
  expressions stay inline when they fit; a complex, commented, or multiline
  branch expands the whole chain. Arrow statement branches retain
  `if condition => statement;`; formatting never changes a block into an arrow
  branch. Loop bodies stay expanded.
- Within a match/catch arm list, simple branch blocks may fit on one line.
  A complex body, comment, authored blank line, or multiline branch expands
  all nonempty block bodies in that group. Expression-only arms keep their
  syntax. Match/catch lists stay expanded. Empty test and const blocks use
  `{}` unless they contain comments; nonempty bodies expand.
- Adjacent single-line top-level items of the same category may remain compact.
  Different categories or a multiline item require one separating blank line.
  The same single-line and multiline rule applies between class member functions.
  Constant and runtime tests form separate categories; visibility modifiers do
  not change the category.
- Author-written blank lines between statements, entries, and around comments are preserved
  up to one consecutive blank line. Unannotated blank lines immediately inside
  delimiters are removed. Spaces on blank lines are removed.
  Item-leading comments stay with the item, after any added separator.
  Use these blank lines to group a function's setup, processing, and result;
  formatting preserves the groups without inferring them from statement kinds.

Ordinary line endings become LF. Nonempty output ends in a newline. Token and
literal spelling, punctuation other than import-list trailing commas, comment text
and token-gap position (allowing insertion/removal of those commas), interpolation
text/specifications, and fenced C++ content are preserved. Expressions inside
interpolation holes are formatted. Output is re-lexed, compared with the input,
and parsed before it is returned.

Width counts UTF-8 bytes, so non-ASCII text may wrap early. Indivisible tokens,
comments, C++ fragments, and type-argument lists may exceed the target.

## Tests

Run from the Carven checkout root:

```sh
./xmakew build
./xmakew build carven-format
./xmakew test -g format
```

Use `.\xmakew.ps1` on Windows.

`tools/format/tests/format/` contains input/expected fixtures. Internal tests check
their exact output, source preservation, idempotence, and stability under changes
to horizontal whitespace. Repository programs exercise valid, stable formatting.
File and batch tests check replacement, permissions, and failure behavior.

`tools/format/tests/cli/` exercises `carven format` in isolated directories,
checking exit status, both streams, and file changes. Failed scenarios retain
their files and captured streams.

## Implementation

`carven-format` uses the compiler lexer and parser through `carven-format-modules`.
Components under `tools/format/src/` have these responsibilities:

| Component | Responsibility |
| --- | --- |
| `main.cpp` | Command selection, source loading, diagnostics, output |
| `source/` | Owned source bytes, compiler token buffer, trivia between tokens |
| `format/` | AST annotations, document construction, output validation |
| `layout/` | Text, breaks, indentation, groups, and width-based rendering |
| `files/` | Path collection, deduplication, file replacement |
| `batch/` | Ordered formatting, accumulated diagnostics, check reports, batch writing |

`FormattingSource` owns bytes, tokens, and trivia. Layout borrows these together
with the AST. Import-comma normalization uses a separate snapshot with preserved
offsets; validation compares the result with the original source.

The AST supplies operator roles, blocks, and expression groups. Layout groups
choose line breaks and continuation indentation; wrapper expressions can share
their element's layout. Compact branch decisions move only toward expansion.
Declaration spacing and array-row alignment follow those decisions. Alignment
preserves line breaks and stays within the target width.

`FormattingDocument` owns its nodes and renders with an explicit stack. Fit checks
include closing punctuation. Generated indentation is emitted only before text,
so blank lines remain empty and verbatim fragments keep their original bytes.

`format_batch` processes inputs in order and publishes results only when all
inputs succeed. A `FormattedBatch` owns outputs and borrows original bytes from
`SourceManager`; keep the manager alive and unchanged through reporting and writing.
File views end when the batch moves or is destroyed. Writing checks all destinations
before beginning per-file replacement.
