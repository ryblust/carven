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
      → BodyPreparation → fragment realization → target syntax
  → TargetUnit
  → emit
  → GeneratedArtifactSet
```

`PlannedCompilation` owns a sealed semantic program and its matching immutable
`TargetPlan`. Semantic type contents remain owned by `SemIRProgram`.
Planning selects names, interfaces, failure representation, and
artifact schedules. Each artifact is lowered into a fresh `ArtifactLowering`
and target-unit identity. Repeated lowering produces independent units. Type and
signature-result caches belong to that artifact lowering and track incomplete
resolution separately from completed target identities.

`lower_body` prepares a published semantic body and finishes `BodyRealizer` while
that preparation remains alive. `BodyLoweringInputs` supplies parameter identities,
capture member names, and the exit contract; `LoweredBody` returns target statements
and referenced-parameter facts.

`BodyPreparation` borrows semantic expression occurrences, stores subtree execution
and storage-observation summaries, and prepares operand demands and operation plans
on request. Each fragment owns its operation preparation. SemIR owns types,
lifetimes, origins, constants, effects, patterns, and structured control flow.
Propagation markers select their operand operation. Summary queries require an
occurrence from the prepared body. The published program outlives realization.

Builtin numeric, Boolean, and character Read parameters own immutable copies, so
later operand execution needs no snapshot of them. Owners and Take parameters can
expose native `T&&`; captures can change when a callback replaces the enclosing
closure. These bindings, Write parameters, aggregates, and native values retain
storage-observation obligations across later execution.

`BodyRealizer` traverses those structured regions directly. A failure receiver is
active only in its protected region; a loop target is active only in its body.
Catch guards and bodies send failures to the enclosing receiver. The active catch
retains the payload and accepted failure set needed by a rethrow.

Within `realization/`, `expr` owns cleanup-frame entry and result delivery;
`fragment` builds and composes completed operands, and `writer` handles repeated
format operands. The `sequencing`, `storage`, and `call` slices preserve execution
order, retain borrowed results, and complete fallible calls. Semantic publication
owns source legality and lifetime contracts; native compilation checks delegated
C++ operations.

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

Representation selection expresses published facts through literals, concrete
types, template arguments, and specialized runtime entries.
Target calls carry ordered type or boolean/integer literal template arguments.
These describe native C++ syntax, with type dependencies visited normally.

Payload enum factories, storage constructors, and projections are ordinary C++
functions. Carven evaluates source constants during semantic analysis; their
uses reconstruct the normalized values through the same target operations.

Required constant calls and direct constant expressions arrive as completed
semantic values. Finite floating values use round-tripping literals; nonfinite
values lower to typed `std::bit_cast` calls from their stored integer bits, with
ordinary target-symbol and type dependencies. Emission only serializes those
constructed expressions. Text constants become byte literals with explicit lengths,
including internal NUL. Fixed arrays and structs become typed initializers of
their completed children. These value initializers establish no source address
identity and do not extend temporary backing lifetimes.

`SliceConstant` instead requests persistent backing for its completed elements.
`backend.lowering.constant` reconstructs completed values. `ArtifactLowering`
owns one `ConstantStorage` that allocates its namespace against the module's
reserved identifiers and deduplicates backing by constant identity within
the artifact. It emits an `inline constexpr` declaration initialized by a typed
`std::array` and realizes the slice as `runtime::as_slice` of that named array.
Generated names distinguish linkage domains, source modules, and artifacts;
different artifacts may use different backing. Storage remains in its source
module's C++ namespace so user types resolve in the same scope. Empty values use
the same representation. Current producers admit supported builtin, fixed-array, and struct
elements. Private type definitions precede static backing, followed by function
bodies; dependencies request complete element definitions for static storage.
Target variable declarations participate in ordinary traversal, verification,
dependency collection, and emission. The existing runtime slice supplies the
read-only access and bounds operations.

Constant blocks have already executed during semantic analysis. Their independent
bodies remain available for semantic validation but have no callable or module
item to schedule, so they produce no C++ body or runtime call.

`SemDefault` realizes as typed C++ value initialization (`T {}`), with pointers
using a typed null cast. Semantic analysis has already established Carven default
availability. Empty structure construction defaults fields in declaration order;
nonempty construction supplies every field explicitly. Aggregate operand
scheduling preserves source operand order, snapshots and cleanup. Empty structures
and default arrays stay compact in generated syntax. Scalar, pointer, slice and
range defaults need no execution when discarded.
Native default constructors remain observable even when the result is discarded,
and C++ checks their availability and access. Required constant defaults arrive
at lowering as completed values through the usual freezing path.

Constant functions also retain ordinary runtime bodies. Runtime calls use normal
lowering, operand evaluation, ownership, cleanup, and wrapping integer arithmetic.
The qualifier alone supplies no call-result fact or permission to discard a call.

Read parameters, Read argument temporaries, and Read range bindings preserve
Carven array, String, and closure storage, including storage in Carven aggregate
fields, through const references. Lowering uses the resolved type-contents query
shared with ownership analysis. Other builtin Read parameters use native const
values. Remaining types use `runtime::ReadArg<T>`, which selects a const value for trivially
copy-constructed and destroyed types and a const reference otherwise. This also
covers Carven records whose native members determine their copying and
destruction. Native template arguments alone leave the instantiated type's
storage contents unknown. Interface planning includes complete definitions for
these trait queries. A pointer representation is complete without completing its
target; pointer dependencies request target declarations. Forming that target's
type expression can still require complete definitions, such as Read parameter
types in a callable signature. Declaration ordering includes these requirements;
cycles in type formation remain C++ errors.

Native Take query operands require `T&&`, including scalar and pointer types.
Operand preparation records this as `NativeTake`, separately from Carven owning
`Consume`. Realization must preserve the complete operand type when producing
that rvalue reference. A sequencing barrier first completes the owning snapshot;
its subsequent native delivery retains the queried `T&&` and its constness.

Write parameters use `T&`; Take parameters own a `T`. `runtime::transfer` exposes
a mutable owner's value for construction: trivial values are read, other values
are supplied as rvalues. It returns a reference and adds no intermediate owner.
C++ selects the constructor. Semantic analysis enforces source availability.

`ptr<T>` lowers to a pointer to `std::add_const_t<T>` and `ptr<&T>` to a
pointer to T, composed per layer for nested ptr values. Read ptr operands
snapshot the address before later operands can replace its slot. Dereference
selects that saved address before evaluating the rest of a store or call. Typed
null constants retain the complete pointer type in native overload resolution.
Native adoption uses typed initialization and C++ conversion checks. External
owners supply resource cleanup.

Native Read pointer operands preserve the pointer type and const-lvalue category
used by C++ type queries. Adaptation isolates the selected address from subsequent
slot writes, using storage when sequencing requires it.

Source-owned writable-pointer declarations retain explicit pointee access. Lowering
records this declaration contract; emission attaches its const-correctness
annotation. Backend operand storage and projections receive ordinary analysis.

The parameter policy is shared by declarations, definitions, and callable signatures.
C++ imports and export façades use this same parameter policy, type realization,
and failure ABI. Export Take parameters are forwarded through `transfer`; import
Take parameters use the native rvalue category. Read and Write retain their
reference/value categories. Import bridges and native expression operands share
the same typed rvalue construction. Export forwarding retains the ordinary
transfer policy even for native types with trivial copying and observable or
deleted move constructors. Import bridges call the globally qualified
provider directly, allowing C++ overload resolution and template deduction.
Type lowering receives an explicit naming scope: public signatures use globally
qualified nominal names, while module bodies retain local names. Both use the
same dependency discovery and callable work queue. Export façades project internal
test-stop transport to the declared result carrier, retaining declared failures
and terminating on an escaping test stop.

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

Bindings initialize in their natural scopes. Assignment uses C++ assignment
and requires an assignable destination. A binding initializer without outward
failure or test exit initializes its final C++ object directly. When it requires
statements or local backing, a typed factory contains the complete initializer
and constructs the result before that backing is destroyed. Escaping structured
initializers deliver directly into their final deferred destination. Delayed
construction uses
local result storage only where direct initialization cannot preserve control
flow and temporary lifetimes. `runtime::DeferredResult<Result>` initializes once
from a typed factory. At C++ template instantiation, `std::is_reference_v<Result>`
selects borrowed or owned storage. Value results construct directly in owned
storage and are destroyed at scope exit; reference results retain their referent's
identity. Borrowed storage requires its owner to remain alive through the source
cleanup boundary. The runtime owns placement construction. Generated factories
return complete initializers, preserving explicit construction and
copy-initialization rules.

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

## Builtin calls and test exit

`SemPrint` lowers to runtime printing calls; `SemTestReport` lowers to reporting
and conditional test exit. A concrete callable whose published effect admits
test stop returns `Outcome<Result, TestStopped, Failures...>`. Callable views
use the same transport, with result adaptation lifting ordinary returns.
Unaffected concrete callables retain their ordinary return representation.

Prepared scalar print operands pass their known text to the ordinary runtime
printing entry. Preparation retains the original operand's execution under its
normal access and sequencing rules. The fallback writes the prepared bytes directly;
when library feature detection admits `std::print`, it prints the text through that
facility. Earlier values and separators remain observable if a later value's
formatting or output fails. An inner formatted String still completes, including
its owning construction, before
subsequent print arguments execute. Allocation counts and incidental buffers
inside scalar output conversion are not source guarantees; source String
construction and operand completion retain their boundaries.

Structural print operands use a borrowing wrapper with a generated stateless,
const-callable display helper. `realization.display` reads published nominal
fields and enum cases and constructs direct field accesses and writer statements.
`ModuleLowering` shares helpers by semantic type and display depth across all use
sites in the module. Helpers are emitted in dependency order after complete
private type declarations, before consuming functions. Fully qualified helper
types avoid local name lookup; no function pointer or captured helper state is
needed. Generated size follows the reachable type-depth pairs and their fields,
plus constant-size use sites. Field layout is emitted as literal text;
sequence emitters receive the known depth for indentation and visit runtime
elements within display limits. `DisplayWriter` handles scalar conversion, nested text
escaping, and bounded output. Its completed text uses the ordinary runtime
printing entry. External and
callable leaves remain opaque; no user formatter participates. The wrapper is
consumed synchronously after ordinary Read argument sequencing.

Direct comparison explanations intercept the condition's existing expression
fragment after operand sequencing. The observer in `testing.hpp` compares once and renders
its operands only on failure. Observation prevents replacing that comparison with
a known Boolean while retaining operand preparation and snapshots. Short-circuit
explanations use ordinary expression construction: selected branches observe the
right Boolean result, and a skipped failing branch records `<not evaluated>`.
Known left operands select their branch during realization. Explanation
storage is local to the report operation and is completed before message evaluation;
`TestFailure` borrows it only during the reporter callback.

Calls that project results check `TestStopped` before success or typed failures.
Propagation returns through each Carven frame, preserving C++ scope cleanup;
the test body consumes the exit by returning to its runner. `TestStopped` is
internal transport and is absent from Carven failure sets.

The runner activates a thread-local `TestContext` for each case. Report operations
use that context. Native export façades remove `TestStopped` from the result
carrier, preserving success and every declared failure. An escaping test stop
terminates. Arbitrary C++ callbacks do not participate in Carven propagation.

Runtime `print.hpp` selects C++23 `std::print` using library feature detection and
otherwise supplies the C++20 `std::format`/`fwrite` implementation. This selection
belongs to consumer compilation and preserves the user's selected C++ standard.
Values, structural display text, separators, and newlines share this output policy.
The C++20 fallback writes UTF-8 bytes; Windows console Unicode rendering depends
on the console configuration. `std::print` supplies native Unicode terminal handling.

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

Discarded results pass through expression evaluation. Known conditions request
execution without a value and retain their full-expression cleanup boundary. An
unused short-circuit result needs no branch when its selected operand has no
execution obligation. After required execution is selected,
`discarded_operation` chooses the C++ statement form. Calls with known Carven or
runtime callable and result contracts use ordinary expression statements.
Non-void native calls, opaque result types, and other retained value expressions
use explicit void conversion. `TargetSymbolInfo` owns each intrinsic's spelling,
defining header, and call-result discard policy. Emission, dependency
collection, and realization consume that record. Discard policy does not grant
permission to omit execution.

Target locals and parameters use unit-owned `TargetLocalID` identities. Their
spellings live in the target unit and are only resolved for emission and name
allocation. `TargetLocalExpr` refers to those identities; symbolic, member, and
capture-field names remain distinct. Sealing rejects foreign, undeclared,
out-of-scope, and multiply declared local identities.

Function-body completion derives parameter retention, local unused attributes,
and owner mutability from identity-based uses in the retained target tree,
preserving initialization and lifetime. Source unused diagnostics belong to
semantic analysis.

Scope construction owns auxiliary storage and preserves semantic lifetimes.
Initialization stays at its execution point, including conditional paths. Native
bodies provide scopes; independent regions with declarations require a block.
Statement composition and source attribution do not create scopes.

Realization records mutable owner access required by emitted Write operands and
transfers. Function-body completion uses these retained demands to select const or
mutable storage for directly initialized local owners. Deferred initialization
uses mutable result storage.

`ProjectionPlace` forwards the consumer's access through fields and array elements;
`WritePlace` requires mutable access. Realization resolves projection access before
sequencing, retaining const references for read projections and propagating write
demands to the owning object. Dereference reads the pointer slot; the pointer type
determines access to the pointee.

Named values preserve copy access at C++ return sites through const access, such
as `std::as_const`. Take parameters own value storage. Deferred initialization
constructs directly in final storage.

Read range bindings use the Read parameter policy;
Write range bindings are mutable references. A range binding is never a Take
source. Arrays, slices, and text use explicit Read or Write borrowing during
realization. C++ range-for iteration consumes the range expression after operand
construction has retained required backing.
Write element types can be deduced from the range. Text decodes UTF-8 in one
sequential pass. Array initialization uses an accurate type context without
repeating it on both the local declaration and the initializer.

Builtin compound assignment snapshots the prior value when its right operand
requires execution. An execution-free right operand uses the selected place
directly; effectful operands retain the source snapshot and evaluation order.

Carven call operands for builtin value parameters use value delivery. Native C++
calls retain their const-reference operand contract for overload resolution.
Named input storage already has a source lifetime; sequencing creates snapshots
when later evaluation requires them. Scalar value consumers can use direct local
storage within an expression frame.

Integer range values use `runtime::Range<T>`, which stores both bounds and an
upper-bound inclusion flag. Its iteration operations are `constexpr`. Range loops
use C++ range-for; integer ranges are copied before traversal. Closed iteration
tests its final element before incrementing, so it can include the type maximum.

Carven evaluation is left to right and exactly once. Temporaries preserve that
order when a direct C++ expression would not. Short-circuit evaluation remains
inside the selected branch. Concrete closure callees retain object identity
before argument evaluation;
callable views retain a target description. Neither choice copies capture contents.
Source and full-expression scopes preserve lifetimes.

`BodyRealizer::ExpressionBuilder` constructs complete child fragments. Each owns
its declarations, ordered statement prefix, and either a residual target value,
saved storage, an explicit binding or constant identity, or completed evaluation. Ordinary
residual target trees are consumed once. Repeated consumers explicitly save their
input. Source subtree summaries describe prospective obligations; residual flags
describe only execution and observation still present after prefix extraction.

Children are constructed from right to left to determine later statement
boundaries and backing-retention demands. Their declarations and initializations
are adopted from left to right, preserving source execution and reverse cleanup
order. Statement chunks splice in constant time and become vectors at completed
target statement boundaries.

One `ContinuationTask` chain covers recursive expression, region, loop, report,
and pattern construction. The body entry drives the chain; completed fragments
use synchronous storage and operand operations. `realize_operation` consumes
prepared operands. Cleanup-frame links share retained declarations across
operations with the same source lifetime.

Composition uses execution and storage-read facts with C++ sequencing guarantees.
Storage access preserves scalar Read snapshots and Read aliases to owned storage.
Write operands retain the selected place before later operand evaluation; a later
closure rebind cannot change an already selected assignment target or receiver.
Callees are selected before arguments. Structured regions deliver through explicit
result destinations. Completion without a normal successor stops operand
composition. Known short-circuit conditions select execution paths while retaining
the condition's required execution.

Builtin integer, Boolean, and character unary/binary chains use a target
expression-depth budget. Realization selects a typed scalar storage boundary
before constructing children, then propagates retention demand while preserving
arithmetic order, checks, and traps. Native and floating expressions retain their
ordinary construction and evaluation paths.

Expression frames use the existing `LifetimeRegionID`. Conditional execution
that shares a full-expression lifetime uses the same frame; an expression-position
lexical region retains its own frame. Frames separate storage declarations from
initialization, reserving temporary storage in source order at the enclosing
expression boundary and initializing only on the selected path.
Reverse destruction order includes those objects and any retained Outcome owners.
Known or discarded results retain required execution.
Fallible calls check success before continuing with its value. Result demand controls whether
the success value is observed, transferred, or discarded, or an exact Outcome
is propagated to the function return. Numeric operations
preserve their resolved type across promotion, overload, and deduction boundaries;
the renderer does not infer types.
Discard demand removes unneeded pure results through the same expression lowering
that handles retained results. Short-circuit control has one construction path;
execution and lifetime requirements remain active when the result is discarded.

A known scalar result uses literal delivery when the operation itself needs no
execution and selects no storage. The ordinary discard path completes its
operands before delivering the constant. Effects-only evaluation preserves
borrowed temporary owners through their source cleanup boundary.
For `values.slice(0, 2).len()`, realization executes the checked slice and returns
`2`; the slice result needs no storage or size query. Failure still prevents
result delivery.

An independent full-expression root call can initialize an ordinary Outcome local
when it shares no retained storage or cleanup frame with other operands. Nested
calls and conditional execution use deferred storage where needed. Both paths
use the selected result demand and preserve reverse destruction order among
all retained owners.

## Extending operations

Semantic analysis publishes the operation's type, evaluation and lifetime contracts.
For ordinary operations using existing contracts, the backend describes ordered
inputs and uses in `preparation/operands.cpp` and realizes C++ syntax in
`realization/operation.cpp`. Inputs with a uniform storage use share the direct
child definition in `semantic.semir.children`; mixed uses retain their operand
adapters. Both visitors enumerate the semantic expression alternatives explicitly.
Native operation realization also enumerates the `CppOperation` alternatives.
Structured control retains its specialized realization paths.

Scalar and array callable adaptation share the source capture-policy decision.
Array adoption stabilizes its source through ordinary operand construction, then
calls `runtime::adopt_array<Destination, Stateless>` in `array.hpp`. Native array
types determine recursive aggregate initialization; matching types retain ordinary
copy construction. Empty arrays perform no element adaptation. The explicit
stateless policy selects the existing callable factory at leaves; other leaves
use ordinary construction. Semantic analysis applies the same callable compatibility constraints to scalar
and array element types. Slice elements retain invariant storage types; adoption
does not rebuild their borrowed backing. Semantic analysis owns borrowing validity. The body realizer retains
the source backing storage.

Tests must establish the new operation's behavior and interactions with existing
contracts. Scheduling, storage and cleanup consume those contracts. New control
scopes, ownership modes or partial-object lifetimes require design and checks at
their owning boundaries.

Structure operands retain source order while C++ initializes fields in declaration
order. Realization identifies the suffix whose effects and storage reads retain
their required order in C++. Pure values impose no ordering constraint.
Conflict barriers complete the preceding operands; the suffix constructs directly
in the final initializer, including any explicit empty constructions.
Nested expressions that emit statements still complete pending predecessors before
those statements.

Aggregate operands may be completed in separate storage before the final
initializer consumes them. An immovable native component saved across a failure
barrier cannot be transferred into the final aggregate; C++ rejects that generated
construction. Partial-object initialization and cleanup are not represented by
the existing operand storage contract.

## Control

Conditionals, returns, and scopes lower directly. Boolean short-circuit values
use C++ `&&` and `||` when the selected operand needs no preceding statements;
otherwise a branch contains that operand's evaluation and result delivery.
Loops use native while conditions when condition evaluation needs no preceding
statements. Otherwise the iteration body sequences the condition before its exit
test. Step loops retain a local continue target.
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
expanding combinations of alternatives. Matching produces a known Boolean or a
residual predicate with an ordered statement prefix. Literal and range patterns
use direct comparisons; wildcard and type constraints are known successes.
Compound patterns use native short-circuit expressions when their operands need
no statement prefix, and conditional statements otherwise. Partial matches record
binding addresses; the successful arm constructs its bindings and evaluates its
guard once. Catch arms use the same predicate composition.

`FailureABI` gives each failure set a deterministic member order. Failing
results use `Outcome`; other results are direct. Widening accepts identity or a
strict failure-set superset. Calls, propagation, handlers, and callable
adaptation use this one contract. Handler failures go to the enclosing failure
target; rethrow preserves the selected failure. Test exit leaves the test.
Outcome and handler-variant failures share one typed dispatch construction;
their source forms determine the payload projection.

A call in function-return position uses `Outcome::propagate() &&` when its
failure destination is the function exit and its normalized target result type
equals the enclosing callable's result type, including `TestStopped`. Realization
retains the call's existing Outcome local or deferred storage, operand sequencing,
and cleanup frame, then returns `std::move(outcome).propagate()` (dereferencing
deferred storage where needed). Runtime reconstructs the active alternative
through the existing payload `transfer` policy. It adds no intermediate carrier
owner and leaves source-carrier destruction in the caller's scope.

This demand applies only to the root call; argument calls retain their own
success and failure handling. Local handlers, differing carriers, and success
computation keep ordinary projection and delivery. Keeping the source carrier
materialized preserves observations of the payload's construction address,
including for payloads with trivial copy, move, and destruction.
The transfer policy also preserves native copy and move effects.

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

Schedules own interface definitions, C++ façades, private nominal and closure
ordering, and selected tests. Module lowering starts with externally visible
functions, process and C++ export entries, interface closures, and enabled runtime
tests. Realizing native function references and closure types requests their local
definitions through `ModuleLowering`. Each callable is lowered once; its references
can request further definitions. Unrequested private functions and closures have
no native declarations or definitions. Constant execution retains its SemIR bodies
independently of this native selection.

Lowering also records providers of emitted names and types. These transient
provider sets are consumed into include directives.

Each runtime test becomes a function. Static tests have already executed during
analysis and receive no target function name or runner entry. Module runners call
tests in source order; the runner header calls modules in canonical order. The default entry calls
that same runner. Default test-entry mode disables the program entry wrapper in
the module schedule; external-runner mode retains it for the consuming build.
Explicit C++ fragments preserve their bytes and source order.

## Target syntax and emission

Expressions, statements, and items are move-only recursive values.
`TargetBlockStmt` always denotes an actual C++ block; plain sequences are
composed before publication. Only types are interned. A construction-time index
selects candidates for structural equality; type storage and IDs retain insertion
order. Target type IDs belong to one unit. Source attribution and function forms
use exact variants.

Type construction accepts only children already present in the same unit;
append-only insertion establishes acyclicity. Finishing names registered
namespace-visible query types before their first declaration use, then validates
occurrence type references and local control transfers.
Target verification does not recheck Carven evaluation order, object lifetime
semantics, or C++ overload and constructor feasibility. Realization preserves
those input contracts through operand use, frame ownership, result destinations
and failure dispatch, with local invariants and generated-program tests.
Owned syntax nodes define occurrence identity. `TreeValue` uses an explicit work
stack to clear descendants, including partially moved trees, statement bodies,
and namespace items. Child cleanup also bounds the stack depth of variant
replacement. Traversal preserves scope order through enter/leave events.
Dependency collection visits the finished tree and referenced types to derive
the required standard and runtime headers.

The renderer builds completed layout tables in postorder for expressions,
statements, items and the type dependency DAG, including decltype expressions.
Consumers apply precedence to completed expression layouts. These tables live
for emission. The renderer serializes nodes, directives, source mapping, and
whitespace. Layout alternatives inspect pending commands up to the next line
boundary; command links and explicit choice frames share the remaining work. Binary rendering
preserves the expression tree using C++ precedence and associativity. Nested
comparisons on either side receive explicit parentheses to make their grouping
visible. Semantic inference and target syntax construction finish before
rendering. Artifact collection checks logical paths, uniqueness, and prefix
safety.

Continuation indentation is bounded by half the configured line width, keeping
whitespace proportional to syntax size. Child-before-parent width summaries
avoid repeated measurements of single-line preferred layouts. Width addition
checks overflow; line breaks, raw bytes, source directives, and indentation
changes retain their layout operations.

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
protocol. Lowering registers external queries for artifact-local type aliases.
Finishing collects occupied names across the complete unit, merging reopened and
qualified namespace paths, then allocates aliases in their destination namespace.
Aliases precede their first use in dependency order, after preceding nominal
definitions. References use qualified alias names; exact
references and cv-qualification remain in the alias definition. Generic local
`decltype` expressions retain their local scope. This preserves shared query
dependencies in the generated C++.

Executed calls and type queries consume the explicit semantic callee and operand
access. Executed calls use ordinary argument sequencing and access lowering.
Receiver access is preserved independently of storage made mutable to realize a
later Take. Discarded external calls need no result storage, so void-returning
providers remain usable.

C string literal operations have an intrinsic external `const char*` type.
Lowering emits byte-escaped narrow literal storage with `static_cast<const char*>`,
preserving pointer semantics for overload resolution and deduction. Ordinary
string literals retain `std::string_view` realization.

## Operation preparation

`backend/preparation` consumes immutable semantic operations and their known
normal-completion values. It owns prepared bytes, integer field plans, residual
operand mappings, size bounds, and UTF-8 proofs. It never interns into semantic
stores. `PreparedOperation` optionally owns a plan through realization.
Operations without preparation and print calls without known scalar text hold no
plan payload.

`PreparedFormatText` owns the complete text. `PreparedWriterFormat` owns literal
segments, builtin fields, reservation bounds, and retained operand indices.
Fields consume the retained operands in order: one value, followed by a width
when the integer field has no static width. Retained operand indices map this
pack to the original source expressions.
`PreparedDelegatedFormat` owns native format bytes in `format_string`, retained
operand indices, and an encoding guarantee. `PreparedPrint` owns optional scalar
text for each source operand. Published semantics retain the source operation and
its value facts.

Preparation adapts published constant facts for `semantic.format.builtin`.
Optional text materialization has a preparation-owned 64 KiB budget. Direct
writer selection consumes prepared text and fields without native serialization.
Delegated residual formatting additionally budgets escaped braces and field
spellings through the bounded `semantic.format` serializer. Over-budget or
unsupported work retains runtime formatting. Serialization of the source
fallback is outside the optional materialization budget. Supported, known dynamic
integer widths and floating widths/precisions resolve to static specification text
within the preparation budget. Writer classification accepts static integer
specifications and dynamic widths of type `i8`, `u8`, `i16`, `u16`, or `i32`,
with optional zero padding and an optional `b`, `B`, `o`, `d`, `x`, or `X`
presentation.
These width types fit the runtime entry's signed 32-bit parameter; negative
widths terminate. Wider width types and unsupported specifications use general
formatting.

Writer also accepts default floating output, fixed/scientific/general floating
presentations with precision up to 256, and default text, boolean, and character
fields. Larger precisions and decorated floating fields retain the standard
formatter. Reservation bounds exclude dynamic text bytes and dynamic-width
fields. Text lengths contribute after operand completion; dynamic-width fields
grow storage as needed. Native/custom formatters retain all original arguments
because they can inspect the argument pack.

Preparation translates each selection into a generic input demand: value or
execution effects. Every original operand remains in source order. Realization
uses these demands to preserve effects, failures, scalar snapshots, storage reads,
and temporary backing, while passing only demanded values to the native operation.
The sequencing and storage machinery does not classify format plans.

C++ checks native format specifications and formatter availability, performs object
layout and ordinary optimization, and owns instruction selection. Carven selects
implementations from published source facts and local operation contracts.

## Owning text realization

Text byte and scalar queries use `runtime::text_bytes` and `runtime::text_chars`.
C++ overload resolution selects the `std::string_view` or `const String&` adapter;
both return views of the input storage. Semantic loans and generated cleanup
scopes retain the backing owner.

Builtin String lowers to the owner in `string.hpp`, with private `std::string`
storage. The shared Read storage policy preserves caller aliasing for named owners and
projected fields/elements across sequencing and failure barriers. Text operations
select runtime factories and members through target syntax and record their
support-header dependencies. Write receivers remain places; range projections
borrow the receiver's text storage.

Native byte storage enters through `String::from_utf8`, which validates UTF-8
before adopting it.

All formatting forms complete their hole operands in source order under the
Read storage policy before constructing or appending text. Scalar snapshots,
String aliases, failures, and temporary backing follow ordinary operand
construction. Discarding an owning result retains String construction.

An owning `SemFormat` with `PreparedFormatText` lowers to `String::from_str` with
an explicit byte-length literal. Mixed preparation maps retained operands to the
selected format; folded operands retain their execution obligations. String
contents are observed after all holes complete.

`PreparedWriterFormat` lowers through the shared statement builder in
`realization.format` to `runtime::Writer` in `writer.hpp`. Formatted append emits
ordinary statements after the expression builder completes its inputs and flushes
pending earlier work. A direct owning return with the native return ABI creates a
local String, writes its fields, and returns it by name, permitting C++ NRVO.
Nested owning expressions, initialization, and failure-transport returns retain an
expression lambda to preserve their construction and delivery boundary. Its
parameters use the existing Read storage policy.

Both forms append static text and call
`integer<base, uppercase, zero_pad>(value, width)` or
`integer_dynamic_width<base, uppercase, zero_pad>(value, width)` in order, copy
text fields, select boolean text, and encode Unicode scalars directly. Floating fields call
`floating(value)`, `fixed<precision>(value)`, `scientific<precision>(value)`, or
`general<precision>(value)`. These small runtime entries use `std::to_chars` with
a bounded stack buffer and append converted bytes directly to the destination.
Writer realization consumes completed operands before emitting reservation and
field writes.

The prepared minimum and maximum byte counts for statically bounded fragments
reach the writer as ordinary arguments, followed by an initializer list of
explicit dynamic text byte lengths. Carven emits each `.size()` query after all
holes complete. Runtime
adds these lengths to both bounds with checked arithmetic; it does not
rediscover field types or parse format policies.

When the minimum exceeds available capacity, runtime reserves for the maximum, capped at the
destination's size limit. Otherwise normal storage growth applies. Equal bounds
reserve an exact size for the counted fragments; dynamic-width fields are excluded.
An upper bound alone does not force allocation for a short result. Runtime checks
size arithmetic and performs integer conversion
with `std::to_chars`, sign handling, and padding. Writer integer inputs use the
same `runtime::Integer` domain as arithmetic; boolean and character fields use
their separate operations.

The writer borrows private String storage and requires valid text and disjoint
inputs. Direct conversions need no format parsing or output validation scan.
Unsupported fields retain the complete selected general format call. Writer
paths have no `<format>` dependency; fully precomputed contents use direct text
construction or append.

Reservation requests do not prescribe the native String's exact capacity or
allocation alignment; the native String implementation selects both.

Other owning `SemFormat` operations pass their selected argument pack. The
prepared `PreparedDelegatedFormat::encoding` selects `format_valid_utf8` for
`ValidUTF8` and `format` for `Unproven`. Realization embeds the prepared bytes as a
compile-time `std::string_view` with an explicit byte length. Inside that call, String
aliases provide text views and `char` values encode to UTF-8 Strings. C++ checks
`std::format_string` and formatter availability; source directives attribute those diagnostics to the
interpolation. Both entries are `noexcept` and use the same argument adapters
and `std::format` call. The general entry passes the completed buffer through
`String::from_utf8`; the proved entry adopts it without scanning it again.
`StringFormatAccess` privately adopts that buffer using the shared String move
constructor, without an additional byte copy or allocation. Entry selection
consumes the preparation-owned encoding proof.

An append `SemFormat` places its Write receiver before the hole operands in target
construction. Realization selects the receiver once, then completes all holes using
the same Read, failure, and temporary-backing rules. Preparation operand indices
exclude the receiver. Known contents lower to `receiver.append(static_text)` after
required hole execution. Prepared builtin fields use the writer above. Otherwise,
`PreparedDelegatedFormat::encoding` selects `append_format_valid_utf8`
or `append_format`, with the receiver followed by the selected format and argument pack.

The append entries reuse their corresponding owning formatter, then append the
completed valid text. The proved entry formats once, adopts that buffer, and avoids
a UTF-8 validation scan. Standard formatting owns any partial byte writes until
completion in a temporary native buffer. Known contents lower to direct append.
`Writer::append` requires each supplied fragment to be complete valid text.

Both entries are `noexcept` and require the source operation's destination/input
separation, without rollback after formatting begins. Capacity growth and native
formatter allocations remain runtime concerns.

Ownership analysis establishes borrowing validity before lowering. Runtime views
carry no owner metadata. String owners, pending operands, retained range sources,
closure captures, and Outcome payloads use ordinary construction and cleanup frames.

Unchecked character construction lowers to a C++ cast to the character
representation. Unchecked UTF-8 construction calls `utf8_text` from `text.hpp`
to create a view over the input bytes. Neither operation validates content;
semantic ownership analysis preserves the text's input backing before lowering.
The backend supplies runtime includes without a source-level header import.
