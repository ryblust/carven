# Semantic execution extensions

- **Status:** Draft
- **Scope:** Interpreted closures, additional address observations, retained reference graphs, and host scripting and build-stage execution

## Summary

Extend the shared semantic executor at the following boundaries. Each slice has
its own admission, lifetime, and validation requirements.

| Slice | Design status | Remaining work |
| --- | --- | --- |
| Interpreted closures (`EXEC-01`) | Accepted | Execute concrete closures and their captures through ordinary calls |
| Additional address observations (`OPEN-03`) | Exploration | Select an observation and its stability domain |
| Retained reference graphs (`DEFER-01`) | Deferred | Select a static-data consumer requiring shared nodes or cycles |
| Host scripting (`DEFER-02`) | Deferred | Select a script and the host operations it requires |
| Build-stage entry (`DEFER-03`) | Deferred | Define a source-generation workflow and its publication boundary |

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

### Retained reference graphs

#### DEFER-01 — Shared or cyclic static graphs

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

### Host scripting and staged builds

#### Research basis

| Source | Observed boundary | Implication for Carven |
| --- | --- | --- |
| NimScript | Uses Nim's compile-time VM for standalone scripts, configuration, and build tasks; ordinary FFI is unavailable. | A useful scripting subset can reuse semantic execution while native integration develops separately. |
| Zig Build System | `build.zig` describes a graph of build steps, including generated files and tool execution. | Sharing the project language does not settle task ordering, artifact ownership, or rebuild policy. |
| Cargo build scripts | A host executable runs before package compilation, writes generated outputs under `OUT_DIR`, and reports file/environment dependencies for reruns. Host configuration differs from target configuration. | A pre-build entry needs explicit inputs, outputs, failure behavior, and host/target context even when it shares the application's language. |
| Build Systems à la Carte | Separates task scheduling from the decision to rebuild and studies static and dynamic dependencies. | Keep execution capability, dependency discovery, and result reuse as separate decisions. |

These comparisons motivate the following candidates. The implementation and
invocation model remain open.

#### DEFER-02 — Host scripting through semantic execution

- **Reason deferred:** No script has selected the required file, argument,
  environment, or process operations and invocation contract.
- **Reactivation condition:** Select a script such as reading project
  configuration and generating a source or resource file, with a bounded set
  of required host operations.

Use the existing source language, checking, and semantic executor for scripts.
Select host capabilities from the consumer: configuration-driven generation may
need file access and arguments; a tool wrapper may also need environment and
process operations. Define each capability's execution stages, value and failure
contracts, resource ownership, and cancellation behavior where applicable.
Implement host operations through execution adapters with explicit provider
contracts.

Choose invocation and process behavior before broadening the API: arguments,
working directory, output routing, exit status, and interruption all affect a
script's observable contract. Compare extending `interpret` with a dedicated
script entry only when the consumer demonstrates a distinction. Calls supported
in multiple stages preserve ordinary ordering, ownership, and failures; host
state is observed in the selected environment. Execution support does not make
a live host resource eligible for constant retention.

Keep execution obligations explicit when analysis or tooling requests values.
A dependency query must not replay a script's host work. Reusing a value must not
silently skip required reads, writes, output, or cleanup. Frontend analysis owns
query effects and event delivery; compiler optimization owns reuse experiments
under those contracts. A first scripting consumer can execute without caching.

Validate the selected script's values, typed failures, output, exit behavior,
and resource cleanup. Compare native and interpreted behavior for their common
supported operations. Exercise reached unsupported native operations and ensure
that merely collecting native inputs does not reject an otherwise supported
script. Include explicit static execution only for capabilities admitted there.

#### DEFER-03 — Explicit build-stage entry

- **Reason deferred:** A build entry needs a demonstrated generation workflow and
  stage/dependency contracts beyond the current executor. Xmake owns current
  project builds and native scheduling.
- **Reactivation condition:** A project needs configuration-driven source or
  resource generation before application analysis, and the required scripting
  capabilities have a defined contract.

`build.cv` is a provisional entry name. Compare an ordinary Carven script invoked
by the existing build tool with a dedicated entry that publishes inputs for a
subsequent compilation. A task-graph API is a further option if a concrete
multi-step workflow requires scheduling and incremental reuse. The initial
consumer is configuration input, generation of one `.cv` file, and compilation
and execution of its application through the existing toolchain.

Define the stage boundary independently of ordinary application `const` roots:
the build entry and its own dependencies must be analyzable before it can produce
application inputs. Reject cycles that require those outputs to analyze the
entry itself. Record which source selection and configuration the next stage
receives, and distinguish the executing host from the generated program's target.

Specify output ownership and successful publication before adding reuse. A failed
or interrupted generation must not make partial output available as a completed
input. Distinguish repeatable dependency reads from effects whose execution count
matters. File, environment, and tool dependencies need an explicit declaration
or observation model; clock reads and other untracked observations need a defined
rerun or freezing policy. Language execution capability alone establishes none
of these cache guarantees.

The bounded experiment must establish stage ordering, generation failure and
cleanup, dependency cycles, host/target separation, and agreement with a fresh
build after input changes. If reuse is selected, include unchanged runs, changed
configuration or tools, and obsolete output removal.

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

## References

- [NimScript](https://nim-lang.org/docs/nims.html): shared compile-time VM, scripting and build tasks, and FFI limitations.
- [Zig Build System](https://ziglang.org/learn/build-system/): build graphs, generated files, and tool execution.
- [Cargo build scripts](https://doc.rust-lang.org/cargo/reference/build-scripts.html): host execution, output directories, and dependency-based reruns.
- Mokhov, Mitchell, and Peyton Jones, [Build Systems à la Carte](https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems-final.pdf), ICFP 2018: scheduling and rebuilding as separate design choices.
