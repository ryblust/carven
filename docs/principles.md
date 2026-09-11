# Design principles

Carven lets programmers express intent and supplies the C++ operations, storage,
and control flow needed to carry it out. These principles guide design
decisions; the language and toolchain references describe supported behavior.

## Intent over mechanism

Source expresses access, ownership transfer, captures, and failure handling. The
compiler selects constructors, references, storage, and control flow that
preserve those choices. Expose representation choices when they change
observable behavior or a caller's obligations. Access, mutation, lifetime,
control flow, failure, allocation, and interoperation are relevant distinctions.

Defaults and inference have bounded rules: state their inputs, priority,
stopping conditions, and incompatible cases. Consequential operations need an
explicit source form or a rule at the construct that supplies them. This applies
to ownership transfer, mutable access, capture, failure handling, and retained
lifetimes.

## Typed failure contracts

A callable's contract describes its possible failures alongside its successful
result. Composition preserves distinct failure types and payloads for
propagation, recovery, or translation. Ordinary functions and callbacks use the
same model.

Private implementations may infer failure sets. Shared interfaces state bounds
checked against their bodies. Propagation and handling remain visible in source;
handlers cover the failures they consume. Recovery policy belongs to the caller.
Failure handling composes with evaluation order, ownership, and cleanup.
Completed effects remain on failure; rollback is an application operation with
its own contract.

## Zero-overhead abstractions

The cost baseline is skilled handwritten C++ preserving the same evaluation
order, ownership, lifetimes, and safety checks. Prefer direct native operations
that express those guarantees. Runtime storage, allocation, indirection, checks,
and dispatch each serve an identifiable behavioral purpose. Compile-time facts
remain compiler data unless execution requires their runtime representation.

Evaluate compiler time and memory separately from generated-program costs.
Generated C++ remains inspectable. Performance claims require concrete
comparison implementations, defined workloads, and measurements. Correctness
holds with optional optimizer transformations disabled.

## Build on the C++ ecosystem

Evaluate native foundations for behavior, maturity, performance, portability,
and fit with the intended operation. Reuse established implementations and
protocols when their access requirements and lifetime boundaries satisfy the
Carven contract.

A mature native capability can support a builtin Carven operation when its
behavior, access requirements, and lifetime boundaries are defined. Carven
presents the source concepts; the native library supplies the delegated
implementation. Compiler analysis models native details only when a source
semantic requirement needs those facts.

## Semantic authority and native boundaries

Carven defines evaluation order, access, ownership, and failures. It compiles an
explicit source batch into artifacts. The build system obtains dependencies,
collects sources, configures native inputs, and compiles and links those
artifacts. Build integration automates the same explicit compilation available
through the CLI.

C++ checks delegated declarations, overloads, templates, conversions, and
construction. Each boundary states the responsibilities of Carven, the provider,
and the caller. Native providers and callers own external object validity,
retention, returned aliases, and reentry. Native exception recovery occurs in
C++ before an exception escapes a generated `noexcept` boundary. Carven checks
its own access and ownership rules across these calls. Document
native-environment dependencies where they affect behavior.

Each fact has one authority. Later compiler stages consume published semantic
facts as their source of meaning. Generated names and text are output
representations. Native traits and protocols establish the delegated native
requirements within the Carven operation's contract.

## Require only what an operation needs

An operation requires only the capabilities needed to perform it. Read access
must remain usable for objects without user-defined copying. Destination
construction uses direct initialization where default construction and
assignment are unnecessary. Ownership transfer requires the applicable
construction operation.

Apply this rule to every generated step, including temporaries, wrappers, and
intermediate storage. Their construction, assignment, and invocation
requirements must preserve support for the types admitted by the source
operation.

When implementation adds a requirement, document the affected cases as a current
limitation and resolve the implementation or source contract explicitly. Changes
to the admitted types require a source-contract decision.

## Practical safety

The implementation's author is responsible for functional correctness: whether
it fulfills its purpose. Correct use concerns access, ownership, lifetime, and
failure obligations. Carven checks the conditions represented in its model;
application requirements and recovery policy remain with their authors.

Use static checks where sufficient facts exist and runtime checks where the
operation's contract requires them. Weigh the errors prevented against the
burden on ordinary use, runtime costs, and integration costs. Clear defaults and
explicit consequential operations should make correct use straightforward.

State what each check establishes and the assumptions it requires. For example,
non-null checking establishes an address condition; the external owner
establishes object liveness. Termination defines how an invalid operation stops;
recovery requires a separate contract. Access and lifetime checks establish
storage-use conditions; algorithmic correctness remains an implementation
responsibility.

Safety claims match enforced checks and stated assumptions. Diagnose violations
at the boundary that owns the check and document current limitations.

## Simplicity and feature admission

Give each intent a direct, consistent expression. Additional forms need a
meaningful difference in behavior or contract. Prefer a small set of composable
rules. Parsing depends on tokens and delimiters, keeping a form's structure
consistent across scopes.

A feature needs a concrete intent or guarantee that existing forms cannot
adequately express. Evaluate its rules, exceptions, interactions, runtime costs,
and tooling requirements against that need. Its benefit must justify the burden
on programmers and implementers. Settle meaning before syntax and add extension
points when an actual consumer requires them.

Update internal interfaces with their callers. Each supported scope has explicit
admission rules and diagnostics at its owning boundary.
