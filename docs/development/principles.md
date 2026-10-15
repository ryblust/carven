# Design principles

This document states the criteria for Carven's language and implementation
choices. Language and toolchain references specify current behavior; compiler
and backend documents describe implementation responsibilities.

## Intent over mechanism

Source expresses access, ownership transfer, captures, and failure handling.
The compiler selects construction, storage, and control flow that preserve
those choices. Expose representation choices when they affect behavior or a
caller's obligations.

Prefer direct source forms for common operations. Let the compiler use known
types and semantic facts to perform the required checking and implementation
selection. Evaluate the resulting complexity across user code, language rules,
compiler implementation, and generated C++.

Inference has defined inputs, precedence, defaults, stopping conditions, and
incompatible cases. Changes to ownership, mutable access, captures, failure
handling, or retained lifetimes follow an explicit source form or a rule of
the enclosing construct.

## Static computation and specialization

Established types, values, structure, access, lifetimes, and failure contracts
inform native implementation selection. Specialization depends on the available
facts, the work they remove, and the native operations that consume them.

An operation can combine complete precomputation, preparation of known parts,
and specialized runtime calls. Use general runtime work where available facts
or the operation's contract do not justify specialization. Dynamic values can
still have known structure, conversion choices, or output sizes.

Semantic analysis publishes validated facts; lowering selects their realization;
runtime performs the remaining work. Published facts remain available to their
consumers without requiring source code to restate its structure. Access and
lifetime facts inform storage, snapshots, and native argument passing.

Facts identify their subject and validity domain. Writes and calls invalidate
assumptions they may change; a control-flow join retains facts established on
every reachable incoming path. Publish facts with their semantic owner so later
stages consume them directly.

Knowing a result and proving execution removable are separate facts. Preserve
required evaluation, storage observations, ownership, cleanup, and failure
behavior. Compiler-side computation must agree with the target operation's
behavior.

Static execution completes before emission under an explicit source
contract and resource limits. Define admitted operations and results separately
from optional precomputation. Allow ordinary control flow, mutable locals, and
incremental construction within that contract. Explicit static execution
can also produce output or validate tests without retaining a result value.
Give observable operations an execution-stage contract; optional precomputation
must preserve their required effects.

`const` marks static execution capability. Supported effects, such as input,
clock sampling, waiting, and printing, occur on the compiler host during explicit
static execution. Ordinary calls retain their runtime stage; known arguments
alone do not move observable work into compilation.

Compiler-side implementations can use host code independently of C++ constant
evaluation support in the runtime implementation. Define which completed types
and backing storage can be retained after temporary construction state ends.
Optional precomputation preserves owning construction; static results
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
and safety guarantees as the cost baseline. Select native implementations that
meet or improve that baseline without adding runtime work solely to represent
a language abstraction. Runtime storage, allocation, indirection, checks, and
dispatch each serve a required behavior. Static facts need runtime representation
only when execution uses them.

Distinguish source-level transfer from incidental movement of an implementation
carrier. Use known type contents, ownership, and lifetime facts to remove carrier
storage and movement that have no observable effect. Preserve native construction,
address observations, transfer effects, and cleanup at their established boundaries.

Carven compilation, generated C++ compilation, artifact size, and program execution
have distinct costs. Evaluate the dimensions affected by a design and its
performance claims, including storage lifetime, reuse, and input scale where
relevant. Choose defaults from end-to-end workload costs, the size of gains or
losses, and explicit resource constraints.

Performance claims identify the comparison implementation, workload, and
measurement method. Evaluate generated readability and machine-code performance
separately. Establish whole-operation gains through compiler selection and runtime
execution together; scope kernel measurements to the kernel. Correctness holds
with optional optimizer transformations disabled.

## Generate idiomatic C++

Source `class` and `struct` declarations establish nominal identity, fields, and
access rules. Their C++ carriers and physical layout are implementation choices.
Select representations from established semantic facts while preserving values,
evaluation order, addressability, ownership, lifetimes, and failures. Native
interfaces constrain the representation and operations exposed at their boundary.

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
semantic checks and static execution follow their language contracts.

## Build on the C++ ecosystem

Evaluate native foundations for behavior, maturity, performance, portability,
and fit with the intended operation. Define source behavior, access, and lifetime
requirements before selecting a native implementation. Model native details
where a language rule depends on them.

## Language mechanisms and library composition

Organize standard APIs around the capability a caller needs. Choose names,
defaults, and type-associated methods from ordinary use. Decide the public entry,
declaration owner, implementation location, and execution capability separately.

Keep related public types, errors, and operations together at the shortest module
path that identifies their capability.
A public submodule supplies an independently useful contract or vocabulary.
Implementation decomposition follows its algorithms and dependencies; it does not
require another public entry. Apply these criteria to source and builtin APIs alike.

Standard Craft organizes public APIs and reusable source implementations.
Builtins supply types and operations whose semantics the compiler owns.
Runtime Craft supplies C++ representations and operations for generated programs.
A standard capability can use all three.

Default visibility is a separate API choice. Defaults serve pervasive language
types and common entry operations; builtin ownership does not make a name
default-visible. Resolving an operation's result or failure type does not add a
binding to the caller's scope. Inference and propagation use that type without
requiring its name in scope; explicit names follow ordinary lookup and import rules.

Choose builtin ownership by four questions:

- What type, access, lifetime, failure, or execution knowledge must the compiler own?
- Can existing source operations naturally express the complete contract?
- Does compiler knowledge simplify use or remove repeated analysis, checks,
  conversions, storage, or calls?
- How much library-specific policy and maintenance does it add to the compiler?

Method syntax, operand count, algorithm size, and source expressibility do not
individually determine ownership. Builtins participate in the shared type, access,
ownership, lifetime, and failure models. `Operation` is a builtin type; ordinary
async functions and builtin composition produce values of that same type.

`print` uses known argument types and order to offer heterogeneous printing.
`when_all` derives result and failure types and checks ownership transfer. Their
bounded builtin APIs provide these capabilities without requiring callers to use
variadic templates or higher-order generic machinery. Scalar and SIMD floating
methods share a bounded numeric contract even where comparisons and selection
can express their bodies. SIMD block traversal and scanning compose builtin
operations in source.

Choose ownership by the contract being implemented:

- Prefer source declarations for ordinary public data contracts, including domain
  errors, result enums, and interval values. Builtin operations can resolve and
  use those declarations through their ordinary type rules. Duration literals,
  numeric parsing, and timeout composition use standard-source declarations.
- Prefer reusable source bodies when existing operations express the full
  contract. JSON, text processing, framing, and SIMD scanning use ordinary
  functions. Include effects, failure recovery, cancellation, and retained
  resources when evaluating that contract. Associated entries can select source
  functions or builtin operations.
- Keep a host operation together when its state and completion rules need one
  execution boundary. Algorithms and host operations can remain builtin when
  that gives a simpler API and execution model. Library-specific native adapters
  belong to their owning Craft.

Select native implementations and optimizations separately from API ownership.
Inspect generated C++ and optimized native code to distinguish required work from
removable intermediate expressions.

Standard and user source share discovery, module resolution, declaration
completion, analysis, execution, and publication. Resolve each import to one
canonical module identity. Standard entries may also supply compiler-provided
declarations. Resolve a builtin's standard-source dependencies through the same
catalog and declaration-completion path.

Static execution, interpretation, and native generation preserve the same values,
evaluation order, effects, ownership, and failures within a capability's supported
execution stages. Implementations can use different value representations and
host adapters; verify agreement through observable behavior. Keep execution
context, host resources, and result retention explicit.

Collect a Craft as one source package, including its native adapters. Native
source presence does not determine whether a Carven execution path is supported.
Check execution capability at the operation that needs it. Define native
initialization, cleanup, and external validity obligations in the interoperation
contract; arbitrary C++ side effects are outside semantic execution.

The compiler build is independent of Crafts headers and native libraries.
Programs being compiled receive their standard-source dependencies through
ordinary source discovery.

## Semantic authority and native boundaries

Carven defines evaluation order, access, ownership, lifetimes, and failures for
an explicit source batch. Validate conditions that analysis can check before
publishing the semantic program. Preserve those semantics and required runtime
checks through lowering and execution.

External C++ function bodies remain outside Carven's analysis. Carven checks
argument evaluation, access, and known lifetime relationships at native calls.
C++ resolves delegated declarations, types, and operations. Providers and callers
satisfy the external behavior and storage contracts; native results establish
only the relationships supported by the boundary's semantic model.

The driver combines explicit application inputs with installed Crafts. Package
authors or users prepare Crafts for the selected platform and configuration before
installation. Packaging tools can automate that preparation using the same source
layout. The build system owns dependency acquisition, additional source selection,
and native build configuration.

Give each semantic fact one authority. Later stages consume published facts;
generated names and text represent their output. Repeated local exceptions call
for reviewing the owning contract and the information it publishes. Consolidate
equivalent paths at that boundary. Distinguish implementation simplification
from changes to observable behavior, admitted programs, or caller obligations.

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
compilation and runtime costs, and tooling burden. Settle meaning before syntax.
Introduce general mechanisms such as higher-order interfaces, parameter packs,
or generic composition when independent use cases justify their shared rules.
The ability to reimplement a builtin alone does not establish that need.
Add extension points when a consumer needs them, with admitted uses and
diagnostics defined at the responsible boundary.

Judge simplification by clarity of responsibilities, fewer interacting rules
and execution paths, and preserved contracts. Reduced code size is a useful
consequence. Update names and owning boundaries to match the resulting
responsibilities.
