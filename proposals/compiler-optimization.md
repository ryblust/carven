# Compiler costs and C++ realization

- **Status:** Exploration
- **Implementation:** Not started for the candidates below
- **Scope:** Reducing compiler and generated-program costs while preserving existing source contracts

## Summary

This proposal collects implementation experiments that preserve source behavior.
Each experiment identifies the work removed, the costs measured, and the evidence
needed for adoption.

| Area | Question | Evidence for selecting an experiment |
| --- | --- | --- |
| Compiler data and queries | Can ownership, preparation, or data organization avoid repeated work? | A reproducible time, memory, or scaling cost |
| Stage parallelism | Can independent work overlap within a bounded resource budget? | Sufficient independent work after scheduling and memory costs |
| C++ realization | Can a snapshot, reservation, or result carrier be avoided? | A concrete redundant generated operation with an equivalent alternative |
| Constant output | Can completed values use smaller target expressions or shared storage? | Output growth or repeated materialization in representative programs |
| Local value facts | Can additional non-diagnostic facts remove runtime work? | An operation or formatting workload that benefits from precomputation |

Compiler wall time, CPU time, peak RSS, generated size, native compilation, and
program runtime and memory are measured separately.

## Measurements

Define the measured boundary and preserve results, effects, diagnostics, and
output contracts across variants. Use actual programs and a minimal reproduction,
adding deep or wide expressions, skewed file sizes, mutable state, constant loops,
or error recovery when they exercise the affected cost.

Use the repository benchmark commands:

```sh
./xmakew bench compile --list
./xmakew bench compile --samples=5 --warmups=1 --timings --output=build/bench/compile.json
./xmakew bench incremental --samples=5 --warmups=1 --timings --output=build/bench/incremental.json
```

These use the current configuration. Record the source revision, local changes,
toolchain, mode, flags, and recoverable inputs. Report Release results for
throughput decisions; Debug development costs can be measured separately.
External `--compiler` artifacts need their own revision and mode records.
`compile` excludes native compilation; `incremental` includes Xmake and native
build work. Neither directly measures target-program speed.

Select workloads with `--case=<id>`. JSON retains inputs, commands, and samples.
The displayed stage report is from the valid sample closest to median wall time,
not a set of stage medians. `<0.1 ms` is an upper bound; a missing report is not
zero cost. Compare identical timing settings, alternate baseline and variant runs,
and retain dispersion, adverse results, and independent peak-memory observations.
Do not infer allocation counts or cache behavior from timings alone.

## Data and query experiments

| Observed cost | Candidate | Obligations and additional costs |
| --- | --- | --- |
| Construction allocation or value movement | Change local ownership, capacity, or result delivery | Borrows, failure paths, destruction, and small-input overhead |
| Ownership relationships, outlives matrices, or state copying | Compact relationships or avoid unchanged state copies | Fixed-point convergence, diagnostic witnesses, aliases, joins, and queries |
| Preparation scans or summary lookups | Reserve capacity, use compact temporary indexes, or reuse local summaries | Index construction, address-to-ordinal mapping, destruction, and deep-tree recomputation |
| Token-to-AST literal copying | File-local storage or ownership transfer after successful parsing | Parser rollback, diagnostics, and independent source-snapshot lifetime |
| Module/import lookup growth | Index a measured linear query | Path identity, source order, construction, and publication costs |
| Loop RSS grows with cumulative object creation | Reclaim execution slots or prepare region-binding queries | Stale-coordinate rejection, generations, and cache lifetime |
| Wide records or fragmented representation | Separate hot fields, move rare payloads, or repack locally | Indirection, overlapping old/new storage, conversion, destruction, and origins |

Shared source backing needs an immutable owner; replacing ownership with a
`string_view` is insufficient. Token compaction must preserve exact spans:
stateful interpolation boundaries cannot generally be recovered from the next
token's start. Prepared summaries belong to the correct body and completion
state, and cannot outlive their borrowed source.

Match execution preparation cost to reuse: one-shot roots and repeatedly called
bodies may justify different choices. Preparation caches do not authorize result
memoization that skips output, diagnostics, budgets, or other required-execution
events. Preparing an entire body must not reject an unexecuted native branch in
interpretation. Slot reuse needs generations or an equivalent stale-coordinate
proof so Take, release, and reinitialization cannot revive prior pointers.

Evaluate native-call diagnostic replay separately from call-free paths: extending
eligibility can reverse a gain on real native-contract inputs. For synchronous
binary-leaf evaluation, measure ordinary inputs, expression stress cases, and
added memory; weigh the selected workload's benefit against regressions elsewhere.
For parent-owned `OwnershipFlow` updated by child work, measure ordinary Release
CPU cost as well as result-transfer counts and Debug deep-expression behavior.

## Bounded stage parallelism

One candidate is independent tasks within a stage, synchronous join, and ordered
coordinator publication. Select an entry independently of the others:

| Entry | Candidate unit | Boundary to preserve |
| --- | --- | --- |
| Syntax construction | Per-file lex and parse | Fixed SourceID/module order, private AST and diagnostics, import closure after completion |
| Post-solve body completion | One owned body with shared global solutions | Reserved identities, no concurrent table append, completion and event order |
| Nullability | Private analyzer and sink per body | Publication gates and diagnostic set |
| Backend | Per-artifact lower, verify, and emit | Immutable plans and names, private lowering, stable plan-order results |

Artifacts may cover an SCC or a test entry rather than one source module. Compare
stable bytes, paths, roles, and dependencies, not owner identity numbers from
different compilations. Distinct FunctionIDs do not establish independent write
sets for construction, interning, static execution, or fixed-point solving.
Unproven work remains serial. Preserve source-error collection, failed-request
reuse, ordered events, and the partial-output state after a file failure.

### Candidate execution protocol

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

Source reading and SourceID registration have different ordering responsibilities.
Keep writes serial unless their failure and output contracts are redesigned;
joining computation is not a filesystem transaction. Native subprocess builds
remain with the outer build system.

## C++ storage and delivery

Identify the source lifetime and control duties of the generated operation, then
compare alternative realizations under the selected cost measures.

### Storage observations

A more local stability proof may avoid snapshots that a body-wide
mutation/access/exposure classification retains. Compare the extra scan, summary
space, and query cost with removed work. A stable binding, selected storage, and
backing are different facts: a stable slice descriptor does not stabilize its
elements, nor does a stable pointer slot stabilize its pointee. Preserve Write
iteration aliases, captures, native references, address exposure, and the type's
Read snapshot policy. Check Read followed by Write, projections, captures, and
native retention at their original observation points.

### Intermediate storage and failure delivery

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

## Constant output and local facts

### Compact default arrays

Frozen `ArrayConstant` lowering reconstructs elements individually. A candidate
recognizes completed all-default fixed arrays and emits `std::array<T, N>{}` using
typed construction. Eligibility comes from final contents, not source syntax.
One bounded scope admits integer zero, false, zero char, and nested fixed arrays.
Check leaf types even for empty arrays. Floating values, enums, text, slices,
pointers, native types, structures, and slice backing remain outside this scope.

Preserve canonical identity, evaluation budgets, ownership, and freezing. Fall
back to element reconstruction for ineligible contents, avoiding repeated scans
of nested fallback paths. Test widths, empty/nested arrays, non-default fallback,
excluded types, indexing, and independent mutation after copying. At fixed depth,
initializer-node count should not grow with all-default element count; scanning
and extent spelling still have costs. Measure target nodes, bytes, generation,
and native compilation separately.

### Shared materialization

Repeated large values may justify shared target data only if owner independence,
Take, address observations, and cleanup survive. Prove C++20 static-initialization
eligibility for each reconstruction path, including payload-enum factories.
Use shared static storage only for paths proven to permit C++20 constant
initialization; retain current reconstruction for other paths.

### Non-diagnostic local precomputation

A use such as `let count = 4; return f"{count + 1}";` can motivate additional
facts when it removes meaningful runtime work. Define propagation, invalidation,
joins, and consumers. Source folding can diagnose; an optional non-diagnostic
analysis instead returns no fact for unknown, invalid, or over-budget operations
and retains the operation. It does not implicitly execute ordinary functions or
broaden floating precomputation.

Preserve SemIR known-result boundaries: facts do not change ordinary source
reachability, failure, ownership, or return analysis. Storage stability does not
establish a known value. Test mutation,
aliases, calls, loops, and joins. `touch() && false` still executes touch;
`false && touch()` does not. Known slice length does not remove bounds checks.
Preserve failure, storage observation, borrowed owners, and condition cleanup.

## Validation and stopping conditions

Select source, structural, diagnostic, and native checks for the affected behavior.
Parallel experiments compare budgets 1/2/4 and larger values when useful, including small, skewed,
failing, and multiprocess workloads. Verify join, lifetime, visibility, nesting,
exactly-once execution, ordered publication, and failure gates using controlled
synchronization and bounded harnesses. Sanitizers supplement these proofs.

Stop or revise a candidate when benefits remain within noise, occur only at
unrepresentative scales, cause unacceptable regressions on actual inputs, or
require disproportionate complexity.

## References

- [Benchmarks](../xmake/README.md#benchmarks) and [testing](../docs/development/testing.md): measurement and validation commands.
- [Realization](../docs/compiler/backend/realization.md) and [known results](../docs/compiler/analysis/semir.md#known-results-and-execution-requirements): current semantic boundaries.
- [Compiler architecture notes](../notes/compiler-architecture.md#preparation-and-scheduling-should-match-reuse): preparation cost and locality.
- [Execution model](execution-model.md), [constant storage](constant-storage.md), [uninitialized storage](uninitialized-storage.md), and [layout](layout.md): related semantic designs.
- [Infrastructure candidates](roadmap.md#deferred-infrastructure): concurrent queries and persistent caches.
