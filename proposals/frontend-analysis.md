# Recoverable frontend analysis and compilation control

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** Lossless damaged syntax, snapshot-owned partial semantics, causal diagnostics, query coordination, request cancellation, and analysis fault isolation

## Summary

Extend existing top-level syntax recovery and complete-selection workspace
analysis with lossless damaged syntax, independently owned partial types, and
tracked semantic queries. Preserve established facts and causal diagnostics when
other work fails, and distinguish source errors from interrupted requests.

Separate diagnostic recording, local recovery, and request coordination. Track
operand, type, body, and execution-result validity explicitly at their producing
operations. Use those facts for dependent work and artifact admission.

The design retains strict executable SemIR, source provenance, immutable snapshots,
explicit failure propagation, and the shared semantic executor. It introduces
an independently owned partial analysis model and a process boundary for a
long-lived language server.

```text
Immutable source inputs
  -> lossless recovering syntax
  -> snapshot-owned semantic queries -> partial editor facts and diagnostics
  -> complete requested analysis and execution obligations
  -> compilation admission -> SemIRProgram -> backend or interpreter

Request coordinator: scope, priorities, budgets, cancellation, result delivery
LSP supervisor: document revisions, protocol, analysis-worker restart
```

## Context

- `diagnostics/sink` stores diagnostics without printing or stopping.
  `Diagnostic` has a code, severity, message, source labels, notes, and helps.
- `lex` returns tokens and diagnostics. `Parser::fail` records a diagnostic and
  sets parser failure state. Recovery retains complete top-level items, but
  delimiter preflight and malformed initial imports can discard the entire tree.
- `parse_program` visits independent files but requires error-free syntax and a
  closed import graph before producing `SyntaxProgram`. Workspace parsing skips
  syntax analysis when lexing reports errors.
- Ordinary function, test, and const-block construction can continue after an
  independent body fails. Failed inferred contracts propagate without repeating
  their originating diagnostic. Declaration, construction, and publication
  gates still stop dependent work.
- Source observations depend on admission and successful body construction.
  Failed publication can retain definitions and builtin types, but general
  `TypeID` observations require a published program owner.
- Workspace retains revisions and complete selection results. An uncached
  semantic selection runs the full compiler pipeline. The current continuation
  loop drives dependencies synchronously; it is not a language-server request
  scheduler or an incremental query database.
- Both internal invariant failures and low-level capacity exhaustion can abort.
  Semantic execution has work limits, but source-language task cancellation is
  distinct from cancellation of a compiler or editor request.

`SourceDocument`, `ParsedDocument`, and `CompilationAnalysis` provide
shared immutable source/syntax and an owned complete analysis result, including
failed-source diagnostics, optional source observations, and captured static
output. Workspace bulk edits publish atomically; unchanged content reuses parsing,
and bounded complete-selection caches retain results across version-only edits.
Navigation records local import aliases and follows provided-contract and duration
prerequisites as well as explicit imports. Workspace requests deny host input,
clock sampling, and waits; reaching one currently produces an admission diagnostic.

Checked units retain executable programs and, when requested, completed
definitions for later generic and staged instantiation. Artifact-only generation
reuses the published program without static execution. Producer roots execute
during producer analysis; importing definitions reuses those results. Consumer
source analysis executes its own roots. These owners require successful
publication. Partial editor facts still need an owner
that survives failed publication.

Analysis marks observed host input, time, or waiting as noncacheable for build
integration. This records whole-producer reuse eligibility. Per-query effects,
dependencies, interruption, and event delivery remain part of this design.

The remaining work is lossless damaged syntax, types owned independently of
program publication, per-query semantic invalidation, request cancellation, and
worker isolation. Measure further reuse against complete-selection caching and
checked-artifact generation under the same source coverage and execution policy.

### Research basis

Use the following mechanisms as design inputs. Validate recovery and publication
against Carven's ownership, generics, and static-execution rules.

| Source | Established result or practice | Application and limit |
| --- | --- | --- |
| rust-analyzer architecture | Broken source is normal input; analyses return values with errors; the analysis core avoids external IO. Request panic boundaries and separate procedural-macro processes serve different failure cases. | Make source results available independently of diagnostics. Rust unwinding is not a recovery mechanism for C++ `abort` or corrupted memory. |
| Clang Recovery AST | Recovery expressions preserve children and source locations while normal AST nodes retain stronger invariants; erroneous ASTs do not enter code generation. | Use explicit recovery structures and preserve strict executable representations. |
| rustc queries and `ErrorGuaranteed` | Demand-driven queries record dependencies; an error token witnesses an already reported error. | Track causal failures and dependencies. A Carven program-admission token has a different meaning from an error-reporting token. |
| TypeScript language service and 5.6 diagnostics | Syntax, semantic diagnostics, and emit are separate services; diagnostics can prioritize a requested region. | Request scope and diagnostic coverage must be explicit. A region result is not a complete project check. |
| CPCT+, ECOOP 2020 | Minimum-cost repair sets reduce cascading errors in the evaluated LR parser workload. | Use bounded repairs and measure cascades; its LR machinery is not a drop-in replacement for Carven's handwritten parser. |
| Total Type Error Localization and Recovery with Holes, POPL 2024 | A mechanized marked calculus gives erroneous expressions a defined type and binding structure using holes and provenance. | Model uncertainty explicitly. Do not infer that adding one universal error type proves Carven recovery sound. |
| Incremental Type-Checking for Free, OOPSLA 2022 | Scope-graph query confirmation supports incremental checking with dynamic and mutual dependencies in Statix. | Track name-resolution dependencies, including unsuccessful lookups. Adopting the entire Statix framework is not required. |
| Build Systems à la Carte, ICFP 2018 | Scheduling and rebuilding can be separated in a model of dependency-driven computation. | Separate request planning from cache validation; this does not specify compiler error recovery. |
| Type error debugging in Hazel, Cambridge technical report, July 2026 | Type slices explain contributing source; the report also explores cast slices and dynamic witnesses. It is a technical report based on a 2025 undergraduate dissertation. | Retain causal evidence for explanations. Dynamic witness search is outside this proposal; the report is not evidence of production maturity. |

## Design

### Ownership and delivery boundaries

Compiler-owned syntax, partial facts, query dependencies, and admission preserve
one set of language rules. Existing builtin contracts participate in these facts;
query effects and request cancellation are compiler-service contracts rather
than new source-level effect forms. The compiler host owns coordination and
worker isolation. Runtime Craft continues to supply generated-program support;
LSP supervision and query storage belong to the analysis service. Standard Craft
build declarations supply project selections consumed by that service.

Deliver recovering syntax and snapshot-owned partial observations using the
smallest required query boundaries. Fine-grained reuse consumes established
query results and dependency records; persistent caching and parallel scheduling
require measurements after those contracts work. Required-value admission is
needed wherever partial analysis must execute a constant dependency. Effectful
query reuse and worker integration have their own completion evidence and do
not gate useful syntax recovery or independent ordinary observations.

### Observable behavior

Within admitted input and resource limits, every completed parse returns a root
covering the source, including malformed text. An unfinished function does not
erase preceding declarations. A malformed import retains its source and does
not prevent indexing local declarations. Recovery does not promise to infer the
intended nesting of arbitrarily ambiguous text.

For `let x: i32 = missing;`, an editor can show the declared type of `x` while
reporting the unresolved initializer. The initializer has no valid value, and
the declaration remains invalid for execution. A later independent statement
can still produce its own diagnostic. A failed inferred return type blocks
dependents that need that type without duplicating its original error.

An edit that adds a missing name or an overload invalidates affected lookups,
including previously unsuccessful ones. Results always identify the snapshot
they describe. Cancelled, timed-out, or crashed analysis never becomes a clean
diagnostic result and never silently clears diagnostics for unexamined source.

Compilation rejects the selected source closure if required checks fail,
regardless of whether those diagnostics are hidden in an editor. An editor may
query independent facts in that same closure. Errors do not authorize execution
of repaired or fabricated source.

### Diagnostics and execution outcomes

Retain a recording-only sink. Recording returns no continue/stop decision and
does not mutate scheduler state. Structured findings contain a diagnostic code,
domain, default severity, typed arguments, locations, causal references, and
optional edits with applicability and source-revision guards. Human messages
are rendered from these facts; no control flow examines message text.

Separate source issues from request completion:

| Kind | Meaning | Handling |
| --- | --- | --- |
| Source issue | A source rule is violated or suspicious | Record it; recover locally or block the specific dependent fact |
| Request cancellation | The caller no longer wants this computation | Stop cooperatively; do not diagnose the program |
| Operational limit or input failure | Deadline, configured work/memory budget, unsupported input size, or unavailable input | Return an explicit request/input outcome; retain only independently completed results |
| Internal fault | An implementation invariant has failed | Terminate the analysis worker; report an internal compiler error outside it |

Classify each limit at its owner. Language-defined bounds may produce source
diagnostics; configurable deadlines and analysis fuel limits produce operational
outcomes. Classify existing `ConstLimit` sites individually.

Query completion is either a committed result or an interruption. A committed
result may describe invalid source and contain partial facts. Each fact has its
own availability: established, unresolved, or blocked by an identified cause.
An inferred metavariable, a syntactically missing expression, and a diagnosed
error are distinct states. A dependency not requested or interrupted belongs to
request coverage/completion metadata, not a reusable semantic fact.

Replace the current use of an emitted diagnostic as the universal
`AnalysisFailure` with explicit dependency causes and operation outcomes.
Failure evidence references its owning result and root source issue. Producing
a diagnostic and returning an unavailable value remain separate actions.
Diagnostic evidence is not permission to construct a missing semantic value.

### Recovering syntax

Use one lossless concrete syntax tree with typed syntax views for compiler,
formatter, and editor consumers. It owns source text and represents trivia,
invalid tokens, missing tokens, and error regions. Typed accessors distinguish
absent syntax from well-formed children. Normal semantic nodes are not used to
encode damaged syntax. Avoid retaining a second full AST with duplicate text
and topology; compact per-item analysis structures are derived as needed.

Keep a handwritten parser initially. Extend speculative checkpoints to a
transactional event/tree builder so alternatives can roll back nodes and
diagnostics without publishing guessed errors. Replace whole-file delimiter
rejection with local missing/unexpected delimiter recovery. Keep nesting and work
limits as explicit request outcomes, with the source snapshot available to the caller.

Define recovery boundaries for imports, parameter lists, type arguments,
expressions, statements, members, and top-level items. Prefer bounded insertion
of a missing token when the following token makes the boundary clear; otherwise
retain skipped tokens in an error region up to a grammar-specific synchronizer.
Every recovery action advances the cursor, exits a production, or spends a
bounded insertion allowance. Never repeatedly insert at the same parser state.

Delimiter and declaration context constrain synchronization. A nested function
must not be promoted to module scope merely because an enclosing statement is
invalid. Ambiguous nesting retains a damaged region rather than inventing an
authoritative module item. A recovery decision is metadata, not an automatic
source edit; quick fixes require their own applicability check.

The first implementation may reparse a whole file. Losslessness, recovery, and
immutable ownership are required before subtree reuse. Compiler and editor
consumers use the same recovering parse result; compilation admission requires
valid syntax. Formatting must preserve or decline damaged regions;
it must never print recovery insertions as user-authored text implicitly.

### Partial semantic analysis

Introduce an immutable `AnalysisSnapshot` owning source, declaration identities,
scope information, partial types, source facts, and query results. Its lifetime
does not depend on successful `SemIRProgram` publication. Query-facing type
identities are owned here and distinct from final SemIR identities.

Index declarations independently of body checking. Split explicit signature
facts from inferred signature dependencies. An invalid body need not invalidate
a well-formed declared parameter or return type. An inferred contract is
unavailable until its required body constraints are established.

Imports produce resolved, unresolved, ambiguous, or syntactically missing edges.
An invalid edge blocks facts that rely on it; it does not discard unrelated
local syntax. However, a damaged import or scope can hide competing candidates:
record scope completeness and return uncertain resolution when uniqueness cannot
be established. Do not claim a definitive binding from a guessed recovery.

Recovery expressions retain source children, an expected type when available,
independently established actual-type facts, and causal uncertainty. Expected
types do not become inferred actual types merely to keep analysis running.
Name resolution and safe child checks can proceed even if the enclosing
operation cannot be typed. Suppress a derived diagnostic only when its necessary
evidence is unavailable because of an existing cause; still check independent
operands. Constraints record origins so errors can explain the relevant chain.

Ownership, failure-set solving, and nullability keep their own prerequisites.
An unknown operation cannot be treated as effect-free, loan-free, returning, or
non-throwing. Initially block the affected body's dependent analyses when those
facts are missing, while retaining its name/type observations and checking
independent bodies. More local continuation requires a conservative transfer
rule for each analysis domain and dedicated evidence before adoption.

Start formalization with name lookup, explicit annotations, calls, and local
bindings. Specify which facts remain established when a child is missing or
erroneous. For the covered fragment, prove that recovery preserves valid evidence
for each publication prerequisite. State the source forms covered by that proof.

Do not add `ErrorType` or recovery operations to executable SemIR. Publication
assembles complete analyzed bodies into the existing strict representation,
with a checked mapping from snapshot identities to program-owned identities.
No query exposes a borrow into mutable construction state or a consumed draft.

### Query ownership and incremental computation

The semantic core exposes queries for file syntax, item indexes, module scopes,
declared signatures, inferred contracts, body analysis, required constants, and
validation summaries. Hover, definition, references, and diagnostics compose
these queries. CLI full checking requests the complete selected source closure;
interactive requests ask only for the facts they need.

Each committed query owns its value, diagnostics, causal evidence, and dependency
record. It observes an immutable snapshot plus explicit semantic configuration,
toolchain inputs, and evaluation mode. External file loading occurs before the
query boundary. Runtime budgets and scheduling priority do not change the
meaning of a completed semantic result.

Only snapshot-derived absence or failure may become a cached unresolved or
blocked fact. Cancellation, exhausted operational budgets, and unrequested work
must not become semantic `Blocked` cache entries. A query requiring an
interrupted dependency is itself interrupted; a request can still return facts
from independent completed queries with explicit coverage. Raising the budget
or issuing a fuller request must not reuse an earlier interruption as an answer.

Cache completed invalid-source results as well as valid ones. A cancelled query
does not commit partial construction or replace its previous completed result.
Completed child queries may survive independently. A crash discards the worker's
in-memory state, including otherwise reusable caches.

Record reads of scope candidate sets and negative lookup results, not just the
chosen declaration. Begin with conservative file, scope, and item dependencies.
Refine invalidation only after incremental results agree with fresh analysis.
Comparison of semantic query outputs can stop propagation when a dependency
changes text but preserves its relevant facts; location/provenance changes have
separate revision-aware handling. Do not reuse old byte offsets unchanged.

Do not equate every dependency cycle with an error. Recursive declarations use
reservations; recursive inference and effects use explicit strongly connected
components and domain-specific solving. Only cycles forbidden by a source rule
produce source diagnostics. An unexpected scheduler cycle is an internal fault.
A parallel scheduler must detect or prevent cycles in cross-query waits;
per-query completion state alone does not establish that waiting can make progress.

Query cache keys use document/module identity, an owner-relative item identity,
and tracked inputs. Snapshot-local node numbers are not cross-edit identities.
When declaration matching is ambiguous after an edit, invalidate conservatively.
Dependencies and source mappings retain owners across eviction; publication
never reinterprets IDs from another snapshot or worker generation.

Diagnostic aggregation follows the requested query closure, deduplicates by
producer and local issue identity, and orders by source location and stable
tie-breakers. Reusing a query includes its diagnostics without re-emitting them
into a global append-only sink. Results include coverage and revision metadata.
Only a complete replacement for the same coverage may clear previous findings;
an interrupted region request does not imply that remaining regions are clean.

### Compilation coordination and publication

Extend `compiler/` with a request coordinator above the semantic query layer.
It selects scope, validation goals, evaluation goals, priority, cancellation,
and budgets. Local recovery and data invariants remain with the producing
operation. There is no centralized switch over every diagnostic code to decide
how to repair syntax or construct a missing type.

Compilation admission requires all of the following for the selected closure:

1. Inputs, syntax, and required import resolution are complete and valid.
2. Declarations, bodies, types, failure sets, and required constants are complete.
3. All mandatory semantic, ownership, nullability, async, and publication checks
   have completed successfully.
4. No source-invalidity evidence remains, and the command's warning policy passes.
5. The results belong to the same admitted snapshot and semantic configuration.

Deliver a `SemIRProgram` through its checked publication path. Require completed
semantic evidence and internal representation verification. Apply warning policy
at the command boundary; diagnostic display settings leave semantic validity
unchanged. Retained checked artifacts keep this successful-publication boundary.

The partial model and the published program share semantic rules and construction
algorithms, not two independently implemented type checkers. Publication checks
the completion of the query products and retains the existing downstream IR
contracts. Backend preparation and realization need no recovery semantics.

### Static evaluation and observable effects

Syntactic parsing performs no semantic execution. Semantic analysis still needs
evaluation for array extents, constants, and static specialization. Those
dependencies cannot all be postponed until after analysis.

Internal required-value execution has a separate admission boundary for an
evaluation-ready root and each dependency it requests. It checks the types,
operations, and execution contracts required by that root without demanding
whole-program publication. Dynamically requested bodies must pass the same
admission before execution; recursive requests retain cycle and work guards.
This capability never admits partial/error operations and is distinct from
the capability to execute a published user program.

Separate required-value queries from requested execution of static tests and
other roots. Interactive requests execute only the selected roots and their
dependencies. An evaluation rejected by a source rule can block its dependent fact.
An evaluation not requested or interrupted leaves that request's coverage incomplete
and does not commit a reusable semantic answer. Neither case substitutes a
value. Source check/build requests the selected batch's mandatory roots.
Artifact-only generation reuses completed analysis without replaying roots;
importing checked definitions retains the producer/consumer execution boundary.

Reusable evaluation must account for inputs, configuration, dependencies, and
effects. Queries do not write to terminal callbacks. Required evaluation captures
observable events in an owned result; command coordination determines ordered
delivery. Roots whose output, execution count, or resource accounting would be
changed by memoization remain non-memoized execution requests. Interactive
results cannot silently satisfy a full execution obligation.

Cache value-only evaluation only when its dependencies and permitted observations
are explicit. Reuse must not bypass per-root limits; retain sufficient work
accounting or re-execute under that root. Existing synchronous output consumers
move to the coordinator boundary. Define event ordering and exactly-once
delivery across requested roots within a command before enabling query-level
effect caching.

### Fault isolation and the role of abort

Terminate the analysis worker on an internal invariant failure. Report a compiler
fault outside that worker and retain the triggering input when available.
Malformed source reaching an invariant failure is a compiler defect.

The containment unit for language-server analysis is a persistent worker
process. The LSP supervisor owns protocol state, document revisions, cancellation,
and restart policy. The worker owns parsing, analysis storage, and caches. On
crash, the supervisor reports the failed request, abandons that worker generation,
restarts with bounded backoff, and replays current document inputs. Repeatedly
crashing identical work is not retried indefinitely. Existing results may only
be shown with their original revision; no stale result is relabeled current.

Start with one worker per workspace session. IPC
carries versioned inputs and owned query responses; it never exports pointers
or assumes that worker-local IDs survive restart. Old-generation replies are
discarded. A parent-enforced deadline can terminate an unresponsive worker when
cooperative cancellation fails. Measure memory duplication and startup cost.

The worker receives cancellation through a control path that remains serviceable
while an analysis query is running, such as an IPC reader updating a request
token. Poll the token at parser work intervals, continuation-loop steps, solver
worklists, and static-executor steps. Cancellation of the host request does not
execute Carven cleanup code or become a language-level cancellation value.
Discard unfinished reservations and construction arenas before another query
can observe them. A wall-clock deadline is a fallback, not a replacement for
bounded work and explicit cancellation checks.

A one-shot CLI can serve as the compilation worker and terminate nonzero on an
internal fault. The invoking build integration owns staging and publication:
generate into a private destination and publish only after success. Direct CLI
output retains its documented partial-write behavior. Capture compiler version,
stage/query identity, and source identity outside the failed worker when available.
Keep fatal-handler work bounded and independent of damaged analysis storage.

Use `noexcept` and explicit results for handled outcomes. Check predictable
source-size, node-count, and work-budget limits before low-level exhaustion and
return operational outcomes. Terminate the worker on unrecoverable host failure.
Do not use exceptions, signal handlers, or non-local jumps as recovery from abort,
corrupted state, undefined behavior, or allocation failure. The supervisor owns
restart and request failure reporting. Process isolation contains worker failure;
security sandboxing and system-wide resource limits require separate mechanisms.

## Open decisions

### OPEN-01 — Recovery quality and tree representation

- **Status:** Active
- **Question:** Which grammar-specific recovery sets and immutable node layout
  preserve the most useful structure at acceptable memory cost?
- **Closure condition:** Compare lossless recovery on representative edit traces,
  parser speculation cases, and malformed-input fuzzing. Choose repair bounds and
  storage layout from those results; do not replace the parser with LR or PEG
  solely to adopt a published repair algorithm.

### OPEN-02 — Semantic unit boundaries and cyclic queries

- **Status:** Active
- **Question:** Which inferred contracts, generic formations, and static roots
  must be solved together to avoid premature publication or dependency cycles?
- **Closure condition:** Specify query contracts and strongly connected component
  boundaries using existing recursive/generic/constant fixtures. Demonstrate
  conservative invalidation before enabling fine-grained value-based reuse.

### OPEN-03 — Execution event ownership

- **Status:** Active
- **Question:** Which static roots are observationally reusable, and which must
  execute once per command regardless of cached value availability?
- **Closure condition:** Classify current output-producing roots and define event
  order, accounting, and observation semantics. Until then, effectful execution
  remains uncached; it must not block introduction of syntax and ordinary queries.

## Deferred work

Full typed-hole metatheory for Carven, execution through holes, SMT-based error
localization, and dynamic error-witness search are separate projects. Reconsider
them when ordinary recovery and causal evidence demonstrate a specific gap.
Carven's ownership and effect rules require domain-specific proofs; results for
a gradual functional calculus do not automatically transfer.

Parallel and persistent query engines require measured pressure after correct
in-memory incrementality. Subtree parsing requires evidence that whole-file
parsing dominates editor latency. These optimizations must preserve the same
fresh-analysis, revision, and effect-accounting contracts.

## Implementation

| Slice | Changes and owners | Completion evidence |
| --- | --- | --- |
| 1. Result contracts | `diagnostics`, compiler request outcomes, causal issue identity, budgets, and cancellation; stop using diagnostic presence as universal control state | Source failure, interruption, and internal faults have distinct tests; warning policy cannot authorize invalid source |
| 2. Recovering syntax | `frontend/lex`, parser/tree storage, typed views, workspace syntax, formatter and dump consumers | Lossless malformed-input trees, bounded recovery, preserved independent items, no invented top-level nesting |
| 3. Partial analysis | Catalog/scopes, declaration and body results, snapshot-owned types and observations; explicit prerequisites for later analyses | Useful facts survive a failed statement/body/import without entering executable SemIR |
| 4. Query database | First run queries without reuse, then add conservative dependencies and atomic memoization; adapt workspace | Fresh and reused analysis agree across edit sequences, including failed lookups and recursive dependencies |
| 5. Admission and execution | Coordinator, strict publication, required-value queries, execution goals and output ownership | Whole-closure admission, static effects and limits, code generation and interpretation remain well-defined |
| 6. Service isolation | Persistent analysis worker, supervisor, versioned IPC, cooperative cancellation and restart | Injected worker faults and deadlines leave the supervisor responsive and do not publish stale/partial results |

Worker protocol and lifecycle design can proceed after slice 1; full service
integration consumes the query interface. Subtree parsing, persistent on-disk
query caches, and parallel query execution are later optimizations, not
prerequisites for useful recovery.

## Validation

The implementation must establish the following properties with fixtures,
property tests, fault injection, and measurements at the owning boundaries:

- **Source preservation:** Concatenating real CST tokens reproduces the input,
  including trivia and invalid bytes admitted by the source layer. Missing tokens
  have zero source width. Tree spans remain inside their source owner.
- **Recovery progress:** Truncated files, damaged imports, invalid characters,
  nested interpolation, and missing/mismatched delimiters terminate under a
  bounded budget. Nested declarations never escape by accidental synchronization.
- **Independent evidence:** Broken initialization retains an explicit declared
  type; failed inference blocks only consumers of that inference. Independent
  name errors are reported; causal cascades are suppressed without hiding them.
- **Conservative semantics:** Unknown calls do not acquire invented effects,
  loans, values, or completion facts. Missing imports cannot establish false
  uniqueness. No missing/error value reaches final SemIR or the executor.
- **Incremental equivalence:** A fresh run and a cached run on the same snapshot
  yield equal semantic facts and normalized diagnostics. Cover adding/removing
  overloads, negative lookups, imports, recursive declarations, generic instances,
  const dependencies, options, and edit sequences with ambiguous item matching.
- **Transactional publication:** Cancellation or an injected failure at every
  commit boundary publishes no partial query or program. Completed sibling
  queries remain usable where their ownership permits it.
- **Revision safety:** Snapshot retention, cache eviction, worker restart, and
  out-of-order replies never reuse invalid IDs or attach old spans to new text.
  Incomplete diagnostic coverage never clears unrelated findings.
- **Execution obligations:** Cold/warm queries preserve required constant values;
  full command roots preserve defined output order, execution obligations, and
  limit accounting. Interactive queries do not unexpectedly run unrelated tests.
- **Failure containment:** An injected abort, allocation failure, or stuck worker
  fails the request, keeps the supervisor responsive, and respects restart bounds.
  Source recovery fuzzing runs in isolated workers and preserves reproducers.

Establish a baseline on repository programs and reproducible edit traces before
setting latency budgets. Measure syntax and hover p50/p95, cancellation latency,
full-check throughput, cache hit rates, peak RSS, retained-snapshot memory, and
worker startup/IPC cost separately. A faster partial result must not be compared
with a complete check without identifying their different coverage.

## References

### Production architecture

- [rust-analyzer architecture](https://rust-analyzer.github.io/book/contributing/architecture.html)
- [Clang error handling and Recovery AST](https://clang.llvm.org/docs/InternalsManual.html#error-handling)
- [rustc query system](https://rustc-dev-guide.rust-lang.org/query.html)
- [rustc ErrorGuaranteed](https://rustc-dev-guide.rust-lang.org/diagnostics/error-guaranteed.html)
- [TypeScript language service API](https://github.com/Microsoft/TypeScript/wiki/Using-the-Language-Service-API)
- [TypeScript 5.6 region-prioritized diagnostics](https://devblogs.microsoft.com/typescript/announcing-typescript-5-6/)
- [LLVM assertions and error handling](https://llvm.org/docs/CodingStandards.html#assert-liberally)
- [LLVM CrashRecoveryContext limitations](https://llvm.org/doxygen/classllvm_1_1CrashRecoveryContext.html)

### Research

- Diekmann and Tratt. [Don't Panic! Better, Fewer, Syntax Errors for LR Parsers](https://soft-dev.org/pubs/html/diekmann_tratt__dont_panic/). ECOOP 2020.
- Zhao et al. [Total Type Error Localization and Recovery with Holes](https://hazel.org/papers/marking-popl24.pdf). POPL 2024.
- Zwaan, van Antwerpen, and Visser. [Incremental Type-Checking for Free](https://aronzwaan.github.io/assets/oopsla22.pdf). OOPSLA 2022.
- Mokhov, Mitchell, and Peyton Jones. [Build Systems à la Carte](https://www.microsoft.com/en-us/research/publication/build-systems-la-carte/). ICFP 2018.
- Carroll. [Type error debugging in Hazel](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-1006.html). Cambridge technical report UCAM-CL-TR-1006, July 2026; based on a May 2025 dissertation.
