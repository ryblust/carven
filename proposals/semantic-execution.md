# Semantic execution extensions

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Direct structured constant initializer and extent expressions, scalar text traversal, address observation, literal identity, retained reference graphs, and deferred host scripting and build-stage execution

## Summary

This proposal explores inline structured expressions in static initializers and
array extents, and source-visible observations of execution addresses. These are
independent design slices. Structured roots can reduce one-use `const fn` helpers;
address observations and retained reference graphs need explicit identity and
stability guarantees.

Host scripting and an explicit build-stage entry are deferred extensions of the
shared execution model. Their remaining questions concern host capabilities,
invocation, dependencies, and output publication. They can be explored without
requiring address observation, retained reference graphs, or arbitrary native
C++ execution.

## Execution boundary

Structured control already executes in `const fn`, `const` blocks, and static
tests. Static parameters, `const if`, and `const for` already produce residual
runtime bodies. Arrays, tuples, structures, payload enums, String, Vector, and
slices have admitted execution and retained-value paths. These implemented
contracts belong to the [execution reference](../docs/compiler/analysis/evaluation.md);
they are not pending extensions here.

Interpretation and explicit static execution share semantic operations and the
executor. Supported input, clocks, waits, and timeout composition already execute
through compiler-host adapters. `const fn` declares static execution capability;
observable work belongs to the selected stage. Type dependency queries restrict
host effects, and retaining a completed constant has separate publication rules.
Native C++ inputs may be present, but semantic execution does not load their
code or run their global initialization. These boundaries already support a
useful language subset independently of complete native interoperation.

The remaining structured-root slice is direct `if`, `match`, or `try` in a
constant initializer or array extent. `StaticRootSite` rejects those forms even
though the shared executor supports them inside admitted bodies. Extending root
construction and declaration-time scope is therefore still necessary.

Execution places use domain and object identities with projection paths.
Address-observation and graph-retention designs must preserve invalidation by
Take and scope exit; reinitialization cannot revive old pointers. Source
admission, executed-path support, and result publication remain separate checks.

## Structured expressions in static roots

An initializer that chooses between values could express its selection directly
instead of declaring a one-use `const fn`. Direct `if`, `match`, and `try` are
separate candidates.

Reuse ordinary expression construction for typing, branch results, scopes, and
lifetimes. The root entry continues to own constant-name visibility, operation
admission, dependency requests, execution budgets, and completion. Declaration
types and array extents may require values before global body construction and
solving are complete. Forward requests, dependency cycles, and root-local bindings
therefore need a construction and execution protocol available at declaration time.

Reuse branch analysis and execution. Initializer freezing and the nonnegative-integer
check for an extent remain distinct completion paths. Define legality of both
branches and the unselected path explicitly, including names, failures, and
unsupported operations; executing one branch does not validate the other.

## Address observation

Choose the relationship being observed before choosing its representation:

| Candidate | Contract to define |
| --- | --- |
| Debug identity | Execution domain, object, projection, and stability interval |
| Execution-environment virtual address | Null, copying, conversion, subobject relations, reuse, and observations after invalidation |
| Target-layout virtual memory | Alignment, address width, field offsets, byte access, and provenance |
| Native bridge | ABI, synchronization, reentry, permissions, lifetime, and failure |

An internal `ExecutionValue*` cannot stand for a source address. Distinct object
identities do not guarantee different address text, and the same address does not
grant the same access permissions. Assigning different numbers to every
projection does not reproduce native subobject address relations. Invalid-pointer
comparison and display need their own policy; rejection of dereference does not
settle those observations.

Address text can be compared or used in control flow, so formatting still has an
observable contract. A C-string converted to `ptr<void>` and then formatted is a
candidate probe. Conversion must preserve its target and permissions without
creating an object or granting Write access.

## C-string literal identity

Constant content and source object identity require separate representations.
The alternatives are:

| Candidate policy | Required execution and backend guarantees |
| --- | --- |
| Every source literal has independent identity | Retain occurrence identity and emit backing that guarantees separation, including across artifacts |
| Equal contents always share identity | Define the merging domain and guarantee shared backing across the applicable modules |
| Sharing or separation is implementation-selected | Do not turn an implementation-dependent identity observation into an environment-independent constant fact |

The first two choices need explicit backing and a defined cross-module strategy;
optional C++ literal pooling cannot guarantee them. Introduce occurrence identity
when the selected policy requires it. A merged `ConstantID` cannot reconstruct
the original literal occurrences.

## Required and interpreted observation policies

Required execution has three candidate boundaries:

- Keep address observation unsupported.
- Permit diagnostic or debug output through an output boundary that does not
  return an ordinary value.
- Permit address text as an ordinary constant, defining whether it describes the
  compile-time environment and its stability within one execution, across
  repeated compilations, and across targets.

Interpreted display can be decided independently. Literal-identity and address
policies must agree wherever an observation includes a literal.

Rejecting pointer values only at freezing is insufficient to prevent address
observations from influencing retained results: an observation can first become
text or an integer, choose a branch, or determine an array extent. A policy that
prohibits that influence must restrict the observation itself or define complete
dependency rules.

## Text traversal admission

Static scalar traversal through `.chars` remains unsupported; this is a computed
range projection, not a method call. Byte traversal through `.bytes` is already
admitted. A text-processing consumer can select scalar traversal. Reuse the text
backing and Unicode scalar contracts, and define cursor lifetime, mutation
invalidation, iteration work accounting, and rejection of escaping views.

## Retained reference graphs

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

## Host scripting and staged builds

### Research basis

| Source | Observed boundary | Implication for Carven |
| --- | --- | --- |
| NimScript | Uses Nim's compile-time VM for standalone scripts, configuration, and build tasks; ordinary FFI is unavailable. | A useful scripting subset can reuse semantic execution while native integration develops separately. |
| Zig Build System | `build.zig` describes a graph of build steps, including generated files and tool execution. | Sharing the project language does not settle task ordering, artifact ownership, or rebuild policy. |
| Cargo build scripts | A host executable runs before package compilation, writes generated outputs under `OUT_DIR`, and reports file/environment dependencies for reruns. Host configuration differs from target configuration. | A pre-build entry needs explicit inputs, outputs, failure behavior, and host/target context even when it shares the application's language. |
| Build Systems à la Carte | Separates task scheduling from the decision to rebuild and studies static and dynamic dependencies. | Keep execution capability, dependency discovery, and result reuse as separate decisions. |

These comparisons motivate the following candidates. The implementation and
invocation model remain open.

### DEFER-02 — Host scripting through semantic execution

- **Status:** Deferred
- **Reason deferred:** Stabilize the current standard APIs, host execution, and
  native boundaries before extending the execution surface.
- **Reactivation condition:** After that consolidation, select a concrete script
  such as reading project configuration and generating a source or resource file,
  with a bounded set of missing host operations.

Use the existing source language, checking, and semantic executor for scripts.
Select host capabilities from the consumer: configuration-driven generation may
need file access and arguments; a tool wrapper may also need environment and
process operations. Define each capability's execution stages, value and failure
contracts, resource ownership, and cancellation behavior where applicable.
Scripting does not require interpreting arbitrary C++ providers or emulating
their ABI and global initialization.

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

### DEFER-03 — Explicit build-stage entry

- **Status:** Deferred
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

The bounded experiment should establish stage ordering, generation failure and
cleanup, dependency cycles, host/target separation, and agreement with a fresh
build after input changes. If reuse is selected, include unchanged runs, changed
configuration or tools, and obsolete output removal. Once invocation, artifacts,
and scheduling require a developed toolchain contract, move this slice to a
dedicated proposal while keeping host execution owned here.

## Open decisions

### OPEN-01 — Define one structured static-root form

Choose `if`, `match`, or `try` and define its source legality, root-local scope,
dependency protocol, and completion paths. Close the decision with initialization
and extent examples, including forward dependencies and rejected cycles.

### OPEN-02 — Choose literal identity and its domain

Choose a literal policy and the backing guarantees needed to implement it.
Explain equal contents, separate occurrences, and cross-module uses. This decision
is needed for observations that depend on literal identity.

### OPEN-03 — Select an observation and execution-mode policy

Specify the observed relationship, permissions, invalidation behavior, and
stability interval, and choose its required and interpreted admission. Apply
`OPEN-02` when literals participate. State whether an ordinary result can carry
the observation and how retained-result restrictions are enforced.

## Validation

Structured-root cases cover both-branch legality, exactly-once evaluation,
unselected-path contracts, forward dependencies and cycles, source failures,
dangling borrows, budgets, and freezing rejection. Check initializer and extent
completion independently.

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

- [Semantic execution](../docs/compiler/analysis/evaluation.md): current roots, storage, and freezing.
- [Static-root construction](../src/semantic/analysis/constant/root.cppm): source admission for initializer and extent expressions.
- [Constant admission tests](../tests/internal/semantic/analysis/const_admission.cpp): structured body support and rejected scalar traversal.
- [Text](../docs/language/text.md): `.bytes` and `.chars` source contracts.
- [CLI interpretation](../docs/toolchain/cli.md#interpretation): current invocation, execution subset, and native boundary.
- [Static execution](../docs/language/constants.md#static-execution-of-functions): callable capability, host effects, and result retention.
- [Frontend analysis](frontend-analysis.md#static-evaluation-and-observable-effects): query execution, effects, and event delivery.
- [Compiler optimization](compiler-optimization.md): preparation, analysis reuse, and build-worker boundaries.
- [NimScript](https://nim-lang.org/docs/nims.html): shared compile-time VM, scripting and build tasks, and FFI limitations.
- [Zig Build System](https://ziglang.org/learn/build-system/): build graphs, generated files, and tool execution.
- [Cargo build scripts](https://doc.rust-lang.org/cargo/reference/build-scripts.html): host execution, output directories, and dependency-based reruns.
- Mokhov, Mitchell, and Peyton Jones, [Build Systems à la Carte](https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems-final.pdf), ICFP 2018: scheduling and rebuilding as separate design choices.
