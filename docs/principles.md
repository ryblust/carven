# Design principles

This document states the criteria for Carven's language and implementation
choices. Current behavior is defined in the language and toolchain references;
implementation responsibilities are defined in the compiler and backend documents.
See the [documentation index](README.md) for their scopes.

## Intent over mechanism

Source expresses access, ownership transfer, captures, and failure handling.
The compiler selects construction, storage, and control flow that preserve
those choices. Expose representation choices when they affect behavior or a
caller's obligations.

Inference has defined inputs, precedence, defaults, stopping conditions, and
incompatible cases. Changes to ownership, mutable access, captures, failure
handling, or retained lifetimes follow an explicit source form or a rule of
the enclosing construct.

## Compile-time computation and specialization

Use established types, values, structure, access, lifetimes, and failure contracts
to select native implementations. Format structure, extents, and result uses can
resolve work before it becomes C++ calls and temporary objects. For each operation,
identify the available facts, the work they remove, and the native operations
that consume them.

An operation can combine complete precomputation, preparation of known parts,
and specialized runtime calls. Use general runtime work where available facts
or the operation's contract do not justify specialization. Dynamic values can
still have known structure, conversion choices, or output sizes. Formatting, for
example, can prepare literal segments and conversion instructions while leaving
value conversion and destination management to runtime.

Semantic analysis publishes validated facts; lowering selects their realization;
runtime performs the remaining work. Pass useful facts through data, constants,
concrete types, or template arguments without requiring source code to restate
its structure. Constant arguments expose choices when visible to the native
optimizer; template arguments preserve static choices across function boundaries.
Runtime APIs should consume prepared facts directly. Use access and lifetime
facts to guide storage, snapshots, and native argument passing.

Facts identify their subject and validity domain. Writes and calls invalidate
assumptions they may change; a control-flow join retains facts established on
every reachable incoming path. Publish facts with their semantic owner so later
stages consume them directly.

Knowing a result and proving execution removable are separate facts. Preserve
required evaluation, storage observations, ownership, cleanup, and failure
behavior. Compiler-side computation must agree with the target operation's
behavior.

Required constant execution completes before emission under an explicit source
contract and resource limits. Define admitted operations and results separately
from optional precomputation. Allow ordinary control flow, mutable locals, and
incremental construction within that contract. Explicit compile-time execution
can also produce output or validate tests without retaining a result value.
Give observable operations an execution-stage contract; optional precomputation
must preserve their required effects.

Compiler-side implementations can use host code independently of C++ constant
evaluation support in the runtime implementation. Define which completed types
and backing storage can be retained after temporary construction state ends.
Optional precomputation preserves owning construction; required constant results
follow their defined freezing rules.

Distinguish exact sizes, upper bounds, and minimum widths when planning storage.
Size facts inform reservation; the runtime selects its growth policy within
explicit destination, allocation, and space constraints. Select the combination
of precomputation, specialization, and runtime work from the operation's contract
and representative measurements.

## Typed failure contracts

Callable contracts describe successful results and possible failure types.
Composition preserves failure types and payloads for propagation, recovery,
or translation. Functions and callbacks use the same model.

Use inference for private failure sets and checked bounds for shared interfaces.
Make propagation and handling visible; handlers cover the failures they consume.
Handling preserves evaluation order, ownership, and cleanup. Completed effects
remain on failure. The application defines recovery and any rollback operation.

## Zero-overhead abstractions

Use skilled handwritten C++ with the same evaluation, ownership, lifetime,
and safety guarantees as the cost baseline. Runtime storage, allocation,
indirection, checks, and dispatch each serve a required behavior. Compile-time
facts need runtime representation only when execution uses them.

Evaluate Carven compilation time and memory, generated C++ compilation cost,
artifact size, and program execution time and memory separately. Include storage
lifetime and reuse, and small and large inputs where relevant. Choose defaults
from end-to-end workload costs, the size of gains or losses, and explicit
resource constraints.

Performance claims identify the comparison implementation, workload, and
measurement method. Evaluate generated readability and machine-code performance
separately. Establish whole-operation gains through compiler selection and runtime
execution together; scope kernel measurements to the kernel. Correctness holds
with optional optimizer transformations disabled.

## Generate idiomatic C++

Express resolved types, construction, access, and control flow through ordinary
C++ facilities. Additional storage and control preserve sequencing, failure
handling, and lifetimes. Use shared runtime operations to keep generated uses
small where appropriate. Evaluate generated clarity, runtime cost, and native
compilation cost together.

Resolve source-level choices before emitting C++: compute known parts, select
runtime policies, construct directly into the required destination, and retain
only necessary execution and storage. The C++ compiler owns general range and
loop optimization, interprocedural optimization, and machine-code selection.

## Optimization analysis boundary

Optional analysis uses structured semantic operations for a concrete
implementation choice. Each analysis defines its fact domain, invalidation
rules, and stopping condition. Unknown facts retain the ordinary operation.

Ownership, nullability, types, and failures have their own correctness analyses
and solvers. Their published facts can guide implementation selection. Required
semantic checks and constant execution follow their language contracts.

## Build on the C++ ecosystem

Evaluate native foundations for behavior, maturity, performance, portability,
and fit with the intended operation. Define source behavior, access, and lifetime
requirements before selecting a native implementation. Model native details
where a language rule depends on them.

## Language mechanisms and library composition

Give each builtin a defined role in language semantics: typing, evaluation,
access, ownership, lifetimes, or observable effects. The compiler establishes
these contracts and carries their facts through execution and lowering. Runtime
support implements the operations required by their native realization.

Libraries compose these mechanisms into algorithms, data structures, and public
APIs. A type's language-level contract and its library operations have distinct
responsibilities. Place compiler knowledge at the semantic boundary and keep
algorithmic choices with the library that owns them.

Standard and user crafts use the same language facilities and admission rules.
Develop shared execution capabilities so library code can benefit from
compile-time evaluation and specialization within defined operation and result
contracts. Let concrete library needs guide the evolution of language mechanisms.

## Semantic authority and native boundaries

Carven defines evaluation order, access, ownership, lifetimes, and failures for
an explicit source batch. Validate statically checkable conditions before
publishing the semantic program. Preserve those semantics and required runtime
checks through lowering and execution.

External C++ function bodies remain outside Carven's analysis. Carven checks
argument evaluation, access, and known lifetime relationships at native calls.
C++ resolves delegated declarations, types, and operations. Providers and callers
satisfy the external behavior and storage contracts; native results establish
only the relationships supported by the boundary's semantic model.

The build system owns dependency acquisition, source collection, and native
build configuration.

Give each semantic fact one authority. Later stages consume published facts;
generated names and text represent their output.

At each native boundary, define the responsibilities of Carven, the provider,
and the caller. State delegated checks, object validity and lifetime assumptions,
failure behavior, and environment dependencies. Address retention, returned
aliases, and reentry explicitly. Place native exception recovery before boundaries
where escaping exceptions terminate execution. Use native traits to establish
delegated type requirements.

## Require only what an operation needs

Generated steps require the construction, assignment, and invocation operations
needed by the source contract. Read access should not require user-defined copying.
Use direct destination initialization to avoid unnecessary default construction
or assignment. Realize ownership transfer through the applicable constructor.
Apply these requirements to temporaries, wrappers, and intermediate storage.

Record additional implementation requirements as limitations in the owning
reference. Changes to admitted types belong in the language contract.

## Practical safety

Carven's implementation is responsible for realizing its defined semantics and
enforcing the associated checks. Specify each operation's valid uses, checked
conditions, and caller obligations so its guarantees have a clear scope.

Program and library authors choose algorithms and operations that meet their
functional requirements. Callers satisfy API preconditions and external validity
obligations. Validate functional behavior through tests and operation-specific
checks. Applications define recovery policies.

Use static checks where sufficient facts exist and runtime checks where the
operation requires them. Weigh prevented errors against complexity for users,
runtime costs, and integration costs.

State what each check proves and which assumptions it requires. Non-null checking
establishes an address condition; the external owner establishes object liveness.
Match safety claims to these checks and assumptions. Diagnose violations at the
responsible boundary. Give termination and recoverable failure distinct
contracts.

## Simplicity and feature admission

Give each intent a direct, consistent expression through composable rules.
Additional forms need a meaningful difference in behavior or contract. Use tokens
and delimiters to keep syntactic structure consistent across scopes.

Require a concrete use that existing forms cannot adequately express. Evaluate
features against that use, including their rules, exceptions, interactions,
runtime costs, and tooling burden. Settle meaning before syntax.
Add extension points when a consumer needs them. Define admitted uses and
diagnostics at the boundary responsible for each feature.
