# Workspace analysis

`tools/workspace` remains the session layer for analyzer and future LSP clients.
It owns document revisions, retained snapshots, query selections, caching, and
navigation indexes. It is independent of an LSP transport and Xmake's native
build targets. The `carven-workspace-modules` target provides these components
without a process entry point or protocol transport.

The compiler owns the shared lower-level `SourceDocument`, `ParsedDocument`, and
`CompilationAnalysis` contracts. CLI and workspace analysis use the same parsing
and complete semantic pipeline. Workspace adapts its results to versioned document
queries; it does not maintain another type checker or artifact inventory.

## Inputs and ownership

`WorkspaceAnalysisHost::update(document, version, text)` owns source bytes and
requires increasing versions. Identical bytes advance the version while retaining
the content owner. Removing a document permits a new version sequence on reopen.
Document keys may be URIs or untitled buffers; the caller supplies their text.

Use `update(vector<WorkspaceDocumentUpdate>)` for initial loading and coordinated
edits. It validates all versions against the current snapshot and publishes the
batch together, copying the document map once. Duplicate document keys, stale
versions, or source-load failures reject the whole batch and identify the failing
document. Successful change results follow input order. An empty batch does
nothing; the single-document overload uses the same transaction.

`WorkspaceProjectModule` maps document keys to canonical module paths. The caller
supplies the closed project, including imported Carven modules. Duplicate document
or module mappings and missing source documents are compilation-input errors.

Snapshots retain their versions and inputs. Immutable source bytes and line
indexes are shared across selections. A parsed document retains its syntax and
diagnostics; each compilation binds that syntax to fresh source/module identities,
provenance, and resolved imports. Query results retain a `CompilationAnalysis`
owner for the sources, diagnostics, captured static output, source observations,
and published program underlying their locations and semantic IDs. Definition
and reference locations carry destination versions.
Offsets and half-open ranges use UTF-8 bytes. Host updates and lazy queries must
be serialized.

## Queries

| Query | Result |
| --- | --- |
| `syntax` | Source and diagnostics, with an error-free AST or recovered complete top-level items. |
| `document_symbols`, `workspace_symbols` | Parsed declarations; workspace symbols contain top-level declarations. |
| `semantic` | Full analysis of the selected project, including diagnostics and static output. |
| `hover` | Optional types and source/provided entity identities at recorded tokens. |
| `definition` | A source declaration or the selected local import alias. |
| `references` | Recorded occurrences of a resolved declaration across the selected project, including its declaration. |

Hover and definition select the document's transitive import closure through the
frontend source dependency discovery, including configured prerequisites of
provided contracts and duration literals. Semantic analysis checks the required
contracts when they are used. Their diagnostics, captured output, and versions describe
that selection. A target outside the project returns no result or analysis owner.
Semantic and reference queries analyze the complete caller-selected project.
Every uncached selection reuses retained document parsing but runs all semantic
and publication checks. Generation may borrow an immutable published program
without consuming the analysis result. This does not authorize a cached editor
query to satisfy a new command's compile-time execution obligations.

Syntax recovery retains complete top-level items around malformed declarations.
Nested constructs stay within their containing item. Lexical errors, malformed
initial imports, and unmatched delimiters prevent a recovered tree. The ordinary
syntax accessor exposes an AST only when parsing succeeds without errors.

## Source observations

Compiler analysis records source tokens, selected entities, local alias bindings,
and optional types. Definitions derive from the selected entity and local binding.
Provided entities carry export keys for module information.
References group by entity across aliases and include import selections.
`WorkspaceSemanticAnalysis::alias_references` restricts rename locations to one
local alias and its uses, excluding export names and other aliases.
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

Syntax and document symbols are lazy per source content. Source content changes
invalidate the affected document parse; unchanged documents keep their parse even
when the complete semantic selection must be recomputed. Each snapshot computes
its workspace-symbol collection once. Semantic selections are keyed by module
mappings and source content. The host accepts a retained-selection capacity,
defaulting to two; zero disables retention. Returned owners remain valid after
cache eviction.

Content changes invalidate affected selections in new snapshots. Version-only
updates reuse analysis with current query versions. Import edits and module
remapping determine new selections. Old snapshots and query results retain their
sources, locations, diagnostics, and output independently of host updates.

Navigation parses documents to discover imports. An uncached semantic selection
parses documents without a retained result and reuses the remaining syntax before
complete semantic analysis. Timing recipients are borrowed during that call and
receive events for new parsing and semantic computations. Cache hits do not
replay timing events.

## Validation

```shell
./xmakew build
./xmakew test -g workspace
```

Workspace scenarios share the internal test executable and exercise editing,
retained results, and navigation through the query API.
