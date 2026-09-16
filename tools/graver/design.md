# Graver implementation

[Usage and style](README.md) describe the public behavior. This document
describes the implementation under `tools/graver/src`.

## Data flow and responsibilities

```text
CLI arguments -> input paths -> SourceManager
    -> format_batch -> format each source -> FormattedBatch
    -> stdout / check report / file replacement

format: source bytes -> lexer + trivia -> parser -> layout document
    -> rendered bytes -> token/comment comparison + parse validation
```

| Component | Responsibility |
| --- | --- |
| `graver.cpp` | Command selection, source loading, diagnostics, output |
| `source/` | Owned source bytes, compiler token buffer, trivia between tokens |
| `format/` | AST annotations, document construction, output validation |
| `layout/` | Text, breaks, indentation, groups, and width-based rendering |
| `files/` | Path collection, deduplication, file replacement |
| `batch/` | Ordered formatting, accumulated diagnostics, check reports, batch writing |

The `graver-modules` target contains source, layout, and formatting code.
`graver-cli-modules` adds path discovery and batch/file output.

## Ownership and publication

The CLI loads all sources before formatting. `Source::scan` owns bytes, compiler
tokens, and trivia; its immutable views remain valid while that object stays in
place. Token source IDs refer to the source manager used for parsing.

The formatter borrows the scanned source and AST during document construction.
The AST identifies operator roles, type syntax, blocks, and groups. Original
tokens determine spelling and order. A parser span can end inside a `>>` token;
range lookup maps that offset to the containing token. The implicit entry
function created by the parser has no source-level delimiters. Its statements
participate individually in top-level spacing, alongside authored declarations;
the synthetic function and block do not introduce layout.

`format_batch` runs serially in input order and collects diagnostics from failed
inputs. It publishes `FormattedBatch` only if every input succeeds. The batch
owns formatted strings and paths, and borrows original bytes from `SourceManager`.
Keep the manager alive, unmoved, and unchanged through reporting and writing.
Batch file views end when the batch moves or is destroyed.

`write_batch` checks all destinations before replacing changed files.
`replace_file` stages each file in its destination directory, preserves
permissions, checks original bytes, and renames the staged file into place.
Replacement is per-file; failures do not roll back completed replacements.

## Layout

For N tokens, N+1 gaps hold whitespace and comments. Gap printing preserves
comment text and position, authored blank lines, and protected literal/C++ bytes.

Token-indexed annotations describe operators, delimiter pairs, block layouts,
and grouping ranges. Compact candidates are rendered, then lexer offsets reveal
whether headers and bodies span lines. Decisions move only from compact to
expanded; branches in the same if-chain or match/catch arm list share the
expansion decision. Branch spans include their conditions or patterns, so a
multiline header also expands sibling block bodies. Once layouts settle,
consecutive declarations supply their minimum separator from category and line
span. After ordinary layout, array-row alignment measures rendered columns and adds
spaces for matching single-line constructions. It does not change line breaks;
groups that would exceed the line width retain ordinary spacing. The final
output passes token/comment comparison and parsing before return.

`Document` owns text and child IDs in an arena. It caches flat widths and renders
with an explicit stack. Fit checks include following material on the same line,
such as closing punctuation. Indentation is emitted when text begins a line,
keeping blank lines free of generated spaces. Width is measured in UTF-8 bytes.

## Tests

C++ tests cover source preservation, layout decisions, batch results, and
file-write boundaries. CLI scenarios cover process behavior; input/output
examples specify style. Corpus checks require known-valid repository inputs to
parse, checking idempotence and output stability after horizontal-whitespace
changes. Lexically or syntactically invalid inputs must fail.
