# Workspace analysis

`tools/workspace` owns document revisions, snapshots, and cached source queries.
It depends on compiler analysis, which analyzes each supplied source batch in full.
The [analyzer](../analyzer/README.md) consumes these queries in a resident process.

## Inputs and ownership

`WorkspaceAnalysisHost::update(document, version, text)` owns a document's source bytes.
Equal or older versions are rejected. Identical bytes advance the version while
reusing the content owner. Removing a document permits reopening it with a new
version sequence.

`WorkspaceProjectModule` maps document identities to canonical module paths. Document keys
may be URIs or untitled buffers; they are never interpreted as filesystem paths.
The caller supplies the closed module set, including imported Carven modules.

`WorkspaceAnalysisSnapshot` retains document versions and input owners. Syntax and
semantic query results retain the owners needed to interpret source locations and
types. Workspace symbols copy names and ranges; their source versions belong to
the snapshot. Client versions are separate from cached content.
Definition and reference locations carry the destination document's version.
Offsets and half-open ranges use UTF-8 bytes. Updates and lazy queries must be
serialized; the library does not synchronize shared caches.

## Queries

| Query | Contract |
| --- | --- |
| `syntax` | Source and diagnostics. `syntax()` returns a complete AST without errors; `recovered_syntax()` also returns complete top-level items retained after recoverable errors. |
| `document_symbols`, `workspace_symbols` | Parsed declarations, including recovered items. Workspace symbols list top-level declarations. These queries do not resolve names. |
| `semantic` | Full analysis of the selected module set, including diagnostics and captured static output. `program()` is available only after semantic publication succeeds. |
| `hover` | Type information at recorded source tokens. Published type IDs belong to the returned program owner; failed analysis can retain concrete builtin types from completed bodies. |
| `definition` | The source declaration identified by name resolution, independently of whether its type is available. |
| `references` | Recorded occurrences identifying the same declaration, including the declaration itself. |

Hover, definition, and references consume one source occurrence index. Semantic
construction records source locations and resolved identities; the query layer
indexes these observations.
Declaration selections derive from catalog symbols, local binding origins, and
the selected record field's syntax. The same metadata supplies declaration
occurrences and resolved uses, including constants, enum cases, and class operations.
Types are attached to direct source tokens: binding and reference names, literals,
operators, access markers, and control keywords. Queries match these token ranges.
Published composite values without their own token anchor expose their types at
binding names; their observed children remain queryable.

Declaration occurrences are available after the declaration and nominal gates
succeed. A body's observations are committed only after its construction succeeds;
another body's failure does not remove them. Parse and import failures prevent
semantic observation. Static-parameter template bodies, specialized instances,
closure signatures and interiors, and module-level const/test bodies are outside
the body observer. Type annotation syntax is not indexed. Ordinary functions
retain observations from constructed source expressions before executable
residualization, including local constant initializers, checked static conditions,
and fields selected within those expressions. Module constant initializer
expressions are outside the observer.

`SourceOccurrence` carries a location, an optional resolved definition, and an
optional `SourceType`. Successful publication provides `TypeID` values belonging
to that program; failed analysis retains only known `BuiltinType` values.
Definition availability is independent of type availability. These observations
do not certify solved effects, ownership, or program validity. Compilation
requires the complete semantic publication gates.

Syntax recovery discards failed items atomically and retains complete independent
items. Synchronization respects parentheses, brackets, braces, and interpolation
boundaries, so nested declarations cannot become module items. Lexical failures,
initial import failures, and delimiter preflight failures provide no recovered
tree. Per-item recursive syntax depth failures can be recovered; unmatched or
excessively nested delimiters fail preflight. Recovery neither retains partial
declarations nor repairs a broken function internally.

## Cache and invalidation

Syntax and symbols are lazy per document. Equal symbol values, including source
ranges, reuse the derived result and workspace index. Each snapshot caches its
most recently queried semantic module selection with order-independent mappings.
Repeated requests avoid sorting the selection again. Version-only and unselected
edits reuse content results while attaching current snapshot versions.

A query with a different selection replaces the cached project. Changing or
removing selected content drops the invalid project from the new snapshot before
another query. Retained snapshots and returned query owners keep their earlier
results alive. The next uncached semantic query analyzes the whole selected module
set. Host updates copy document maps and inherit the current project when its
inputs remain valid.

## Validation and measurement

```shell
./xmakew build
./xmakew test -g workspace
./xmakew test
./xmakew build workspace-benchmark-analysis
./xmakew run workspace-benchmark-analysis --samples 5 > /tmp/carven-workspace-cost.csv
```

The non-default benchmark generates 10, 100, and 500 independent modules, each
with one explicitly typed literal-returning function, plus an unselected document.
It reports median host-update and query times, computation-count deltas, and
weak-owner lifetime observations. `warm_semantic_hover` reports time per pair of
queries over 20 repetitions; count deltas cover the repetition group. `cold_hover`
queries an already computed semantic result. Edit queries exclude the separately
measured update. Weak owners measure retention, not allocated bytes. The workload
provides no latency threshold for other module graphs or compiler workloads.
