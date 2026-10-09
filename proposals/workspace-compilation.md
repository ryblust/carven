# Workspace compilation and reuse

- **Status:** Exploration
- **Implementation:** Not started for the experiments below
- **Scope:** Measuring further workspace reuse and choosing its granularity and lifetime

## Established boundaries

The shared source, parsing, analysis-result, and borrowed-generation contracts are
implemented and documented in the [compiler architecture](../docs/compiler/README.md)
and [workspace API](../tools/workspace/README.md). Workspace remains the analyzer
and LSP session layer. The compiler core owns reusable computation; Xmake owns
project targets, native compilation, and build scheduling.

Ordinary CLI calls retain automatic Crafts collection. Build integrations supply
exact source batches. Explicit `--tests=main|runner` selects runtime-test
generation; no mode is inferred from filenames or target kind. The artifact JSON
is a projection of `GeneratedArtifactSet`, shared with other output consumers.
Current invocation contracts belong in the [CLI reference](../docs/toolchain/cli.md).

Keep complete analysis of a selected source batch as the build correctness
boundary. Which source must be checked, how computation is divided, and how long
results are retained are independent choices. Complete coverage can eventually
reuse smaller computations. Smaller batches can still be recomputed in full.

The [frontend-analysis proposal](frontend-analysis.md) owns partial semantic
facts, query dependencies, cancellation, effects, admission, and worker isolation.
This proposal owns the performance evidence needed to choose further reuse and
its integration with build invocations. It does not introduce another semantic
query database. Editor recovery may independently justify changes to semantic
representation without requiring an incremental build cache at the same time.

## Remaining costs

Workspace now reuses parsing for unchanged documents, but an edited source in a
selected batch still causes a complete semantic transaction. Complete-selection
cache hits require the retained content and module mapping; they do not share
semantic work between different selections with overlapping libraries.

Taking a workspace snapshot is cheap. Bulk loading copies the document table
once, while independent single-document updates still copy it per revision.
Navigation still constructs selection indexes and import closures. Retained old
snapshots can hold results beyond the current selection cache's capacity. Measure
these costs separately before choosing another data structure or cache.

One-shot CLI invocations do not retain in-memory workspace results across target
requests or builds. A shared library interface creates a reuse opportunity only
for callers that keep its owners alive.

## Candidate experiments

| Measured cost | Candidate | Required boundary |
| --- | --- | --- |
| Repeated navigation spends time selecting the same closure | Snapshot-owned module/closure indexes | Imports, missing lookups, remapping, and document revisions invalidate the correct selection |
| Document-table copying remains costly after bulk updates | Persistent document maps | Atomic publication and independently retained old snapshots |
| Local body edits repeat unrelated semantic construction | Scope, declaration, body, or domain-specific SCC queries | Frontend-analysis dependency, identity, effect, and admission contracts |
| Compatible generation requests repeat analysis | A compiler-owned multi-request worker using shared analysis owners | Xmake still schedules targets; command execution obligations remain explicit |
| Separate processes repeatedly reconstruct valuable immutable results | Persistent compiler cache | Stable identities, compiler/configuration keys, dependency validation, atomic writes, and bounded IO |

Start with conservative dependencies. Do not treat a source file, an import SCC,
or a native library boundary as proof of semantic independence. Inferred
contracts, generic specialization, constants, and ownership relationships can
cross those boundaries. Name lookup includes failed lookups and competing
candidates, not only the chosen declaration.

Published SemIR is not a reusable standard-library image: it has released
construction data and static-only bodies, and its IDs belong to one program.
It also cannot be used to replay just the static roots of a later command.
Until the frontend's effect contracts authorize reuse, a new command performs
its complete semantic transaction, while retaining safe source/syntax reuse.

## Request lifetime

An editor already supplies a long-lived workspace host. Use it to evaluate
in-memory reuse before adding a process service. If a build worker is justified,
it accepts compiler requests and retains bounded source/analysis owners; it does
not evaluate Xmake targets, watch project trees, invoke native compilers, or own
a native job graph. Reuse the frontend proposal's worker isolation and lifecycle.

A worker alive only during one Xmake process can share that build's requests,
but cannot speed up a subsequent invocation after exiting. A persistent worker
or disk cache adds lifecycle or serialization costs. Include startup, IPC,
recovery, retained memory, and cache validation in the benefit calculation.
Do not merge semantically distinct targets just to increase cache hits.

## Validation and decision gates

Use existing benchmarks for cold compilation, same-content rewrite, a leaf-body
edit, an exported-contract edit, a const/generic edit, and import addition/removal.
Use an in-process workspace trace for bulk loading, repeated queries, overlapping
selections, and compatible generation requests. Record wall/CPU time, parsing
versus semantics, peak and retained memory, and native work separately. Compare
identical coverage and alternated fresh/reused runs in Release; Debug developer
latency is a separate result. A warm identical query is not an edit benchmark.

Before an experiment, choose an acceptable cold-time/memory budget and a required
edit-latency improvement for the representative workload. Adopt reuse only when
savings exceed lookup, hashing, retention, and transport costs. If parsing is a
small fraction of latency, do not describe parsing reuse as a solution to semantic
throughput. If compatible request overlap is rare, do not build a worker just to
keep a cache alive. Complete batch analysis remains the fresh-run oracle.

Validate a few real edit sequences by comparing fresh and reused diagnostics,
static output, generated paths/bytes, and source locations. Include adding a
previously missing name, recursive contracts, and const/generic dependencies.
Extend existing Xmake scenarios for native behavior. Do not assert internal cache
sizes, pointer identities, query visitation order, or fixed pass counts. Runtime
speed of generated programs requires separate execution measurements.

## References

- [Compiler architecture](../docs/compiler/README.md)
- [Workspace queries](../tools/workspace/README.md)
- [CLI and source inputs](../docs/toolchain/cli.md)
- [Frontend analysis](frontend-analysis.md)
- [Compiler optimization](compiler-optimization.md)
- [rustc incremental compilation](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation-in-detail.html): tracked dependencies and the additional identity, persistence, and hashing costs of reuse
