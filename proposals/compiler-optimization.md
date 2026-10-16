# Compiler costs and C++ realization

- **Status:** Exploration
- **Scope:** Reducing compiler, workspace-reuse, and generated-program costs while preserving existing source contracts

## Summary

This proposal collects implementation experiments that preserve source behavior.
Each experiment identifies the work removed, the costs measured, and the evidence
needed for adoption.

| Area | Question | Evidence for selecting an experiment |
| --- | --- | --- |
| Compiler data and queries | Can ownership, preparation, or data organization avoid repeated work? | A reproducible time, memory, or scaling cost |
| Workspace reuse | Can smaller computations or longer-lived requests avoid repeated complete analysis? | Measured edit or request overlap under identical source coverage |
| Stage parallelism | Can independent work overlap within a bounded resource budget? | Sufficient independent work after scheduling and memory costs |
| C++ realization | Can a snapshot, reservation, or result carrier be avoided? | A concrete redundant generated operation with an equivalent alternative |
| Constant output | Can completed values use smaller target expressions? | Output growth in representative programs |
| Formatting and output | Can reservation or composition remove work? | A workload with preserved observation points and failure behavior |
| Local value facts | Can additional non-diagnostic facts remove runtime work? | An operation or formatting workload that benefits from precomputation |

Compiler wall time, CPU time, peak RSS, generated size, native compilation, and
program runtime and memory are measured separately.

## Context

Semantic analysis folds admitted constant operations, and static
specialization refreshes value facts and index bounds after substitution. Backend
preparation consumes published facts to select constant print text, known lane
paths, and builtin formatting writers; realization consumes proven index bounds.
Writers use checked minimum/maximum capacity
bounds and direct `std::to_chars` conversion; proven UTF-8 formatting avoids a
validation scan. Direct owning format returns can use a local String and NRVO.

## Design

### Data and query experiments

| Observed cost | Candidate | Obligations and additional costs |
| --- | --- | --- |
| Construction allocation or value movement | Change local ownership, capacity, or result delivery | Borrows, failure paths, destruction, and small-input overhead |
| Ownership relationships, outlives matrices, or state copying | Compact relationships or avoid unchanged state copies | Fixed-point convergence, diagnostic witnesses, aliases, joins, and queries |
| Preparation scans or summary lookups | Reserve capacity, use compact temporary indexes, or reuse local summaries | Index construction, address-to-ordinal mapping, destruction, and deep-tree recomputation |
| Token-to-AST literal copying | File-local storage or ownership transfer after successful parsing | Parser rollback, diagnostics, and independent source-snapshot lifetime |
| Module/import lookup growth | Index a measured linear query | Path identity, source order, construction, and publication costs |
| Loop RSS grows with cumulative object creation | Reclaim execution slots or prepare region-binding queries | Stale-coordinate rejection, generations, and cache lifetime |
| Wide records or fragmented representation | Separate hot fields, move rare payloads, or repack locally | Indirection, overlapping old/new storage, conversion, destruction, and origins |

Retain immutable ownership for shared source backing. Preserve exact token spans:
stateful interpolation boundaries cannot generally be recovered from the next
token's start. Prepared summaries belong to the correct body and completion
state, and cannot outlive their borrowed source.

Match execution preparation cost to reuse: one-shot roots and repeatedly called
bodies may justify different choices. Preserve output, diagnostics, budgets, and
required execution events when reusing preparation. Check native-operation
support on executed paths. Use generations or an equivalent stale-coordinate
check when reusing slots after Take, release, or reinitialization.

Grouping compatible passes around each body may improve data locality. Preserve
analysis dependencies and invalidation rules, and measure locality benefits
against scheduling overhead and retained memory.

Evaluate native-call diagnostic replay separately from call-free paths: extending
eligibility can reverse a gain on real native-contract inputs. For synchronous
binary-leaf evaluation, measure ordinary inputs, expression stress cases, and
added memory; weigh the selected workload's benefit against regressions elsewhere.
For parent-owned `OwnershipFlow` updated by child work, measure ordinary Release
CPU cost as well as result-transfer counts and Debug deep-expression behavior.

### Bounded stage parallelism

One candidate is independent tasks within a stage, synchronous join, and ordered
coordinator publication. Select an entry independently of the others:

| Entry | Candidate unit | Boundary to preserve |
| --- | --- | --- |
| Syntax construction | Per-file lex and parse | Fixed SourceID/module order, private AST and diagnostics, import closure after completion |
| Post-solve body completion | One owned body with shared global solutions | Reserved identities, no concurrent table append, completion and event order |
| Nullability | Private analyzer and sink per body | Publication gates and diagnostic set |
| Backend | Per-artifact lower, verify, and emit | Immutable plans and names, private lowering, stable plan-order results |

Artifacts may cover an SCC or a test entry. Compare stable bytes, paths, roles,
and dependencies across compilations; program-local IDs are not cross-compilation
identities. Establish independent write sets for construction, interning, static
execution, and fixed-point solving before parallelizing them. Distinct FunctionIDs
alone do not establish that independence. Keep unproven work serial. Preserve
source-error collection, failed-request reuse, ordered events, and the output
contract after a file failure.

#### Candidate execution protocol

This candidate is a bounded synchronous design. Names such as `ParallelExecutor`
and `map_ordered` are provisional.

- The compilation coordinator owns one executor and lends it to stages. A
  noncopyable, nonmovable owner with no hidden global pool is one candidate.
  Destruction requires no active call, wakes and joins parked workers, and cannot
  occur from a callback running on that executor.
- The supplied budget is at least one and includes the calling thread. At most
  budget-minus-one background workers are created as needed and reused. Nothing
  detaches. Normal return joins all work and releases input borrows.
- Results support move-only, non-default-constructible values and cannot borrow
  worker scratch. Each item owns its output and temporary state. Workers may
  borrow one const callback without copying move-only captures; the caller still
  proves mutable accesses do not conflict. Shared containers cannot resize,
  change capacity, or move their owners while borrowed by parallel work. Any
  additional concurrent mutation requires its own synchronization and lifetime
  proof.
- One active top-level region with same-executor nested calls running serially
  is a candidate deadlock-avoidance policy. Such callbacks do not wait on pending
  sibling or enclosing work. Cross-executor calls require a separate wait policy;
  the restricted candidate excludes them.
- Each index executes exactly once on normal completion. Results are returned in
  index order, with defined input publication and result visibility. Queue-empty
  is not completion; claiming, completion counts, joining, and repeated-region
  isolation need an explicit synchronization proof.
- Budget one executes indexes in ascending order. Empty and small ranges follow
  the same contract. Scheduling metadata is bounded by worker count; ordered
  result storage is O(count), without a future/promise per semantic node.
- Domain errors are results, not implicit fail-fast cancellation. The coordinator
  commits diagnostics, global identities, output, and artifacts. Process-fatal
  behavior and thread-creation failure under `noexcept` do not promise recovery.

The caller supplies resource policy; support does not read CPU count, environment
variables, or CLI options. Measure coexistence with multiple Xmake-launched
compiler processes. Workers return local timing records; the coordinator aggregates
them instead of invoking a synchronous recipient concurrently. Stage wall time,
cumulative task elapsed time, and CPU time remain separate.

Give source reads and SourceID registration separate ordering rules. Keep
SourceID assignment deterministic and preserve read-failure behavior. Keep
filesystem writes serial under their current failure contract; joining computation
is not a filesystem transaction. Build integration owns staged publication and
native subprocess scheduling.

### C++ storage and delivery

Identify the source lifetime and control duties of the generated operation, then
compare alternative realizations under the selected cost measures.

#### Storage observations

A more local stability proof may avoid snapshots that a body-wide
mutation/access/exposure classification retains. Compare the extra scan, summary
space, and query cost with removed work. A stable binding, selected storage, and
backing are different facts: a stable slice descriptor does not stabilize its
elements, nor does a stable pointer slot stabilize its pointee. Preserve Write
iteration aliases, captures, native references, address exposure, and the type's
Read snapshot policy. Check Read followed by Write, projections, captures, and
native retention at their original observation points.

#### Intermediate storage and failure delivery

For each removable reservation, state its source lifetime and C++ scope duties.
Initialization remains on the selected path; construction and reverse cleanup
order remain observable. Backing precedes a result borrowing it. Native Read or
Take can retain even a scalar reference. Distinguish direct construction of an
immovable prvalue from transferring a stored component across a failure barrier.

Pattern checks, binding-source selection, and failure joins have different roles.
Remaining-domain acceptance does not remove guards, dynamic bounds, or selection
between alternative binding sources. Owning bindings remain snapshots with cleanup.
Broader direct failure delivery must account for handoff count, protected-region
loops, payload snapshots, and cleanup of the entire carrier. Guard mutation must
not retroactively change a thrown snapshot.

Check protected cleanup before handlers, rejected guards, handler failures,
rethrow, return, loop transfers, test stops, and skipping later operands after
failure. Storage and failure lifetime tests, together with target-structure tests,
check observable behavior and target structure.

### Constant output and local facts

#### Compact default arrays

Frozen `ArrayConstant` lowering reconstructs elements individually. A candidate
emits `std::array<T, N>{}` when value-initialization of the selected C++
representation produces the completed typed value with the same construction,
ownership, backing, and observable behavior. Establish eligibility from the final
value and target construction rules, including element types of empty arrays.
Numerical equality alone is insufficient for signed floating zero or a type with
native construction effects. When equivalence is unproven, retain explicit
element reconstruction.

Preserve canonical identity, evaluation budgets, and freezing. Avoid repeated
scans of nested fallback paths. Test empty and nested arrays, non-default values,
representation-sensitive values, construction obligations, indexing, and
independent mutation after copying. At fixed depth, require initializer-node count
to remain bounded as eligible element count grows. Measure scanning, extent
spelling, target nodes, bytes, generation, and native compilation separately.

#### Broader local value propagation

Static-parameter substitution refreshes non-diagnostic expression facts.
Further propagation through ordinary local bindings, mutation, branches, or loops
needs a workload with residual runtime work and explicit invalidation and join
rules. Use existing folding and known-result consumers. Unknown, invalid, or
over-budget operations retain runtime execution. Keep ordinary-function and
floating precomputation within their existing admission rules.

Preserve SemIR known-result boundaries: facts do not change ordinary source
reachability, failure, ownership, or return analysis. Storage stability does not
establish a known value. Test mutation,
aliases, calls, loops, and joins. `touch() && false` still executes touch;
`false && touch()` does not. A known slice length alone does not prove a dynamic
index safe; a known in-range index and extent already allow an unchecked path.
Preserve failure, storage observation, borrowed owners, and condition cleanup.

### Workspace reuse

This section owns reuse measurements and build integration. Frontend analysis
owns partial semantic facts, query dependencies, cancellation, effects, admission,
and worker isolation. Measure source selection, computation granularity, and
result lifetime separately.

#### Existing reuse and remaining costs

The compiler publishes checked units containing an executable program,
cacheability, and optional checked definitions. Standard-library producers retain
definitions for generic and staged instantiation in later source analysis.
Generation-only consumers retain the executable program. Artifact directories
carry these results across processes; generation from a retained program repeats
neither source analysis nor static execution.

Xmake shares standard-library analysis and baseline generation across native
configurations, selects native archives by compilation settings, and shares
analysis for identical source batches. Ordinary and test generation consume the
same checked program. Targets with their own source preparation retain separate
batches. Build integration owns dependency freshness and staged publication.

Analysis records observed host input, time, or waiting as noncacheable; build
integration repeats that producer. Producer roots execute during producer
analysis; imported definitions reuse those completed results. Consumer analysis
executes its own roots and records host observations made by its calls. Preserve
this producer/consumer boundary in reuse experiments.

Workspace retains immutable documents and complete-selection results in memory.
Unchanged documents reuse parsing. A content edit requires a complete semantic
transaction for an uncached selection; overlapping selections do not share
individual semantic queries. Bulk updates copy the document table once;
individual updates copy it per revision. Navigation builds selection indexes and
prerequisite closures. Retained snapshots can keep results beyond cache eviction.

Measure artifact encoding, decoding, retained definitions, source analysis, and
native work separately. Select further reuse only for work that remains after
these existing paths.

#### Candidate experiments

| Measured cost | Candidate | Required boundary |
| --- | --- | --- |
| Repeated navigation rebuilds the same closure | Snapshot-owned module/prerequisite indexes | Imports, missing lookups, provided contracts, duration prerequisites, remapping, and revisions invalidate the correct selection |
| Document-table copying remains costly after bulk updates | Persistent document maps | Atomic publication and independently retained old snapshots |
| Body edits repeat unrelated semantics | Scope, declaration, body, or domain-specific SCC queries | Track dependency reads, snapshot-owned identities, effects, and complete-publication requirements |
| Repeated artifact loading dominates compatible generation requests | Worker retaining decoded programs | Preserve generation results, source ownership, freshness, and bounded retention; compare with artifact-directory inputs |
| Completed semantic queries repeat across process lifetimes | Persistent query cache | Requires owned query results, stable identities, dependency validation, atomic publication, and measured benefit over complete artifacts |

Begin with conservative dependencies covering inferred contracts, generic
specialization, constants, and ownership across source and library boundaries.
Track failed lookups and competing candidates as well as selected declarations.

#### Request lifetime

Use the long-lived workspace host for editor reuse experiments. Compare a build
worker with the existing artifact-directory path, including encoding, decoding,
startup, IPC, recovery, retention, and validation costs. Select its lifetime from
measured request overlap within or across builds.

A selected worker owns bounded compiler results. Tag requests and results with
input revisions and a worker generation; discard replies from failed generations.
Keep restart and request failure reporting outside the worker. Xmake retains
target evaluation, native compilation, and job scheduling. Preserve distinct
semantic inputs and source-preparation boundaries.

#### Evidence and decision gates

Use cold compilation, same-content rewrite, leaf-body edits, exported-contract
edits, const/generic edits, and import addition/removal workloads. Add an in-process
trace for bulk loading, repeated navigation, overlapping selections, and compatible
generation requests. Include artifact-only regeneration, standard-definition
imports, ordinary/test consumers, and noncacheable host-effect producers. Record
wall/CPU time, parsing, semantics, serialization, peak and retained memory, and
native work separately. Alternate fresh/reused Release runs with identical
coverage; measure Debug editor latency separately. Report unchanged-query and
edited-source results as separate workloads.

Select an acceptable cold-time/memory budget and required edit-latency improvement
before experimenting. Savings must exceed lookup, hashing, retention, and transport
costs. Measure semantic throughput and request overlap directly. Use fresh
analysis under the same producer inputs and execution policy as the correctness
oracle.

Compare diagnostics, static-root execution and output, paths/bytes, and source
locations under each command's contract. Artifact-only generation must not replay
producer roots. Source edits must execute the newly selected batch's roots and
tests. Cover missing names, local aliases, provided prerequisites, recursive
contracts, and const/generic dependencies. Use existing Xmake scenarios for native
behavior. Keep cache sizes, pointer identities, query visitation order, and pass
counts out of behavioral assertions. Measure generated-program speed separately.

### Formatting and output

#### Destination capacity planning

Select a reservation experiment when a representative workload exceeds existing
writer bounds. Exact length, an upper bound, and minimum field width are distinct
facts. Define length arithmetic, formatter invocation count, and resource
observations. Compare fresh and reused destinations: an upper bound can reserve
unused memory, while computing the exact size adds work. Explicit operation
resource constraints take precedence over growth heuristics.

#### Composition across output boundaries

A formatting/output experiment must preserve operand completion, String
observation, and visible failure prefixes. For example:

```carven
println(f"{text}", mutate(&text));
```

The owning interpolation completes before the later argument mutates `text`.
Within one interpolation, String holes observe their owners after all holes
evaluate. Composition must retain both observation points and temporary backing.

Streaming segments can expose a prefix before failure during String construction;
batching print values can suppress an already observable prefix. Define the
preserved construction, conversion, and allocation-failure behavior before
selecting fusion. Measure the complete operation, including effects and aliases.

#### Conversion runtime replacement

A replacement needs a concrete byte-output and failure contract and representative
cost evidence. Cover the selected target libraries, Unicode width and precision,
locale, floating presentation, custom formatters, and invalid dynamic widths.
Optional precomputation retains runtime evaluation for valid specifications it
cannot evaluate. Preserve runtime failure timing, diagnostics, the supported
data model, and floating-point constant-fact boundaries.

Measure generated source, native compilation, runtime, and memory independently.
A native formatting kernel alone does not establish an application throughput gain.

## Validation

### Measurements

Define the measured boundary and preserve results, effects, diagnostics, and
output contracts across variants. Use actual programs and a minimal reproduction,
adding deep or wide expressions, skewed file sizes, mutable state, constant loops,
or error recovery when they exercise the affected cost.

Use the repository benchmark commands:

```sh
./xmakew bench compile --list
./xmakew bench compile --samples=5 --warmups=1 --timings --output=build/benchmarks/compile.json
./xmakew bench incremental --samples=5 --warmups=1 --timings --output=build/benchmarks/incremental.json
```

These use the current configuration. Record the source revision, local changes,
toolchain, mode, flags, and recoverable inputs. Report Release results for
throughput decisions; Debug development costs can be measured separately.
External `--compiler` artifacts need their own revision and mode records.
`compile` excludes native compilation; `incremental` includes Xmake and native
build work. Neither directly measures target-program speed.

The `async`, `simd`, and `json` topics measure generated program execution. They
calibrate iteration counts by duration and retain raw samples and input
identities. JSON measures the byte-validation API, including UTF-8 and failure
delivery, without constructing a DOM. Match the measured boundary to the proposed
optimization and inspect generated C++ as well as timings.

Select workloads with `--case=<id>`. JSON retains inputs, commands, and samples.
The displayed stage report is from the valid sample closest to median wall time,
not a set of stage medians. `<0.1 ms` is an upper bound; a missing report is not
zero cost. Compare identical timing settings, alternate baseline and variant runs,
and retain dispersion, adverse results, and independent peak-memory observations.
Do not infer allocation counts or cache behavior from timings alone.

### Checks and stopping conditions

Select source, structural, diagnostic, and native checks for the affected behavior.
Parallel experiments compare budgets 1/2/4 and larger values when useful, including small, skewed,
failing, and multiprocess workloads. Verify join, lifetime, visibility, nesting,
exactly-once execution, ordered publication, and failure gates using controlled
synchronization and bounded harnesses. Sanitizers supplement these proofs.

Stop or revise a candidate when benefits remain within noise, occur only at
unrepresentative scales, cause unacceptable regressions on actual inputs, or
require disproportionate complexity.

## References

- [rustc incremental compilation](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation-in-detail.html): tracked dependencies and identity, persistence, and hashing costs.
