# Design principles

This document defines criteria for Carven's language and implementation choices.

## Intent over mechanism

Source expresses access, ownership transfer, captures, and failure handling;
the compiler selects construction, storage, and control flow that preserve them.
Expose representation choices when they affect behavior or caller obligations.

Prefer direct source forms for common operations, using known types and semantic
facts for checking and implementation selection. Evaluate complexity across user
code, language rules, compiler implementation, and generated C++.

Define inference inputs, precedence, defaults, stopping conditions, and incompatible
cases. Changes to ownership, mutable access, captures, failure handling, or retained
lifetimes follow an explicit source form or a rule of the enclosing construct.

## Static computation and specialization

Use established types, values, structure, access, lifetimes, and failure contracts
to select native implementations. Combine precomputation, preparation of known
parts, and specialized runtime calls according to available facts, the work removed,
and the native operations that consume those facts. Retain general runtime
work where the facts or contract do not justify specialization. Dynamic values can
still have known structure, conversions, or output sizes; access and lifetimes guide
storage, snapshots, and argument passing.

Do not treat a known result as proof that execution can be removed. Preserve
required evaluation, storage observations, ownership, cleanup, and failures.
Compiler-side computation must agree with the target operation's behavior.

Complete explicit static execution before emission under its source contract and
resource limits. Define admitted operations and results separately from optional
precomputation. Allow ordinary control flow, mutable locals, and incremental
construction, including effects and tests that retain no result value.

Give observable operations an execution-stage contract. `const` marks static
execution capability: supported input, clock, wait, and print effects occur on the
compiler host during explicit static execution. Ordinary calls retain their
runtime stage; known arguments alone do not move observable work into compilation.

Compiler-side implementations may use host code independently of runtime C++
constant-evaluation support. Define which completed types and backing storage
survive temporary construction state. Optional precomputation preserves owning
construction and required effects; static results follow their freezing rules.

Distinguish exact sizes, upper bounds, and minimum widths when planning storage.
Use size facts for reservation and let runtime growth respect destination,
allocation, and space constraints. Choose specialization from the operation's
contract and representative measurements.

## Typed failure contracts

Use one model for successful results and possible failure types in functions and
callbacks. Preserve failure types and payloads during composition for propagation,
recovery, or translation.

Infer private failure sets and check bounds on shared interfaces. Make propagation
and handling visible; handlers cover the failures they consume while preserving
evaluation order, ownership, and cleanup. Completed effects remain on failure;
applications define recovery and any rollback operation.

## Zero-overhead abstractions

Use skilled handwritten C++ with the same evaluation, ownership, lifetime, and
safety guarantees as the cost baseline. Meet or improve it without adding runtime
work solely to represent a language abstraction. Runtime storage, allocation,
indirection, checks, and dispatch each serve required source behavior.
Static facts need runtime representation only when execution uses them.

Distinguish source transfer from incidental carrier movement. Use type contents,
ownership, and lifetimes to remove unobservable carrier storage and movement while
preserving native construction, address observations, transfer effects, and cleanup
at their established boundaries.

Evaluate affected costs separately: Carven compilation, generated C++ compilation,
artifact size, and program execution. Include storage lifetime, reuse, and input
scale where relevant. Choose defaults from end-to-end workload costs, the size of
gains or losses, and resource constraints.

State the comparison implementation, workload, and method for each performance claim.
Evaluate generated readability separately from machine-code performance and measure
compiler selection with runtime execution for whole-operation claims. Kernel
measurements establish kernel costs. Preserve correctness with optional optimization
disabled.

## Generate idiomatic C++

Source `class` and `struct` declarations establish nominal identity, fields, and
access rules. Choose C++ carriers and layout from semantic facts, preserving source
semantics and addressability. Native interfaces constrain exposed representations
and operations.

Express resolved types, construction, access, and control flow through ordinary
C++ facilities. Add storage and control where sequencing, failures, or lifetimes
require them; use shared runtime operations where they keep generated uses small.
Evaluate clarity, runtime cost, and native compilation cost together.

Before emission, compute known parts, select runtime policies, construct into the
required destination, and retain only necessary execution and storage. The C++
compiler owns general range, loop, and interprocedural optimization and machine-code
selection.

## Optimization analysis boundary

Optional analysis uses structured semantic operations for a concrete implementation
choice. Define its fact domain, invalidation rules, and stopping condition; facts
identify their subject and validity domain. Writes and calls invalidate assumptions
they may change, and joins retain facts established on every reachable incoming
path. Unknown facts retain the ordinary operation.

Ownership, nullability, types, and failures have their own correctness analyses
whose published facts can guide implementation selection. Required semantic checks
and static execution follow their language contracts.

## Build on the C++ ecosystem

Evaluate native foundations for behavior, maturity, performance, portability,
and fit with the intended operation. Define source behavior, access, and lifetime
requirements before selecting a native implementation. Model native details
where a language rule depends on them.

## Language mechanisms and library composition

| Component | Responsibility |
| --- | --- |
| Standard Craft | Public declarations and reusable source implementations |
| Builtins | Types and operations whose semantics the compiler owns |
| Runtime Craft | Native representations and operations for generated programs |

A capability can use all three: public API ownership, compiler knowledge, and native
implementation are separate decisions.

### Public APIs

Organize standard APIs around capabilities callers need. Choose names, defaults,
and type-associated methods from ordinary use. Decide public entries, declaration
owners, implementation locations, and execution capabilities separately.
Keep related public types, errors, and operations at the shortest module path
identifying their capability. Require an independently useful contract or vocabulary
for each public submodule. Split implementations by algorithms and dependencies;
implementation splits do not require additional public entries.
Apply these criteria to source and builtin APIs alike.

Defaults serve pervasive language types and common entry operations, independently
of builtin ownership. Inference and propagation can use result and failure types
without binding their names; explicit names follow ordinary lookup and import rules.

### Ownership selection

Choose builtin ownership with these checks:

- Identify the type, access, lifetime, failure, and execution knowledge the compiler
  must own.
- Check whether existing source operations naturally express the complete contract.
- Assess whether compiler knowledge simplifies use or removes repeated analysis,
  checks, conversions, storage, or calls.
- Assess the library-specific policy and maintenance added to the compiler.

Method syntax, operand count, algorithm size, and source expressibility do not
individually determine ownership. Choose compiler ownership when benefits to API
use, checking, or execution justify its added library policy and maintenance.
Associated entries can select source bodies or builtins. Builtins share the
language's type, access, ownership, lifetime, and failure models; ordinary async
functions and builtin composition both produce the builtin `Operation` type.

Choose ownership by the contract being implemented:

- Prefer source declarations for ordinary public data contracts, including domain
  errors, result enums, and interval values. Builtins can use these declarations
  through ordinary type rules. Duration literals, numeric parsing, and timeout
  composition use standard-source declarations.
- Prefer reusable source bodies when existing operations naturally express the full
  contract, including effects, recovery, cancellation, and retained resources.
  JSON, text processing, and framing use ordinary source functions; SIMD traversal
  and scanning compose builtin operations in source.
- Keep host state and completion rules together when they need one execution
  boundary. Algorithms and host operations can remain builtin for a simpler API
  and execution model. Library-specific native adapters belong to their Craft.

`print` uses known argument types and order for heterogeneous printing; `when_all`
derives result and failure types and checks ownership transfer. Their bounded APIs
provide these capabilities without general variadic or higher-order machinery.
Scalar and SIMD floating methods share a bounded numeric contract even where source
comparisons and selection can express their bodies. Evaluate compiler ownership by
the complete contract and its costs in each case.

### Execution and native integration

Select native implementations and optimizations independently of API ownership.
Inspect generated C++ and native code for required work and removable intermediates.

Apply ordinary source discovery and semantic rules to standard and user modules.
Give each module one canonical identity, including its source and compiler-provided
declarations.

Verify that static execution, interpretation, and native generation preserve the
same values, evaluation order, effects, ownership, and failures within supported
stages. Check agreement across representations and host adapters; keep execution
context, host resources, and result retention explicit.

Collect each Craft, including native adapters, as one source package. Check
execution capability at the operation that needs it, independently of native source
presence. Native initialization, cleanup, and validity belong to the interoperation
contract; arbitrary C++ effects remain outside semantic execution. Keep the
semantic core independent of Craft-specific APIs. Execution frontends bind and
link the native providers they support; compiled programs discover standard-source
dependencies through the same rules as other Crafts.

## Semantic authority and native boundaries

Carven defines values, evaluation order, access, ownership, lifetimes, and failures
for an explicit source batch. Validate analyzable conditions before publication;
preserve these semantics and required runtime checks through lowering and execution.

Give each semantic fact one authority. Analysis publishes validated facts with
their owner; lowering selects their realization; runtime performs the remaining
work. Consumers use these facts directly without requiring source to restate its
structure. Generated names and text represent the output. Repeated local exceptions
call for reviewing the owning contract and consolidating equivalent paths there.

External C++ bodies remain outside Carven analysis. Carven checks native-call
arguments, access, and known lifetime relationships; C++ resolves delegated
declarations, types, and operations. Providers and callers satisfy external behavior
and storage contracts. Native results establish only relationships supported by
the boundary's semantic model.

At native boundaries, assign responsibilities to Carven, providers, and callers:
delegated checks, validity and lifetime assumptions, failures, and environment
dependencies. Address retention, returned aliases, and reentry explicitly. Place
native exception recovery before boundaries where escaping exceptions terminate
execution. Use native traits for delegated type requirements.

Assign dependency acquisition, additional source selection, and native build
configuration to build systems.

## Require only what an operation needs

Generated steps require only construction, assignment, and invocation operations
needed by the source contract, including in temporaries, wrappers, and intermediate
storage. Read access should not require user-defined copying. Initialize destinations
directly to avoid unnecessary default construction or assignment, and realize
ownership transfer through the applicable constructor.

Record additional implementation requirements as limitations in the owning
reference. Changes to admitted types belong in the language contract.

## Practical safety

Carven implements its semantics and associated checks. Specify each operation's
valid uses, checked conditions, and caller obligations. Authors choose algorithms
and operations that meet functional requirements; callers satisfy API preconditions
and external validity obligations. Validate functional behavior through tests and
operation-specific checks; applications define recovery policies.

Use static checks where sufficient facts exist and runtime checks where required.
Weigh prevented errors against user complexity, runtime costs, and integration costs.

State what each check proves and assumes: non-null checking establishes an address
condition; the external owner establishes object liveness. Match safety claims to
these boundaries, diagnose violations there, and distinguish termination from
recoverable failure.

## Simplicity and feature admission

Give each intent a direct expression through composable rules. Additional forms
need a meaningful difference in behavior or contract. Keep tokens, delimiters, and
syntactic structure consistent across scopes.

Require a concrete use that existing forms cannot adequately express. Evaluate its
rules, exceptions, interactions, compilation and runtime costs, and tooling burden.
Settle meaning before syntax. General mechanisms such as higher-order interfaces,
parameter packs, or generic composition need independent use cases to justify their
shared rules; reimplementing a builtin alone is insufficient. Add extension points
for actual consumers, defining admitted uses and diagnostics at the owning boundary.

Judge simplification by clearer responsibilities, fewer interacting rules and
execution paths, and preserved contracts. Distinguish it from changes to behavior,
admitted programs, or caller obligations. Reduced code size is a useful consequence;
update names and owning boundaries to match the resulting responsibilities.
