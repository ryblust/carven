# Semantic execution extensions

- **Status:** Draft
- **Scope:** Interpreted closures, additional address observations, and retained reference graphs

## Summary

Extend the shared semantic executor at the following boundaries. Each slice has
its own admission, lifetime, and validation requirements.

| Slice | Design status | Remaining work |
| --- | --- | --- |
| Interpreted closures (`EXEC-01`) | Accepted | Execute concrete closures and their captures through ordinary calls |
| Additional address observations (`OPEN-03`) | Exploration | Select an observation and its stability domain |
| Retained reference graphs (`DEFER-01`) | Deferred | Select a static-data consumer requiring shared nodes or cycles |

## Context

Structured control executes in constant initializers, array extents, `const fn`,
`const` blocks, and static tests. Static parameters, `const if`, and `const for`
produce residual runtime bodies. Arrays, tuples, structures, payload enums,
String, Vector, and slices have admitted execution and retained-value paths. Byte
and character traversal share text backing and borrowing rules.

Interpretation and explicit static execution share semantic operations and the
executor. Supported input, clocks, waits, and timeout composition execute
through compiler-host adapters. `const fn` declares static execution capability;
observable work belongs to the selected stage. Type dependency queries restrict
host effects, and retaining a completed constant has separate publication rules.
Native C++ inputs may be present, but semantic execution does not load their
code or run their global initialization. Concrete closure construction remains
unsupported by the executor.

Hosted execution binds native capabilities through typed contracts. The same
Carven declarations supply generated native interfaces and interpreted service
calls. `carven build` interprets `build.cv`; `std::build` owns target declarations,
object reuse, native subprocess scheduling, and linking. These implemented
capabilities do not admit arbitrary native code or retained live host resources
into static execution.

Execution places use domain and object identities with projection paths.
Static erased-pointer formatting produces fixed null/non-null text; interpreted
formatting produces execution-local object and projection labels. Pointer
comparison and restoration use established address relations, including
standard-layout first-field relations. Equal C-string contents share immutable
program-lifetime backing within one linkage domain, including across generated
modules and artifacts. Known C strings select execution backing by canonical
contents. Numeric addresses, target-layout queries, and identity guarantees
across execution environments remain outside these contracts.

Address-observation and graph-retention designs must preserve invalidation by
Take and scope exit; reinitialization cannot revive old pointers. Source
admission, executed-path support, and result publication remain separate checks.

## Design

### Ownership and slice dependencies

Compiler analysis publishes callable identities, captures, lifetime and failure
contracts, and stage admission. The shared semantic executor interprets those
facts using logical values and places. Builtin operations retain their bounded
execution contracts; compiler-host adapters implement admitted host interactions.
Runtime Craft supplies shared native representations and execution support for
generated programs. Standard Craft and user source compose the admitted APIs.

`EXEC-01` requires concrete callable publication and ordinary capture/call
semantics. Address observations select their own identity and stability contract;
retained graphs require a consumer needing shared nodes or cycles. Neither is a
prerequisite for concrete closure execution. Static closure invocation would
add stage admission, and retained closure constants would additionally need a
publication contract; these are separate decisions from interpreted calls.

### EXEC-01 — Interpreted closures

- **Status:** Accepted
- **Implementation:** Not started

Execute concrete closure construction, copying, and invocation in interpreted
runtime code. Ordinary closure calls and sequence callbacks use the same
invocation and capture-binding path.

Represent a closure by its published callable identity and capture storage.
Construct captures in source order. Apply ordinary owning-copy rules to value
captures and retain live execution coordinates for Write captures. Preserve
result backing and cleanup. Select the closure object before evaluating arguments,
then read that object's current captures after argument evaluation. An explicit
closure copy establishes a value-capture snapshot. Resolve the callable and bind
captures through the existing execution context without repeating source analysis.

Keep static admission and retained-result publication at their existing
boundaries. This delivery enables interpreted runtime closure calls; static
closure invocation and retained closure constants remain unsupported. Optional
precomputation must preserve callback effects, capture construction, reads, and
cleanup.

Validate ordinary calls and sequence callbacks against generated execution.
Cover value and Write captures, copying, returned closures, aliased callable
replacement during argument evaluation, returned borrows, typed failures,
cleanup, and execution budgets. Validate the shared executor with ordinary closure
programs. Sequence consumers additionally check traversal demand and view lifetimes.

### Address observation

Choose the relationship being observed before choosing its representation:

| Candidate | Contract to define |
| --- | --- |
| Debug identity | Execution domain, object, projection, and stability interval |
| Execution-environment virtual address | Null, copying, conversion, subobject relations, reuse, and observations after invalidation |
| Target-layout virtual memory | Alignment, address width, field offsets, byte access, and provenance |
| Native bridge | ABI, synchronization, reentry, permissions, lifetime, and failure |

Represent source addresses with execution identities and established subobject
relations. Keep access permissions separate from address equality. Canonicalize
projections that share an address and define comparison and display after target
invalidation. Keep compiler-host pointers private to the executor.

Address text can be compared or used in control flow, so formatting still has an
observable contract. A C-string converted to `ptr<void>` and then formatted is a
candidate probe. Conversion must preserve its target and permissions without
creating an object or granting Write access.

### Required and interpreted observation policies

Stronger address observations have three candidate boundaries in required
execution:

- Retain the current fixed null/non-null static display without object identity.
- Permit diagnostic or debug output through an output boundary that does not
  return an ordinary value.
- Permit address text as an ordinary constant, defining whether it describes the
  compile-time environment and its stability within one execution, across
  repeated compilations, and across targets.

Define any stronger interpreted display separately, preserving established
address equality and C-string backing identity.

An address observation can become text or an integer, choose a branch, or
determine an array extent before freezing. Apply restrictions at the observation
or track its dependencies through those uses. Result-pointer rejection alone
does not cover them.

## Open decisions

### OPEN-03 — Select an observation and execution-mode policy

Specify the observed relationship, permissions, invalidation behavior, and
stability interval, and choose its required and interpreted admission. Preserve
the established C-string identity within each linkage domain. State whether an
ordinary result can carry the observation and how retained-result restrictions
are enforced.

## Deferred work

### DEFER-01 — Shared or cyclic static graphs

- **Reason deferred:** Retained alias identity, cycles, and access permissions
  require an explicit source contract.
- **Reactivation condition:** A static-data consumer needs shared nodes or cycles
  and can state its roots, relationships, and permissions.

Retain a root-reachable closure of eligible objects with shared node identity and
valid typed subobject projections. Define promotable targets, admitted cycles,
external resources, and escaping mutable aliases. Promotion cannot repair a
pointer already dangling during execution. The address-observation policy must
agree with the relationships preserved by retention.

A graph needs node identities, staged node and edge construction, and publication
checks for a closed graph; a pointer alternative in the recursive constant tree
is insufficient. Analysis owns canonical publication. Lowering assigns target
symbols and relocations; compiler-host addresses and execution coordinates never
become target pointers.

Validate shared and separate equal-valued nodes, subobjects, closed and escaping
roots, dangling targets, admitted cycles, access permissions, and resource limits.
Native reconstruction must establish C++20 static initialization across modules
without silently adding runtime initialization. Observe alias relationships and
backing lifetimes in the generated program.

## Validation

Address cases cover equal-valued independent objects, copied-pointer aliases,
assignment visibility, Take followed by reinitialization, exactly-once target
selection, Read and Write observations, failure and region cleanup, and literal
identity across modules. Define and test invalid-pointer display and comparison
without dereferencing a dangling pointer. If the selected scope includes payload
positions, test invalidation after variant replacement.

For the common valid subset, required, interpreted, and generated native execution
need separate evidence. Required and interpreted execution share an executor and
cannot establish independent correctness merely by agreeing. Check promised
address relations within each environment, then compare environment-independent
results. Do not strip all address text from comparisons in a way that hides alias
errors. Preserve source contracts for copying, cleanup, typed failure, and resource
stops while implementing the selected observation.
