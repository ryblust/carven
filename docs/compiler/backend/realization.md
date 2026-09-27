# Body realization

Body realization composes prepared semantic operations into target expressions,
statements, storage, and control exits. [Representation](representation.md)
defines native types and ABI; [preparation](preparation.md) supplies operation
plans and operand demands.

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

## Local identities and access

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

Automatic value temporaries use const storage for observation and mutable
storage for `WritePlace`, `Consume`, and `NativeTake`. Reference and Read-parameter
storage retain the access qualification selected by their types.

`ProjectionPlace` forwards the consumer's access through fields and array elements;
`WritePlace` requires mutable access. Realization resolves projection access before
sequencing, retaining const references for read projections and propagating write
demands to the owning object. Dereference reads the pointer slot; the pointer type
determines access to the pointee.

Named values preserve copy access at C++ return sites through const access, such
as `std::as_const`. Take parameters own value storage. Deferred initialization
constructs directly in final storage.

## Final storage and owning snapshots

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

Native call results retained for borrowing use the exact queried return type.
When deferred storage is used, factory deduction is checked against that query, accounting
for C++ dropping top-level cv from scalar call results. Exact reference retention
applies to call results. Owning snapshots instead use the normalized object type.

`Consume` completes an owning value at its source evaluation point. When a
sequencing boundary requires storage, typed initialization of the normalized
object copies a native `T&` or `const T&`, moves from `T&&`, and directly constructs
a prvalue. Later mutation of a borrowed source cannot change that completed
snapshot. `NativeTake` additionally retains the queried rvalue argument category.
Borrowing a native call result and acquiring an owning value therefore have
different storage and delivery contracts.

Native Take query operands require `T&&`, including scalar and pointer types.
Operand preparation records this as `NativeTake`, separately from Carven owning
`Consume`. Realization must preserve the complete operand type when producing
that rvalue reference. A sequencing barrier first completes the owning snapshot;
its subsequent native delivery retains the queried `T&&` and its constness.

A direct Take return of a named automatic owner uses a C++ name expression when
its type contains neither native values nor closures. C++ can then elide the
local return or move from the parameter. Ordinary named returns retain copy
semantics. Native-containing values, closure captures, deferred storage, and
failure transport retain explicit transfer realization.

## Ranges, mutation, and calls

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

A published function target lowers to a direct call using its actual failure and
test-stop contract. The callee's required evaluation precedes its arguments.
Declaration finalization removes unreferenced scalar and nonowning callable locals
only when semantic preparation establishes effect-free initialization and their
types require no cleanup. Remaining target references preserve observable storage.

Executed calls and type queries consume the explicit semantic callee and operand
access. Executed calls use ordinary argument sequencing and access lowering.
Receiver access is preserved independently of storage made mutable to realize a
later Take. Discarded external calls need no result storage, so void-returning
providers remain usable.

## Operand sequencing

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
Storage access preserves Read value snapshots and Read aliases to owned storage.
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

## Cleanup frames and result delivery

Expression frames use the existing `LifetimeRegionID`. Conditional execution
that shares a full-expression lifetime uses the same frame; an expression-position
lexical region retains its own frame. An independent full-expression frame with
no conditional evaluation directly initializes ordinary locals in source order.
Independence requires a distinct cleanup region from every active outer frame;
nesting in another expression does not by itself require deferred storage.
An independent lexical region's tail can use the same storage policy when its
cleanup ID matches the region. Realization encloses tail evaluation and result
delivery in one block, so all payload and backing uses precede its cleanup.
The destination of a result that survives the block retains its own storage.
Preparation summarizes conditional evaluation through nested operands, including
report messages. Shared and conditional frames conservatively separate storage
declarations from initialization, reserving temporary storage in source order at
the enclosing expression boundary and initializing only on the selected path.
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

The frame selects storage uniformly for retained operands and Outcome owners.
This keeps declaration order aligned with construction order; selecting automatic
and hoisted deferred owners independently could reverse their cleanup order.
Both representations use the selected result demand and preserve reverse
destruction order among all retained owners.

### Owning field projection

The semantic `SemField::consumes_source` query distinguishes owning sources
from selections of existing storage. Value category alone is insufficient: Read
bindings can still name existing storage. Preparation consumes the owning
source. Realization anchors the complete source in mutable owned storage, applies
the common transfer policy to the selected field, and keeps the remainder alive
through the cleanup frame. It does not ask native overload resolution to infer
Carven ownership from an incidental C++ value category. Owning field results have
their own temporary identity for backing and escape analysis.

## Callable adaptation

Scalar and array callable adaptation consume `PreparedCallableAdaptation`, which
retains the shared semantic adaptation classification and the array delivery form.
Array adoption stabilizes its source through ordinary operand construction, then
calls `runtime::adopt_array<Destination, Stateless>` in `array.hpp`. Native array
types determine recursive aggregate initialization; matching types retain ordinary
copy construction. Empty arrays perform no element adaptation. The explicit
stateless policy selects the existing callable factory at leaves; other leaves
use ordinary construction. Semantic analysis applies the same callable compatibility constraints to scalar
and array element types. Slice elements retain invariant storage types; adoption
does not rebuild their borrowed backing. Semantic analysis owns borrowing validity. The body realizer retains
the source backing storage.

## Aggregate sequencing

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
An unconditional binding pattern initializes its local directly from the subject;
partial pattern selection retains address slots until the arm is selected.
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

### Patterns and matches

Match locates its subject once and keeps it alive and stable through selection.
`PatternRealizer` traverses the published SemIR patterns in source order without
expanding combinations of alternatives. Matching produces a known Boolean or a
residual predicate with an ordered statement prefix. Literal and range patterns
use direct comparisons; wildcard and type constraints are known successes.
Compound patterns use native short-circuit expressions when their operands need
no statement prefix, and conditional statements otherwise. Partial matches record
binding addresses; the successful arm constructs its bindings and evaluates its
guard once. Catch arms use the same predicate composition.

### Failure dispatch and transport

`BodyRealizer` traverses the published structured regions directly. A failure receiver is
active only in its protected region; a loop target is active only in its body.
Catch guards and bodies send failures to the enclosing receiver. The active catch
retains the payload and accepted failure set needed by a rethrow.

[Failure and test-stop ABI](representation.md#failure-and-test-stop-abi) fixes
carrier alternatives and widening. The following rules dispatch those carriers
through the active control regions.

Handler failures go to the enclosing failure
target; rethrow preserves the selected failure. Test exit leaves the test.
Outcome and handler failures share one typed dispatch construction. A handler
slot uses `optional<E>` for one protected failure type and
`optional<variant<E...>>` for several. Its layout stays fixed while a catch or
residual path may narrow the candidate set. The optional retains the failure
through protected-scope cleanup. Typed projection uses the slot layout; the last
candidate of a closed dispatch transfers directly after earlier candidates have
been excluded. Call dispatch follows the success and test-stop checks. Handler
payload patterns and guards retain their own selection.

Calls that project results check `TestStopped` before success or typed failures.
Propagation returns through each Carven frame, preserving C++ scope cleanup;
the test body consumes the exit by returning to its runner. `TestStopped` is
internal transport and is absent from Carven failure sets.

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

### Process entry and local exits

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
