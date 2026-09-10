# Design principles

Carven lets programmers express intent and supplies the C++ operations, storage,
and control flow needed to carry it out. It aims to make ownership, access, and
failure easier to compose while retaining native performance and access to the
C++ ecosystem.

These principles develop the goals in Why Carven into criteria for language and
implementation decisions. They describe the design direction; the language and
toolchain references state supported behavior and current restrictions.

## Intent over mechanism

Source should state whether an operation reads, mutates, or takes ownership of a
value, what it captures, and how it handles failure. The compiler selects and
composes the constructors, references, storage, and control flow that realize
those choices. This removes routine mechanism management while keeping decisions
that affect program behavior visible to the programmer.

Expose distinctions that affect access, ownership, mutation, lifetime, control,
failure, allocation, or interoperability. Representation choices remain internal
when they preserve the promised behavior. Source-level control is needed where a
choice changes the operation the programmer requests or the guarantee it provides.

Give omitted spellings bounded rules. Type context can remove repeated type
information, and inference can derive facts from a body. Each rule states its
inputs, priority, stopping conditions, and incompatible cases. Ownership transfer,
mutable access, capture, failure handling, and retained lifetimes need explicit
source forms or documented rules at the construct that supplies them.

## Typed failure contracts

A callable's contract describes its possible failures alongside its successful
result. Composition should preserve distinct failure types and payloads so a
caller can decide what to propagate, recover from, or translate at an interface.
The same model applies to ordinary functions and callbacks.

Inference reduces repetition inside private implementations. Shared interfaces
state bounds that their implementations must satisfy. Propagation and handling
remain visible at their source positions, and failure behavior composes with
ordinary evaluation order, ownership, and cleanup.

Failure contracts bound which failures can escape and require handling to cover
the relevant cases. They preserve information for recovery decisions without
selecting a recovery policy. Propagating a failure preserves completed effects;
rollback, when required, is an application operation with its own contract.

## Zero-overhead abstractions

The cost baseline is skilled handwritten C++ preserving the same evaluation
order, ownership, lifetimes, and safety checks. Prefer direct native operations
when they express those guarantees. An abstraction should not introduce costs
unrelated to the behavior it provides, and compile-time distinctions should leave
no runtime state unless execution needs it.

Storage, allocation, indirection, checks, and dispatch need an identifiable
behavioral purpose. Compiler analysis may need information that the running
program does not; that information alone does not justify runtime storage.
Evaluate compiler time and memory separately from generated-program costs.

Generated C++ remains inspectable so representation and cost can be examined.
Performance claims require concrete comparison implementations, defined workloads,
and measurements. Correctness must hold without optional optimizer transformations.

## Build on the C++ ecosystem

Use the C++ community's libraries, tools, and expertise as foundations for Carven
facilities. A mature native capability can support a built-in language operation
when its behavior, access requirements, and lifetime boundaries are defined.
Carven can provide a coherent source interface while reusing the native
implementation behind it.

Evaluate a native foundation for its behavior, maturity, performance, portability,
and fit with the intended language operation. Reuse its implementation and
established protocols where they satisfy that operation. Reimplementing library
internals in compiler analysis needs a concrete semantic requirement.

The source facility presents the concepts needed by Carven programmers; the
underlying library supplies the implementation. This separation lets the language
benefit from native expertise without making every library detail a source concept.

## Semantic authority and native boundaries

Carven is a source-generation step in an existing native build. Header imports,
explicit native calls, and generated public interfaces connect Carven programs to
C++ providers and consumers. Native tools retain responsibility for compilation,
linking, and their build configuration.

Carven defines the meaning of its operations, including evaluation order, access,
ownership, and failures. C++ checks explicitly delegated declarations, overloads,
templates, conversions, and construction. Each boundary identifies the required
operations and the responsibilities of Carven, the provider, and the caller.
Where behavior depends on the native environment, state that dependency.

Native providers and callers own specified obligations for external object
validity, retained references, and reentry. Carven retains authority over its own
access and ownership rules at those boundaries. Native exception recovery belongs
in C++ before an exception escapes a generated `noexcept` boundary.

Keep one authority for each fact. Later compiler stages consume established
semantic facts rather than reconstructing meaning from generated names or text.
Native traits and protocols check delegated requirements. Their results establish
those native facts; they do not independently define Carven operations.

## Require only what an operation needs

An operation should require only the capabilities needed to perform it. Reading
an object should not require user-defined copying. Constructing a destination
should not require default construction and assignment merely because an
implementation chose to create empty storage and fill it later. Transferring an
owner needs the applicable construction operation, not unrelated capabilities.

This principle constrains how the compiler composes mechanisms. Temporaries,
wrappers, and storage strategies must accommodate the admitted operations and
types, including both compiler-generated and library-backed types. Evaluate the
required constructors, assignments, and other capabilities at each generated
step, including paths that introduce intermediate storage.

If the implementation imposes additional requirements, identify the affected
cases as a current limitation. Resolve the implementation or reconsider the
source contract explicitly. Implementation convenience alone does not justify
narrowing the types an operation admits.

## Practical safety

Distinguish functional correctness from correct use. An implementation's author
is responsible for whether it fulfills its purpose. The language defines the
conditions under which operations may be used, including access, ownership,
lifetime, and failure obligations. It provides necessary checks within that model;
application requirements and recovery decisions remain with their authors.

Check operation conditions where the required facts are available. Use static
checks where sufficient information exists and runtime checks where the
operation's contract requires them. The choice should
account for the errors prevented, the burden on ordinary use, and runtime and
integration costs. Clear defaults and explicit consequential operations should
make correct use straightforward, with checks preventing violations of the
conditions they can establish.

State what each check establishes and where its evidence ends. A non-null address
alone does not establish a live object, and a defined termination path provides no
recovery guarantee. Access and lifetime checks constrain storage use without
proving that an algorithm produces the intended result.

Safety claims must match enforced checks and stated assumptions. Restrictions
should identify the guarantees they provide. Diagnose violations at the boundary
that checks them and keep known limitations visible.

## Simplicity and feature admission

Give the same intent one direct, consistent expression. Additional forms need a
meaningful difference in behavior or contract; equivalent C++ mechanisms alone
do not justify separate source forms. Prefer a small set of composable rules over
parallel mechanisms and special cases that users must learn to choose between.
Parsing should depend on tokens and delimiters so a form keeps the same structure
across scopes.

A feature needs a user intent or guarantee that existing forms do not adequately
express. Technical feasibility alone is insufficient. Evaluate the rules,
exceptions, interactions, runtime costs, and tooling work it introduces. Its
benefit must justify the burden on both programmers and implementers.

Keep each supported scope coherent across its boundaries. Reject unsupported
forms and identify the rejection boundary. Syntax follows settled meaning;
speculative extension points need an actual consumer. Internal refactoring does
not require preserving obsolete representations, while compatibility obligations
need an identified interface and consumer.
