# Workspace analysis

`tools/workspace` owns document revisions, immutable snapshots, query selections,
and navigation indexes for the analyzer. It uses the compiler's `SourceDocument`,
`ParsedDocument`, and `CompilationAnalysis` contracts. Filesystem loading and
protocol transport belong to its caller.

## Inputs and ownership

`WorkspaceAnalysisHost::update(document, version, text)` owns source bytes and
requires increasing versions. Identical bytes advance the version while retaining
the content owner. Removing a document permits a new version sequence on reopen.
Document keys may be paths, URIs, or untitled buffers.

Batch updates validate every version before publishing changes together. Duplicate
keys, stale versions, and source-load failures reject the whole batch and identify
the failing document. Successful results follow input order; an empty batch changes
nothing. The single-document overload uses the same transaction.

`WorkspaceProjectModule` maps document keys to canonical module paths. The caller
supplies a closed project containing its imported modules. Duplicate mappings and
missing documents are compilation-input errors.

Snapshots retain their versions and source owners. A parsed document owns its
original tokens, recovered syntax, and diagnostics. Completion, import edits,
signature help, folding, and decorations reuse that token sequence. Each semantic
selection binds syntax to its own source/module identities and resolved imports.
Query results retain the analysis owning their sources, diagnostics, captured
static output, observations, and published program. Locations use half-open UTF-8
byte ranges and carry destination versions. Host updates and lazy queries must be
serialized.

Workspace binds standard native providers with host environment effects denied.
Static output is captured in the analysis result. A cached editor observation
cannot satisfy a fresh command's static-execution obligations.

## Queries

| Query | Result |
| --- | --- |
| `syntax` | Source, diagnostics, and complete or recovered syntax |
| `document_symbols`, `workspace_symbols` | Parsed declarations; workspace symbols contain top-level declarations |
| `semantic` | Full analysis of the selected project, including diagnostics and static output |
| `hover`, `definition`, `references` | Known types, detached descriptions, and selected source identities |
| `completion` | Visible candidates, replacement span, and auto-import edits |
| `auto_imports` | Importable source/provided declarations filtered by visibility and type context |
| `prepare_rename`, `rename` | Symbol spelling and binding-preserving target-wide edits |
| `signature_help` | Call/parameter labels and active parameter |
| `prepare_call_hierarchy`, `incoming_calls`, `outgoing_calls` | Callable identities and grouped source call sites |
| `folding_ranges` | Code, import, and comment spans from syntax and lexical gaps |
| `document_highlights` | Current-document references, preserving import-alias identity |
| `decorations` | Identifier roles/modifiers and inferred-type/parameter hints |
| `formatting`, `code_actions`, `organize_imports` | Replacement edits for client application |

Hover and definition reuse complete source observations for the exact target,
module mappings, and current source owners. Complete construction can retain
navigation facts after validation fails while `program()` remains absent.
Otherwise, navigation selects the document's transitive import closure through
frontend dependency discovery. Semantic and reference queries use the complete
caller-selected project. Uncached selections reuse parsing and run semantic checks.

## Incomplete source

The frontend retains complete items and statements around lexical/syntax errors,
with original byte spans. The ordinary syntax accessor requires error-free parsing;
recovered syntax may be partial. Invalid literals remain opaque. Unresolved imports
and incomplete module scopes can prevent binding lookup.

Source queries reuse detached observations from failed semantic construction.
When more facts are needed, recovery excludes the smallest failed body statement
and retries analysis against the same immutable syntax. Each retry runs semantic
passes over the selection, so several independent errors can require several
passes. Parsing remains shared across recovery passes.

Recovery preserves declarations and imports. Unavailable bindings keep their name,
source identity, and lexical shadowing; known annotations and references may survive
failed initializers. Observations merge by source identity and byte span, preserving
established facts when a later pass has no corresponding evidence. Failed bodies
remain invalid and cannot supply inferred callable contracts or published SemIR.
Original diagnostics describe the unmodified source.

When target errors lie within the document's import closure, navigation and
references can share its recovery owner. Errors outside that closure retain a
separate local navigation selection. Complete construction stops recovery even
when later validation fails.

Completion substitutes a query token for the cursor's identifier in a private
source selection. Type contexts filter to type declarations; body names honor
visibility and shadowing. An unresolved receiver has no member candidates.
Returned strings own their bytes, and replacement spans refer to the current
source. The live snapshot and its diagnostics remain unchanged.

## Navigation and presentation

Compiler observations own token identities, aliases, optional types, detached
descriptions, and proven callable targets. References group by entity; alias rename
restricts edits to the local binding and its uses. Rename analyzes an edited private
selection to check binding preservation. Invalid names and binding conflicts have
distinct errors. Published `TypeID` values belong to the returned program.

Generic source exposes resolved definitions and known concrete types. Bodies
requiring static specialization and their nested closures contribute no source
observations. References cover the recorded occurrences under those rules.

Call hierarchy joins retained call syntax with compiler-resolved identities.
Top-level statements belong to the implicit entry; anonymous closures own their
nested calls. Incoming calls include unopened reverse dependents. Sites are grouped
by caller/callee. Concrete callable types and immutable view initializers can prove
targets; dynamic views with no proven target remain absent.

Interop queries expose selected C++ paths, explicit import tokens, and declared
Carven signatures. Native header contents and fragment payloads remain opaque.
Explicit native selections enter completion as neutral names; namespace openings
cannot enumerate unknown members.

Signature help uses retained call syntax and resolved hover observations. Nested
delimiters and generic arguments determine the active parameter. Source functions
retain authored annotations; bound methods omit `self`. Typed callable variables
use detached descriptions. Unresolved callees have no signature.

Decorations combine retained declarations and source observations. Roles include
types, parameters, locals, fields, cases, functions, and methods. Modifiers identify
declarations, readonly bindings, static/async capability, and standard entities.
Type hints omit explicit or unavailable types. Parameter hints require resolved
source declarations and omit same-named arguments and implicit `self`. Results use
original byte positions with half-open range filtering.

## Edits

Formatting requires complete syntax and uses the formatter's fixed style. It trims
unchanged prefixes/suffixes on UTF-8 boundaries; an already formatted source returns
no edit. Semantic errors permit formatting.

Code actions use compiler diagnostic fixes and source import candidates. Compiler
attachments own titles and edits; Workspace maps their spans to documents. All
returned edits refer to the original snapshot; the caller owns application and
editor-version checks.

Auto imports use the compiler's shared visibility rules and canonical module
identities. Private and foreign-Craft bare names are excluded. Visible names
suppress redundant candidates; member completion omits global imports. An edit
extends a complete selection when possible or inserts an import after header
comments, preserving aliases.

Import organization sorts and merges contiguous uncommented explicit imports.
Blank-line groups, comments, native imports, wildcards, and malformed imports bound
these runs. Only successfully checked unused-import diagnostics permit removal.
The formatter lays out replacements; other text and line endings are preserved.

## Reuse

Parsing, document symbols, and folding are lazy per source content. Unchanged
documents retain their parsed owner across edits. Each snapshot computes workspace
symbols once. Semantic selections match module mappings and source owners; the
configured capacity defaults to two, and zero disables retention. Returned owners
remain valid after eviction.

Content changes invalidate affected selections. Version-only updates reuse facts
with current locations. Completion retains bounded selections keyed by normalized
source contents, module mappings, and cursor anchor, so prefix edits can reuse
analysis. Old snapshots remain independently valid.

Timing recipients observe new parsing and semantic work during their call. Cache
hits return retained results without replaying those events.

## Validation

```sh
./xmakew build
./xmakew test -g workspace
```

`carven-test-workspace` exercises editing, retained results, query reuse, and
navigation through the Workspace API.
