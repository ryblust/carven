# Workspace analysis

`tools/workspace` owns document revisions, retained snapshots, and cached source
queries. It calls compiler analysis with a closed source batch and retains the
result's source and semantic owners.

## Inputs and ownership

`WorkspaceAnalysisHost::update(document, version, text)` owns source bytes. Equal
or older versions are rejected. Identical bytes advance the version and reuse the
content owner. Removing a document permits reopening with a new version sequence.
Document keys may be URIs or untitled buffers; the host performs no filesystem I/O.

`WorkspaceProjectModule` maps document keys to canonical module paths. The caller
supplies the closed project, including imported Carven modules. Duplicate document
or module mappings are compilation-input errors.

Snapshots retain their document versions and input owners. Query results retain
source locations, diagnostics, static output, and semantic IDs through their owning
analysis. Client versions are attached to each query separately from cached
content. Definition and reference locations carry the destination version.
Offsets and half-open ranges use UTF-8 bytes. Host updates and lazy queries must
be serialized; shared caches are not synchronized.

## Queries

| Query | Result |
| --- | --- |
| `syntax` | Source and diagnostics; `syntax()` exposes an error-free AST and `recovered_syntax()` exposes retained complete top-level items. |
| `document_symbols`, `workspace_symbols` | Parsed declarations, including recovered items. Workspace symbols contain top-level declarations. Names are not resolved. |
| `semantic` | Full analysis of the selected project, with diagnostics and captured static output. `program()` is available after publication succeeds. |
| `hover` | Types at recorded source tokens in the target document and its transitive imports. |
| `definition` | A resolved source declaration in the target document and its transitive imports. |
| `references` | Recorded occurrences with the same resolved declaration across the selected project, including its declaration. |

Hover and definition select the target document's transitive import closure. The
compiler import resolver supplies relative, domain-root, and craft paths, including
the `std` craft alias. Cycles are visited once. Every selected declaration, body,
static computation, and publication gate runs. Versions, diagnostics, and captured
output describe this selection. An unrelated project source does not enter the
query. A target outside the project returns no information or analysis owner,
performs no analysis, and has an empty version list. A mapped but missing source
is a compilation-input error. Reachable syntax and import errors are reported by
the compiler within the query selection. Semantic and reference queries analyze
the complete caller-selected project.

## Source observations

Semantic construction records source locations, resolved declarations, and types;
queries index these observations. Declaration selections come from catalog
symbols, local binding origins, and field metadata. Types attach to direct tokens
such as names, literals, operators, access markers, and control keywords. Shared
name and type resolution also records type annotations, module constant
initializers, closure captures, and named-construction field labels. Composite
values expose their types at binding names when they have no direct token anchor.

`SourceOccurrence` has a location, an optional definition, and an optional
`SourceType`. Published `TypeID` values belong to the returned program owner.
Failed analysis can retain known builtin types and definitions from completed
construction. Definition availability is independent of type availability.
Observation availability does not establish program validity; compilation requires
all publication checks. References report recorded occurrences in the analysis.

Declaration observations are admitted after the declaration and nominal checks.
Non-staged functions, closures, `const` blocks, and runtime and static tests
contribute observations after successful construction. A nested closure merges
into its parent transaction; a parent failure discards both. An unrelated body
failure does not remove completed observations. Parse and import errors prevent
semantic observation.

Bodies requiring static specialization and their nested closures are not
source-observed. Declaration annotations, constants, and type extents are observed
in their original declaration context. Specialized instances do not supply token
types for that source. Reference results follow these observation boundaries.

Syntax recovery discards failed top-level items and retains complete independent
items. Synchronization respects nested delimiters and interpolation boundaries.
Lexical errors, initial import errors, and delimiter preflight failures provide no
recovered tree. Recovery does not repair partial function bodies.

## Caching

Syntax and symbols are lazy per document. Equal symbol names and source ranges
reuse their derived result and workspace index. Navigation populates reachable
syntax caches while discovering imports.

Semantic selections use a canonical key containing module mappings and source
content owners. Each snapshot retains at most two distinct semantic results.
Query scope determines the selected inputs independently of cache contents.
Identical selections share their analysis, regardless of module order.

Changing or removing a selected document drops its cached result from the new
snapshot. Version-only updates reuse content analysis with current query versions.
Import edits and project remapping determine a new selection. Snapshots and returned
results retain their old sources, IDs, diagnostics, and output. Host updates copy
document maps and inherit valid cached results.

An uncached selection runs the full compiler pipeline, including parsing. Syntax
used for import discovery is retained for source queries and is parsed again by
compiler analysis. Dense import graphs can add discovery work without reducing the
selected analysis. Timing recipients are borrowed during the semantic call and
receive events only for a new computation.

## Validation

```shell
./xmakew build
./xmakew test -g workspace
```

See [benchmarks](benchmarks/README.md) for workload commands and measurement boundaries.
