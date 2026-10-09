# Workspace analysis

`tools/workspace` owns document revisions, retained snapshots, and cached source
queries. Each semantic computation passes a closed source batch to the compiler
and retains its source and semantic owners. The `carven-workspace-modules` target
provides these components without a process entry point or protocol transport.

## Inputs and ownership

`WorkspaceAnalysisHost::update(document, version, text)` owns source bytes and
requires increasing versions. Identical bytes advance the version while retaining
the content owner. Removing a document permits a new version sequence on reopen.
Document keys may be URIs or untitled buffers; the caller supplies their text.

`WorkspaceProjectModule` maps document keys to canonical module paths. The caller
supplies the closed project, including imported Carven modules. Duplicate document
or module mappings and missing source documents are compilation-input errors.

Snapshots retain their versions and inputs. Query results own the sources,
diagnostics, static output, and published program underlying their locations and
semantic IDs. Definition and reference locations carry destination versions.
Offsets and half-open ranges use UTF-8 bytes. Host updates and lazy queries must
be serialized.

## Queries

| Query | Result |
| --- | --- |
| `syntax` | Source and diagnostics, with an error-free AST or recovered complete top-level items. |
| `document_symbols`, `workspace_symbols` | Parsed declarations; workspace symbols contain top-level declarations. |
| `semantic` | Full analysis of the selected project, including diagnostics and static output. |
| `hover` | Types at recorded source tokens. |
| `definition` | A resolved source declaration. |
| `references` | Recorded occurrences of a resolved declaration across the selected project, including its declaration. |

Hover and definition select the document's transitive import closure through the
compiler import resolver. Their diagnostics, captured output, and versions describe
that selection. A target outside the project returns no result or analysis owner.
Semantic and reference queries analyze the complete caller-selected project.
Every uncached selection runs all semantic and publication checks.

Syntax recovery retains complete top-level items around malformed declarations.
Nested constructs stay within their containing item. Lexical errors, malformed
initial imports, and unmatched delimiters prevent a recovered tree. The ordinary
syntax accessor exposes an AST only when parsing succeeds without errors.

## Source observations

Compiler analysis records source tokens, resolved definitions, and optional types.
Queries index these observations. Published `TypeID` values belong to the returned
program. Failed analysis can retain definitions and known builtin types from
admitted declarations and successfully constructed bodies. A failed body discards
its local observations, including nested closures. Parsing and import errors
prevent semantic observation. `program()` is available only after publication.

Generic source retains resolved definition locations and concrete source types;
unresolved generic types remain unknown. Bodies requiring static specialization
and their nested closures contribute no observations. Declaration annotations,
constants, and type extents are observed in their original context. References
are limited to the occurrences recorded under these rules.

## Caching

Syntax and document symbols are lazy per source content. Each snapshot computes
its workspace-symbol collection once. Semantic selections are keyed by module
mappings and source content. The host accepts a retained-selection capacity,
defaulting to two; zero disables retention. Returned owners remain valid after
cache eviction.

Content changes invalidate affected selections in new snapshots. Version-only
updates reuse analysis with current query versions. Import edits and module
remapping determine new selections. Old snapshots and query results retain their
sources, locations, diagnostics, and output independently of host updates.

Navigation parses documents to discover imports. An uncached semantic selection
runs the complete compiler pipeline, including parsing. Timing recipients are
borrowed during that call and receive events for new computations.

## Validation

```shell
./xmakew build
./xmakew test -g workspace
```

Workspace scenarios share the internal test executable and exercise editing,
retained results, and navigation through the query API.
