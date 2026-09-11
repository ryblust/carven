# C++ generation

This document describes how published semantics become C++ artifacts through
planning, representation selection, lowering, dependency collection, and emission.
The input is a published semantic program. The representations here describe
the current implementation.

## Pipeline

```text
SemIRProgram + TargetPlanningRequest
  → PlannedCompilation
  → lower_artifact
      → BodyConstruction → body realization → target syntax
  → TargetUnit
  → emit
  → GeneratedArtifactSet
```

`PlannedCompilation` owns a sealed semantic program and its matching immutable
`TargetPlan`. Planning selects names, interfaces, failure representation, and
artifact schedules. Each artifact is lowered into a fresh `ArtifactLowering`
and target-unit identity. Repeated lowering produces independent units. Type and
signature-result caches belong to that artifact lowering and track incomplete
resolution separately from completed target identities.

`lower_body` owns the synchronous `construct_body` → `realize_body` sequence.
`BodyConstruction` prepares ordered operands, use contracts, execution summaries,
structured regions and control destinations. It borrows the frozen SemIR operations
and pattern identities through realization. Its expression and region IDs carry
the source `BodyID`; semantic type, binding, lifetime, failure and provenance IDs
retain their existing owners. The published semantic program outlives this data.
Ordinary operation realization reads the original resolved operation and consumes
prepared Target operands.

Construction completion runs one read-only structural check before realization.
It checks expression and region ID ownership and bounds, unique execution
ownership, execution cycles and rows without execution owners, lifetime and pattern
membership, loop target kind and body scope, protected-region failure destinations,
and the selected catch arm behind a rethrow. Control references are not execution
children: a handler is active only for its protected region, and a loop target
only for its body. Catch guards and bodies send failures to the enclosing target.
Violations retain the containing source origin when one is available. Malformed
inputs are available only through the internal testing fixture.

These checks cover relationships introduced by construction. Semantic publication
owns type legality, binding contracts, ownership and lifetime analysis. Native C++
compilation owns delegated operation and construction viability. Function return,
test exit and function failure destinations implicitly refer to the current body;
thrown types come from their values and call transport cleanup comes from the
call's lifetime.

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

Payload enum factories, storage constructors, and projections are ordinary C++
functions. Carven evaluates source constants during semantic analysis; their
uses reconstruct the normalized values through the same target operations.

Read parameters, Read argument temporaries, and Read range bindings preserve
Carven array, String, and closure storage, including storage in Carven aggregate
fields, through const references. The plan uses the resolved type-contents query
shared with ownership analysis. Native C++ types and other types use
`runtime::ReadArg<T>`, which selects a const value for trivially copy-constructed
and destroyed types and a const reference otherwise. Native template arguments
do not establish the instantiated type's storage contents. Interface planning
includes complete definitions for these trait queries. A pointer representation
is complete without completing its target; pointer dependencies request target
declarations. Forming that target's type expression can still require complete definitions, such as Read parameter
types in a callable signature. Declaration ordering includes these requirements;
cycles in type formation remain C++ errors.

Native Take query operands require `T&&`, including scalar and pointer types.
`BodyConstruction` records this as `NativeTake`, separately from Carven owning
`Consume`. Realization must preserve the complete operand type when producing
that rvalue reference. A sequencing barrier first completes the owning snapshot;
its subsequent native delivery retains `T&&`. It must not cast away constness
introduced by a different transfer policy.

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

Native Read pointer operands preserve the pointer type and const-lvalue category
used by C++ type queries. Adaptation isolates the selected address from subsequent
slot writes, using storage when sequencing requires it.

Source-owned writable-pointer declarations retain explicit pointee access. Lowering
records this declaration contract; emission attaches its const-correctness
annotation. Backend operand storage and projections receive ordinary analysis.

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
flow and temporary lifetimes. `runtime::DeferredResult<Result>` initializes once
from a typed factory. Value results construct directly in owned storage and are
destroyed at scope exit; reference results retain their referent's identity.
The runtime owns placement construction. Generated factories return complete
initializers, preserving explicit construction and copy-initialization rules.

Native call results retained for borrowing use deferred storage with the exact
queried return type. Factory deduction is checked against that query, accounting
for C++ dropping top-level cv from scalar call results. Exact reference retention
applies to call results. Owning snapshots instead use the normalized object type.

`Consume` completes an owning value at its source evaluation point. When a
sequencing boundary requires storage, typed initialization of the normalized
object copies a native `T&` or `const T&`, moves from `T&&`, and directly constructs
a prvalue. Later mutation of a borrowed source cannot change that completed
snapshot. `NativeTake` additionally retains the queried rvalue argument category.
Borrowing a native call result and acquiring an owning value therefore have
different storage and delivery contracts.

## Evaluation and values

Function-body lowering composes `Lowered<T>` results containing target statements,
a normal result, and owned control exits. A normal result retains an expression
or records that evaluation is complete. Retained expressions may have void type;
completed evaluation is distinct from absence of a normal successor. Regions
deliver a result on each normal path, including paths without a tail expression.
Result destinations are function return, local lambda yield, final-storage
initialization, and discard. Discard preserves required execution without
extracting an unused success payload. Operand realization determines storage
identity and observation or transfer behavior before composing the result.

Function-body completion derives parameter names and local unused attributes
from the retained target tree while preserving initialization and lifetime.
Source unused diagnostics belong to semantic analysis.

Scope construction owns auxiliary storage and preserves semantic lifetimes.
Initialization stays at its execution point, including conditional paths. Native
bodies provide scopes; independent regions with declarations require a block.
Statement composition and source attribution do not create scopes.

Locals that require writes, delayed initialization, or Take remain mutable C++ storage.
Other local owners are const. Named nontrivial values preserve copy access at
C++ return sites. Take parameters retain value storage even when their bodies
only read it. A local requiring deferred initialization initializes its final
storage directly.

Read range bindings use the Read parameter policy;
Write range bindings are mutable references. A range binding is never a Take
source. Sequence sources use explicit Read or Write borrowing during realization.
Arrays, slices, and text use C++ range-for iteration; text decodes UTF-8 in one
sequential pass.

Carven evaluation is left to right and exactly once. Temporaries preserve that
order when a direct C++ expression would not. Short-circuit evaluation remains
inside the selected branch. Concrete closure callees retain object identity
before argument evaluation;
callable views retain a target description. Neither choice copies capture contents.
Source and full-expression scopes preserve lifetimes.

`BodyRealizer::ExpressionBuilder` composes private recipes from prepared operations.
A recipe retains an unevaluated operation and its operands, or a completed target
expression or stable result. Recipes are temporary realization state. Operation
emission consumes their prepared operands through `realize_operation`.

Recipes borrow the ordered operand contracts from body construction. Completion
produces a saved result, a residual target expression, or completed evaluation
without a residual value. Prepared summaries borrow stable semantic nodes and
end with body lowering.

Composition uses execution and storage-read facts with C++ sequencing guarantees.
An actual sequencing or control boundary completes preceding recipes in source
order. Storage access preserves scalar Read snapshots and Read aliases to owned
storage. Write operands retain aliases; callees are selected before arguments. Structured regions deliver through explicit
result destinations. Completion without a normal successor stops operand
composition. Known short-circuit conditions select execution paths while retaining
the condition's required execution.

Sequencing completes preceding effects and storage reads in source order.
Independent scalar results can use ordinary local initialization; borrowed places
retain aliases. Storage selection must preserve the source cleanup frame.

Expression frames use the existing `LifetimeRegionID`. Conditional execution
that shares a full-expression lifetime uses the same frame; an expression-position
lexical region retains its own frame. Frames separate storage declarations from
initialization, reserving temporary storage in construction order at the enclosing
expression boundary and initializing only on the selected path.
Reverse destruction order includes those objects and any retained Outcome owners.
Known or discarded results retain required execution.
Fallible calls check success before continuing. Result demand controls whether
the success value is observed, transferred, or discarded. Numeric operations
preserve their resolved type across promotion, overload, and deduction boundaries;
the renderer does not infer types.
Discard demand removes unneeded pure results through the same expression lowering
that handles retained results. Short-circuit control has one construction path;
execution and lifetime requirements remain active when the result is discarded.

An independent full-expression root call can initialize an ordinary Outcome local
when it shares no retained storage or cleanup frame with other operands. Nested
calls and conditional execution use deferred storage where needed. Both paths
share failure dispatch and success delivery, and preserve reverse destruction
order among all retained owners.

## Extending operations

Semantic analysis publishes the operation's type, evaluation and lifetime contracts.
For ordinary operations using existing contracts, the backend describes ordered
inputs and uses in `construction/operands.cpp` and realizes C++ syntax in
`realization/operation.cpp`. Both visitors enumerate the semantic expression
alternatives explicitly. Native operation realization also enumerates the
`CppOperation` alternatives. Structured control and array adoption retain their
specialized realization paths.

Tests must establish the new operation's behavior and interactions with existing
contracts. Scheduling, storage and cleanup consume those contracts. New control
scopes, ownership modes or partial-object lifetimes require design and checks at
their owning boundaries.

Aggregate operands may be completed in separate storage before the final
initializer consumes them. An immovable native component saved across a failure
barrier cannot be transferred into the final aggregate; C++ rejects that generated
construction. Partial-object initialization and cleanup are not represented by
the existing operand storage contract.

## Control

Conditionals, returns, and scopes lower directly. Ordinary loops use an
initializer scope and a while loop; condition sequencing executes on every
test. A loop with steps gives continue a local step target during construction.
Known consumers receive results directly, including returns from selected branches.
Expression-position value branches with no outward failure or test exit use local
value lambdas; branches with those exits use deferred initialization. Function
return applies the failure ABI independently of lambda yield. A retained void
expression can be returned directly; an Outcome return executes it before
constructing success without a payload. Non-void success uses `Outcome::success_from(factory)`:
the factory is invoked immediately and exactly once to construct the payload directly. Callable
adaptation uses the same construction. A factory may return an immovable prvalue.
Owner transfer, payload extraction, and Outcome widening require the constructors
used by those operations. Void and discarded regions need no result storage.
Loops and handlers receive only exits belonging to their own construct. Loops
without steps and range loops use native `continue`.

Match locates its subject once and keeps it alive and stable through selection.
`PatternRealizer` traverses the published SemIR patterns in source order without
expanding combinations of alternatives. Partial matches record binding addresses;
the successful arm constructs its bindings and evaluates its guard once. Catch
arms use the same realization.

`FailureABI` gives each failure set a deterministic member order. Failing
results use `Outcome`; other results are direct. Widening accepts identity or a
strict failure-set superset. Calls, propagation, handlers, and callable
adaptation use this one contract. Handler failures go to the enclosing failure
target; rethrow preserves the selected failure. Test exit leaves the test.
Outcome and handler-variant failures share one typed dispatch construction;
their source forms determine the payload projection.

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
owns the immutable module reservations borrowed by these local allocators. It
owns encoded public namespace and function names shared by API headers and
export façades. Artifact paths retain canonical source names.

Semantic visibility and C++ definition requirements determine interface
artifacts. Declaration-only dependencies use forward declarations. Complete
requirements form interface edges; strongly connected components share an
interface. Function declaration return types, including Outcome and arrays,
require only declarations of their component types. Object storage and Read
traits require complete definitions. Body-only calls do not merge interfaces.

Schedules own the ordered interface definitions and function declarations, C++
façades, private nominal and closure ordering, and selected tests. Ordinary module
items are scanned from SemIR. Lowering records providers of actually emitted names and
types. These transient provider sets are consumed into include directives.

Each test becomes a function. Module runners call tests in source order; the
runner header calls modules in canonical order. The default entry calls
that same runner. Explicit C++ fragments preserve their bytes and source order.

## Target syntax and emission

Expressions, statements, and items are move-only recursive values.
`TargetBlockStmt` always denotes an actual C++ block; plain sequences are
composed before publication. Only types are interned. A construction-time index
selects candidates for structural equality; type storage and IDs retain insertion
order. Target type IDs belong to one unit. Source attribution and function forms
use exact variants.

Type construction accepts only children already present in the same unit;
append-only insertion establishes acyclicity. Finishing validates occurrence
type references and local control transfers.
Target verification does not recheck Carven evaluation order, object lifetime
semantics, or C++ overload and constructor feasibility. Realization preserves
those input contracts through operand use, frame ownership, result destinations
and failure dispatch, with local invariants and generated-program tests.
Recursive ownership establishes syntax occurrence ownership. Dependency
collection traverses the finished tree and referenced types, producing the
required standard and runtime headers. Unreferenced interned types add no headers.

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

External result queries use `TargetDecltypeType` to preserve the queried
expression's type and value category as `decltype((...))`. Semantic object types
explicitly wrap that query in `std::remove_cvref_t`; normalization belongs to
representation selection. Restricted query shapes have structural equality for
type interning; general target expressions remain move-only and have no equality
protocol. Executed calls and type queries
consume the explicit semantic callee and operand access. Executed calls use
ordinary argument sequencing and access lowering. Receiver access is preserved
independently of storage made mutable to realize a later Take. Discarded external
calls need no result storage, so void-returning providers remain usable.

C string literal operations have an intrinsic external `const char*` type.
Lowering emits byte-escaped narrow literal storage with `static_cast<const char*>`,
preserving pointer semantics for overload resolution and deduction. Ordinary
string literals retain `std::string_view` realization.

## Owning text realization

Builtin String lowers to the owner in `string.hpp`, with private `std::string`
storage. The shared Read storage policy preserves caller aliasing for named owners and
projected fields/elements across sequencing and failure barriers. Text operations
select runtime factories and members through target syntax and record their
support-header dependencies. Write receivers remain places; range projections
borrow through `as_str` and the UTF-8 views.

Native byte storage enters through `String::from_utf8`, which validates UTF-8
before adopting it.

`SemFormat` lowers to the runtime `format` entry in `format.hpp`. Its normalized
constant becomes a compile-time `std::string_view` with an explicit byte length.
Realization completes the ordered Read operands before invoking the formatter.
Inside that call, String aliases provide text views and `char` values encode to
UTF-8 Strings. C++ checks `std::format_string` and formatter availability; source
directives attribute those diagnostics to the interpolation. The runtime entry
passes the formatted buffer through `String::from_utf8` and is `noexcept`.

Ownership analysis establishes borrowing validity before lowering. Runtime views
carry no owner metadata. String owners, pending operands, retained range sources,
closure captures, and Outcome payloads use ordinary construction and cleanup frames.
