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
and target-unit identity. Repeated lowering produces independent units. Type and
signature-result caches each use one slot containing Unseen, Resolving, or a
completed TargetTypeID.

`TargetUnitBuilder::finish` verifies the target tree, derives dependencies, and
materializes directives. Rendering serializes that finished tree. Filesystem
output and native compilation are separate consumers.

## Representation

The backend expresses resolved operations using C++ types, constructors,
references, scopes, calls, and control flow. Carven determines source evaluation
order, access, lifetimes, and exit destinations. C++ performs overload resolution,
object construction, copy elision, scope cleanup, layout, and native optimization.
Lowering preserves the types, value categories, and initialization syntax supplied
to those C++ operations.

Read parameters, Read argument temporaries, and Read array-range bindings use
`runtime::ReadArg<T>`, which selects a const value for trivially copy-constructed
and destroyed types and a const reference otherwise. Its trait
queries require complete definitions of value representations, which interface
planning includes. A pointer representation is complete without completing its
target; pointer dependencies request target declarations. Forming that target's
type expression can still require complete definitions, such as Read parameter
types in a callable signature. Declaration ordering includes these requirements;
cycles in type formation remain C++ errors.

Write parameters use `T&`; Take parameters own a `T`. `runtime::transfer` exposes
a mutable owner's value for construction: trivial values are read, other values
are supplied as rvalues. It returns a reference and adds no intermediate owner.
C++ selects the constructor. Semantic analysis enforces source availability.

`ptr<T>` lowers to a pointer to `std::add_const_t<T>` and `ptr<&T>` to a
pointer to T, composed per layer for nested ptr values. Read ptr operands
snapshot the address before later operands can replace its slot. Dereference
selects that saved address before evaluating the rest of a store or call. Typed
null constants retain the complete pointer type in native overload resolution.
Native adoption uses typed initialization, preserving C++ conversion checks;
there is no runtime pointer wrapper or automatic resource cleanup.

The parameter policy is shared by declarations, definitions, and callable signatures.
C++ imports and export façades obey their explicit boundary signatures.

Concrete closures use named structures with const call operators defined in the
source artifact. Closures exposed by function result types publish their layouts
in the generated interface. Noncapturing closures adapt to callable views without
borrowing an object; the source closure expression is evaluated once.
Value captures are fields and are read-only through the call operator. Write
captures are `std::reference_wrapper<T>` fields whose referents are accessed
through `.get()`. Capture fields permit the generated default
C++ assignment operation to copy values and rebind reference targets.
Callable borrows use non-owning `FunctionRef` target descriptions.

Generated function bodies, closure call operators, evaluation lambdas, import
bridges, and export façades use unconditional `noexcept` specifications.

`FunctionRef` invocation and `Outcome` payload construction and movement also
establish `noexcept` boundaries. Their constraints require the constructions
and invocations they perform, without requiring those operations to declare
`noexcept`. Outcome remains move-constructible when its alternatives permit
it, with no copy, assignment, or default construction. Function-pointer thunks
restore the original pointer type before invocation.

Bindings initialize in their natural scopes. Assignment uses C++ assignment;
the backend does not reconstruct unassignable values. Delayed construction uses
local result storage only where direct initialization cannot preserve control
flow and temporary lifetimes. `runtime::DeferredStorage<T>` owns such storage;
a generated placement-new expression constructs `T` directly, and the storage
destroys the object at scope exit.

## Evaluation and values

Function-body lowering composes `Lowered<T>` results containing target statements,
a normal result, and owned control exits. A normal result retains an expression
or records that evaluation is complete. Retained expressions may have void type;
completed evaluation is distinct from absence of a normal successor. Regions
deliver a result on each normal path, including paths without a tail expression.
Result destinations are function return, local lambda yield, final-storage
initialization, discard, and boolean-result assignment. Discard
executes any retained expression. Owned and temporary results preserve storage
identity and observation or transfer behavior.

Function-body completion finalizes parameter names and local declaration attributes
from references in the retained target tree. Lexical scopes distinguish declaration
occurrences; evaluation lambdas can reference outer declarations. Unreferenced
parameters lose their names. Source bindings and operand temporaries start marked
`maybe_unused`; references outside direct assignment and update targets clear the
attribute. Completion preserves initialization and lifetime. Source unused
diagnostics belong to semantic analysis.

Scope construction owns auxiliary storage and preserves semantic lifetimes.
Initialization stays at its execution point, including conditional paths. Native
bodies provide scopes; independent regions with declarations require a block.
Statement composition and source attribution do not create scopes.

Locals that require writes, delayed initialization, or Take remain mutable C++ storage.
Other local owners are const. A local requiring deferred initialization initializes
its final storage directly.
Read range bindings use the Read parameter policy;
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

Operand composition uses prepared execution and storage-read facts together with
C++ sequencing guarantees. It lowers operands in source order once and constructs
the operation from their results. A value branch delivers a value through a local
lambda or an explicit destination. Each following operand and statement is lowered
once. Operand access determines Read snapshots and Write aliases; calls select
their callee before arguments, and short-circuit operators evaluate only the
selected side.

Conditional full-expression temporaries reserve storage in construction order at
the enclosing expression boundary and initialize only on the selected path.
Reverse destruction order includes those objects and any retained Outcome owners.
Expression-position lexical regions retain their separate cleanup boundary.
Known or discarded results retain required execution.
Result demand determines whether a call checks success, observes its payload, or
transfers ownership. Numeric operations preserve their resolved type across
promotion, overload, and deduction boundaries; the renderer does not infer types.

## Control

Conditionals, returns, and scopes lower directly. Ordinary loops use an
initializer scope and a while loop; condition sequencing executes on every
test. A loop with steps gives continue a local step target during construction.
Known consumers receive results directly, including returns from selected branches.
Expression-position value branches with no outward failure or test exit use local
value lambdas; branches with those exits use deferred initialization. Function
return applies the failure ABI independently of lambda yield. A retained void
expression can be returned directly; an Outcome return executes it before
constructing success without a payload. Non-void success uses `Outcome::success_from(factory)`: the factory is
invoked immediately and exactly once to construct the payload directly. Callable
adaptation uses the same construction. A factory may return an immovable prvalue.
Owner transfer, payload extraction, and Outcome widening require the constructors
used by those operations. Void and discarded regions need no result storage.
Loops and handlers receive only exits belonging to their own construct. Loops
without steps and range loops use native `continue`.

Match locates its subject once and keeps it alive and stable through selection.
Pattern owners initialize before guards. A single selection path declares its
bindings in the successful branch; multiple alternatives share binding storage
and one guard. Catch arms use the same selection lowering.

`FailureABI` gives each failure set a deterministic member order. Failing
results use `Outcome`; other results are direct. Widening accepts identity or a
strict failure-set superset. Calls, propagation, handlers, and callable
adaptation use this one contract. Handler failures go to the enclosing failure
target; rethrow preserves the selected failure. Test exit leaves the test.

The process entry wrapper calls the Carven entry exactly once. An infallible
entry's ordinary result is discarded and the wrapper returns zero. For a
nonempty declared failure contract, the wrapper owns the returned `Outcome`,
checks `success_if()`, and returns zero or `EXIT_FAILURE` from `<cstdlib>`.
The wrapper's result storage undergoes ordinary scope cleanup.

A local label realizes an exit for which C++ has no suitable structured form.
Labels carry a control purpose and must respect initialization barriers. The
label remains local to the source control construct it implements. Region exits
record use when emitting a jump. Only their owning construct can restore an
entry; lowering never recovers continuation by scanning generated statements.

## Names and interfaces

`TargetNamePlan` owns linkage-domain namespaces, module names, nominal names,
closure names, enum payload names, and generated test names. A callable-local
allocator reserves source names and allocates temporaries and labels. The plan
owns encoded public namespace and function names shared by API headers and
export façades. Artifact paths retain canonical source names.

Semantic visibility and C++ definition requirements determine interface
artifacts. Declaration-only dependencies use forward declarations. Complete
requirements form interface edges; strongly connected components share an
interface. Function declaration return types, including Outcome and arrays,
require only declarations of their component types. Object storage and Read traits require
complete definitions. Body-only calls do not merge interfaces.

Schedules own the ordered interface definitions and function declarations, C++
façades, private nominal and closure ordering, and selected tests. Ordinary module items are scanned from SemIR.
Lowering records providers of actually emitted names and types. These transient
provider sets are consumed into include directives.

Each test becomes a function. Module runners call tests in source order; the
runner header calls modules in canonical order. The default entry calls
that same runner. Explicit C++ fragments preserve their bytes and source order.

## Target syntax and emission

Expressions, statements, and items are move-only recursive values.
`TargetBlockStmt` always denotes an actual C++ block; plain sequences are
composed before publication. Only types are interned. Target type IDs belong to one unit. Source attribution and
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
checks logical paths, uniqueness, and prefix safety.

## External names and operations

Semantic name resolution expands explicit C++ selections into globally rooted
paths while retaining the provider module's header environment. External names
lower through one shared path for values, named types, and type queries. Global
lookup produces a root-qualified C++ path. Opened namespace lookup prefixes the
path with the context module's generated namespace. Both record a declaration
environment requirement; opened namespace lookup also requires the using environment.

Environment requirements are independent of semantic lookup modes and merge
idempotently, with using requirements including declarations. Lowering owns
their materialization into ordered includes and using declarations.
Nested types and result queries carry their environments into consuming
artifacts; dependencies are not inferred by matching symbols to headers.

External result queries directly construct the expression owned by
`TargetDeducedType`, rendered as `std::remove_cvref_t<decltype(...)>`. Its restricted
query shapes have structural equality for type interning; general target
expressions remain move-only and have no equality protocol. Executed calls and type queries
consume the explicit semantic callee and operand access. Executed calls use
ordinary argument sequencing and access lowering. Receiver access is preserved
independently of storage made mutable to realize a later Take. Discarded external
calls need no result storage, so void-returning providers remain usable.

C string literal operations have an intrinsic external `const char*` type.
Lowering emits byte-escaped narrow literal storage with `static_cast<const char*>`,
preserving pointer semantics for overload resolution and deduction. Ordinary
string literals retain `std::string_view` realization.
