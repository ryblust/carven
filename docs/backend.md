# C++ generation

This document describes how published semantics become C++ artifacts through
planning, representation selection, lowering, dependency collection, and emission.

## Pipeline

```text
SemIRProgram + TargetPlanningRequest
  → PlannedCompilation
  → lower_artifact
  → TargetUnit
  → emit
  → GeneratedArtifactSet
```

`PlannedCompilation` owns a sealed semantic program and its matching immutable
`TargetPlan`. Planning selects names, interfaces, failure representation, and
artifact schedules. Each artifact is lowered into a fresh `ArtifactLowering`
and target-unit identity. Repeated lowering produces independent units.

`TargetUnitBuilder::finish` verifies the target tree, derives dependencies, and
materializes directives. Rendering serializes that finished tree. Filesystem
output and native compilation are separate consumers.

## Representation

The backend expresses resolved operations using C++ types, constructors,
references, scopes, calls, and control flow. C++ supplies object layout, template
instantiation, special-member selection, ABI, and native optimization.

Read parameters, Read argument temporaries, and Read array-range bindings use
`runtime::ReadArg<T>` under the
[native parameter policy](toolchain.md#read-parameter-realization). Its trait
queries require complete definitions, which interface planning includes.

Write parameters use `T&`; Take parameters own a `T`. `runtime::transfer` exposes
a mutable owner's value for construction: trivial values are read, other values
are supplied as rvalues. It returns a reference and adds no intermediate owner.
C++ selects the constructor. Semantic analysis enforces source availability.

The parameter policy is shared by declarations, definitions, and callable signatures.
C++ imports and export façades obey their explicit boundary signatures.

Concrete closures use named structures. Value captures are fields; Write
captures are `std::reference_wrapper<T>` fields. Their call operators are const:
value captures are read-only through the operator, and reference captures reach
their referents through `.get()`. Capture fields permit the generated default
C++ assignment operation to copy values and rebind reference targets.
Callable borrows use non-owning `FunctionRef` target descriptions.

`Outcome`, `FunctionRef`, and generated C++ bridges establish `noexcept`
boundaries. They require the constructions and invocations they perform, without
requiring those operations to declare `noexcept`. A C++ exception that escapes
such an operation terminates at the boundary; it is not converted into a Carven
failure. Outcome remains move-constructible when its alternatives permit it,
with no copy, assignment, or default construction. Function-pointer thunks
restore the original pointer type before invocation.

Bindings initialize in their natural scopes. Assignment uses C++ assignment;
the backend does not reconstruct unassignable values. Delayed construction uses
local result storage only where direct initialization cannot preserve control
flow and temporary lifetimes.

## Evaluation and values

Lowering distinguishes saved read values, owned values to deliver, and located
places. Saved values establish evaluation order; located places retain storage
identity. Only owned value delivery requests transfer. Observations do not
consume their operands.

Locals that require writes, delayed initialization, or Take remain mutable C++ storage.
Other local owners are const. Read range bindings use the Read parameter policy;
Write range bindings are mutable references. A range binding is never a Take
source. Arrays and text use C++ range-for
iteration; text is decoded sequentially without length prepasses or indexed
rescanning.

Carven evaluation is left to right and exactly once. Temporaries preserve that
order when a direct C++ expression would not. Short-circuit evaluation remains
inside the selected branch. Concrete closure callees retain object identity
before argument evaluation;
callable views retain a target description. Neither choice copies capture contents.
Source and full-expression scopes preserve lifetimes.

## Control

Conditionals, loops, returns, and scopes lower directly. Complex loop-condition
sequencing executes on every test; a C-style continue executes the step clauses.
A value region may initialize a destination directly or use an immediately
invoked lambda when its exits stay within that region. When lowering establishes a lexical C++ exit,
it omits subsequent operations and result transport. This includes synthetic
failure exits; inactive source still receives semantic contract checks. A complex step sequence gives
`continue` a local jump target; ordinary loops use native `continue`.

Match locates its subject once and keeps it alive and stable through selection.
Pattern owners initialize before guards. A single selection path declares its
bindings in the successful branch; multiple alternatives share binding storage
and one guard. Catch arms use the same selection lowering.

`FailureABI` gives each failure set a deterministic member order. Failing
results use `Outcome`; other results are direct. Widening accepts identity or a
strict failure-set superset. Calls, propagation, handlers, and callable
adaptation use this one contract. Handler failures go to the enclosing failure
target; rethrow preserves the selected failure. Test exit leaves the test.

A local label realizes an exit for which C++ has no suitable structured form.
Labels carry a control purpose and must respect initialization barriers. The
label remains local to the source control construct it implements.

## Names and interfaces

`TargetNamePlan` owns linkage-domain namespaces, module names, nominal names,
closure names, enum payload names, and generated test names. A callable-local
allocator reserves source names and allocates temporaries and labels.

Semantic visibility and C++ definition requirements determine interface
artifacts. Declaration-only dependencies use forward declarations. Complete
requirements form interface edges; strongly connected components share an
interface. Function declaration return types, including Outcome and arrays,
require only declarations of their component types. Object storage and Read traits require
complete definitions. Body-only calls do not merge interfaces.

Schedules own interface declarations, C++ façades, private nominal and closure
ordering, and selected tests. Ordinary module items are scanned from SemIR.
Lowering records providers of actually emitted names and types. These transient
provider sets are consumed into include directives.

Each test becomes a function. Module runners call tests in source order; the
runner header calls modules in canonical order, as specified by the
[generated test interface](toolchain.md#artifact-paths). The default entry calls
that same runner. Explicit C++ fragments preserve their bytes and source order.

## Target syntax and emission

Expressions, statements, and items are move-only recursive values. Only types
are interned. Target type IDs belong to one unit. Source attribution and
function forms use exact variants.

Type construction accepts only children already present in the same unit;
append-only insertion establishes acyclicity. Finishing validates occurrence
type references and local control transfers.
Recursive ownership establishes syntax occurrence ownership. Dependency
collection traverses the finished tree and referenced types, producing the
required standard and runtime headers. Unreferenced interned types add no
headers.

The renderer serializes nodes, directives, source mapping, and whitespace. It
does not infer language meaning or repair target syntax. Artifact collection
checks logical paths, uniqueness, and prefix safety. Unused parameter names are
omitted; unused local owners retain their initialization and lifetime.

## External names and operations

External imports lower to includes and module-scoped using declarations or
namespace directives. References preserve their binding's module identity.
External types used in interfaces carry their header and name-environment
dependencies into those artifacts.

External result queries lower to structured unevaluated C++ expressions and
`std::remove_cvref_t<decltype(...)>` owner types. Executed calls use the ordinary
argument sequencing and access lowering. Receiver access is preserved
independently of storage made mutable to realize a later Take. Discarded external
calls need no result storage, so void-returning providers remain usable.
