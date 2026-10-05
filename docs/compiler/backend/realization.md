# Body realization

Body realization composes prepared semantic operations into target expressions,
statements, storage, and control exits. Representation defines native types and
ABI; preparation supplies operation plans and operand demands. Realization
preserves evaluation order, storage observations, constructor capabilities,
control exits, and cleanup.

Body lowering queries one published semantic body by BodyID. Preparation
summarizes its executable region; realization uses its binding, parameter,
capture, and lifetime identities. Static-instance bodies retain only runtime
parameter mappings. Calls identify their instance directly in SemIR.

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
An expression with a proved absent execution entry retains its type as
`SemUnreachable`. Realization emits a semantic-proof unreachable statement and
produces no normal value.

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

Statement fragments separately retain declarations and outstanding cleanup.
Concatenation combines both facts; an enclosing block ends its children's cleanup
before the next statement. Declarations remain initialization barriers even when
they require no cleanup. Unclassified declarations conservatively retain cleanup;
storage producers can establish its absence from the selected representation.

Realization records mutable owner access required by emitted Write operands and
transfers. Function-body completion uses these retained demands to select const or
mutable storage for directly initialized local owners. Deferred initialization
uses mutable result storage.

Automatic value temporaries use const storage for observation and mutable
storage for `WritePlace`, `Consume`, and `NativeTake`. Reference, Read-parameter,
and exact native-query storage retain the access qualification selected by their
types; consumer access remains independent of the local declaration.

`ProjectionPlace` forwards the consumer's access through fields and array elements;
`WritePlace` requires mutable access. Realization resolves projection access before
sequencing, retaining const references for read projections and propagating write
demands to the owning object. Dereference reads the pointer slot; the pointer type
determines access to the pointee.

Named values preserve copy access at C++ return sites through const access, such
as `std::as_const`. Take parameters own value storage. Deferred initialization
constructs directly in final storage.

## Final storage and owning snapshots

Read operands whose types are value snapshots need no separate temporary backing.
The published type-contents query excludes storage owners, closures, and native
values from this case. Sequencing still saves earlier values before later effects;
place projections and borrowed owners retain their storage requirements.

Bindings initialize in their natural scopes. Assignment uses C++ assignment
and requires an assignable destination. When the realized initializer prefix has
no outstanding cleanup, its statements precede an ordinary final declaration,
including when they propagate failures or test stops. This applies independently
of the final object's constructor capabilities. Source lifetime and borrowing
rules remain unchanged; only cleanup-free implementation storage may live longer.
The proof considers all retained owners and every Outcome alternative. Native
values, owned storage, and closure owners conservatively require cleanup;
retained references borrow their backing, whose own obligation remains separate.

A binding initializer without outward
failure or test exit initializes its final C++ object directly. When it requires
local cleanup, a typed factory contains the complete initializer
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
Read-only ordinary storage uses `std::add_const_t<Query>` to qualify value
queries while preserving reference queries. Read delivery also applies const
access at the consumer.
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
realization. Their C++ range-for iteration consumes the range expression after
operand construction has retained required backing.
Write element types can be deduced from the range. Text decodes UTF-8 in one
sequential pass. Array initialization uses an accurate type context without
repeating it on both the local declaration and the initializer.

Builtin compound assignment snapshots the prior value when its right operand
requires execution. An execution-free right operand uses the selected place
directly; effectful operands retain the source snapshot and evaluation order.
A binding, or a field path over one, names the same object wherever it is
evaluated, so assignment spells it again instead of holding a place reference.

Carven call and intrinsic operands for builtin Read values use value delivery.
Carven parameter types are exact and member access selects no overload, so their
other Read operands need no const cast; Read parameters are already const. Native
C++ calls retain their const-reference operand contract for overload resolution.
Named input storage already has a source lifetime; sequencing creates snapshots
when later evaluation requires them. Scalar value consumers can use direct local
storage within an expression frame.

Integer range values use `runtime::Range<T>`, which stores both bounds and an
upper-bound inclusion flag. Integer loops snapshot that value once and use an
independent cursor. Known exclusive bounds produce a direct C++ `for`; other
integer loops check the terminal element before incrementing, including when
`continue` ends the body. Closed iteration can therefore include the type maximum.

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
its exported storage reservations, ordered statement prefix, and either a residual
target value, saved storage, an explicit binding or constant identity, or completed
evaluation. Ordinary
residual target trees are consumed once. Repeated consumers explicitly save their
input. Source subtree summaries describe prospective obligations; residual flags
describe only execution and observation still present after prefix extraction.

Children are constructed from right to left to determine later statement
boundaries and backing-retention demands. Their reservations and statements
are adopted from left to right. A reservation enters the cleanup-owning scope
at its controlling operation, after earlier owners, preserving source execution
and reverse cleanup order. Statement chunks splice in constant time and become
vectors at completed target statement boundaries.

Traversal uses one `ContinuationTask` chain for expression, region, loop, report,
and pattern construction. The body entry drives the chain; completed fragments
use synchronous storage and operand operations. `realize_operation` consumes
prepared operands. Cleanup-frame links share retained declarations across
operations with the same source lifetime.

Validate stack use and growth on deep and wide structured bodies, and check
evaluation order and cleanup through behavior and lifetime tests.

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
lexical region retains its own frame. A scope owns cleanup when its full-expression
or delivered lexical region is distinct from every active outer frame. Realization
encloses evaluation and delivery in that scope, so all payload and backing uses
precede cleanup. The destination of a result that survives the scope retains its
own storage.

Within the cleanup-owning C++ scope, retained operands and Outcomes initialize
ordinary locals directly. Evaluation in a nested C++ scope exports reservations
for storage that must survive that scope; initialization remains on its selected
path. Reservations propagate to the owning scope and enter its statement sequence
at the controlling operation, after preceding ordinary owners. Ordinary declarations
and reservations retain source construction order. Result reservations follow their
retained backing, so the result is destroyed first. Conditional expressions that
reduce to direct C++ expressions use ordinary expression delivery.
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

Each fragment captures whether it is constructed in the cleanup-owning scope.
Owner anchoring and call completion use this single storage authority. Shared
evaluation and a dynamic short-circuit operand enter a nested storage scope;
statically selected short-circuit operands stay in the current scope. Result
demand remains independent of storage ownership and does not change the source
lifetime or payload delivery contract.

A copied scalar can remain local to its evaluation scope. Native Read or Take
may retain a reference to a scalar through the enclosing full expression, so
that backing follows the same scope rule as other retained storage. Having no
destructor does not by itself permit a shorter object lifetime.

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
An independent condition with outstanding cleanup delivers its Boolean into an
outer local and closes the evaluation scope before branch execution. A prefix
without that obligation uses its predicate directly, preserving its preceding
execution and exits without an additional Boolean local.
Loops use native conditions when condition evaluation needs no preceding
statements. Region realization consumes semantic entry reachability before
implementing statements or tail results. With a direct condition, a single step
expressible in a C++ for header uses that syntax. A normally completing step region
whose exits stay local uses an immediately invoked void lambda in the header;
statements retain their full-expression and cleanup scopes. Native `continue` runs
these steps after body cleanup. Steps with outward exits use the enclosing control
receivers and a local continue target in the while form.
A condition with preceding statements executes inside the iteration before its
exit test.
Known consumers receive results directly, including returns from selected branches.
Pattern bindings initialize directly from their selected subject or projection.
Alternatives retain address slots when their sources must cross a selection scope
or differ between successful paths.

Structured values take the most direct C++ form their destination admits:

1. A scalar value region whose realization is one returned expression, or one
   two-way conditional of returned expressions, is that expression or `?:`.
   A result copied into typed storage or a by-value operand is the expression
   itself; a returned integer literal states the region's result type. A result
   its consumer may bind by reference is cast to the result type, so a selected
   expression that names storage still delivers a value. The check reads only
   the statements the region itself produced.
2. Otherwise each arm delivers to the destination: a return, an assignment to a
   named local, deferred initialization, or a discard.
3. Region exit labels remain only where C++ has no structured form: failure
   transfer, expanded-loop exits, and match arms that must fall through.
4. An expression-position value region with statements and no outward failure or
   test exit becomes a local value lambda, which also keeps a `let` initializer
   const. Regions with those exits use deferred initialization.

Function return applies the failure ABI independently of lambda yield. A retained
void expression can be returned directly; an Outcome return executes it before
constructing success without a payload. Scalar success uses `Outcome::success(value)`.
Other non-void success uses `Outcome::success_from(factory)`: the factory is
invoked immediately and exactly once to construct the payload directly. Callable
adaptation uses the same construction. A factory may return an immovable prvalue.
Owner transfer, payload extraction, and Outcome widening require the constructors
used by those operations. Void and discarded regions need no result storage.
Loops and handlers receive only exits belonging to their own construct. Loops
without steps, sequence traversal, and exclusive integer ranges use native
`continue`. Inclusive and dynamic integer ranges complete iteration cleanup
before their terminal check and cursor increment.

Expanded loops realize their ordered iteration regions. Semantic specialization
publishes distinct binding, pattern, and lifetime identities for each iteration;
realization uses the ordinary binding map and preserves shared outer storage.
Regions with storage retain a C++ block for cleanup.
Continue exits one iteration and break exits the expansion. Their labels exist
only when a retained transfer needs them. A jump that immediately precedes its
label is removed. When a label has a single jump and that jump ends an earlier
conditional in the same statement list, the skipped statements become the
conditional's alternative, or its negated body when the jump was the whole
branch. This restructuring stops at declarations in the same statement list.
Skipped storage stays inside its existing C++ blocks, preserving its scope and
destruction order.

### Patterns and matches

Match locates its subject once and keeps its storage alive through selection.
A named local place subject is matched in place.
`PatternRealizer` traverses the published SemIR patterns in source order without
expanding combinations of alternatives. A selection retains ordered tests, their
statement prefixes, and binding sources. Each successful test encloses the next
test and the selected arm, keeping its projections in scope. Literal and range
patterns use direct comparisons; wildcard and type constraints are known
successes. Prefix-free tests compose as native short-circuit expressions.

A binding records its source until the whole pattern accepts. The arm then
constructs its bindings and evaluates its guard once. Alternatives share the
same arm; differing sources and sources local to an alternative join through
address slots. A common source already visible outside the alternatives needs
no slot. Match and catch use the same selection operations.

Owning pattern bindings retain an independent snapshot and its cleanup. Target
declarations record this obligation as `ConstSnapshot`; emission annotates the
binding name for clang-tidy's copy-initialization check.

Enum payload projections are pure borrowed pointers. Realization records them as
removable locals; body completion retains only projections referenced by tag
tests, payload reads, or binding construction. Removing an unused projection can
also release its enclosing projection. Subject evaluation and owner cleanup stay
in place.

Pattern completion supplies accepted and rejected successors. Realization keeps
the predicate's required prefix and uses those facts to enter later payloads,
alternatives, guards, and bodies. A stopped operand needs only a conditional
branch; its surviving normal path has a known Boolean result. A sole enum case
needs no tag test. A match arm's published no-rejection proof also removes tests
already established by earlier unguarded arms. Guaranteed conjunctions pass the
proof to their payload patterns. A guaranteed disjunction retains tests needed
to select binding sources and proves its final alternative after earlier ones
reject. Pure alternatives with a common source need no selection tests. Dynamic
bounds retain their execution, and differing binding sources join through the
same selection machinery.

A guard-free arm whose test needs no statements selects between its body and the
remaining arms, so consecutive such arms form one `if`/`else if` chain. An arm
with a guard or a test prefix falls through to its successor when it does not
apply and leaves through the match exit label when selected.

### Failure dispatch and transport

`BodyRealizer` traverses the published structured regions directly. A failure receiver is
active only in its protected region; a loop target is active only in its body.
Catch guards and bodies send failures to the enclosing receiver. The active catch
retains the payload and accepted failure set needed by a rethrow.

[Failure and test-stop ABI](representation.md#failure-and-test-stop-abi) fixes
carrier alternatives and widening. The following rules dispatch those carriers
through the active control regions.

Failure delivery registers a logical receiver, typed source, candidate failure
set, and unique target edge. A call with several failure alternatives is one
handoff; each rethrow site is a separate handoff. After the protected region is
complete, realization selects handler placement and storage from the retained
handoffs and exit cleanup facts. Composition records whether each exit crosses
pending cleanup when it is introduced and propagates that fact through closed
scopes.

The handler executes at a single handoff when its candidate payloads have
semantic value-snapshot behavior, the exit crosses no observable cleanup, and
placement crosses no loop inside the protected region. It projects the original
Outcome or receives a direct throw in an ordinary typed local. Direct throws
construct independent snapshots because a catch guard can modify the binding
used by the throw. After receiver edges are resolved, body local-use analysis
removes unused payload storage. Source execution summaries select omission of a
pure initializer or evaluation for its effects; evaluated initializers retain
their local dependencies. Pure projections use the same omission policy.

Other receivers use `optional<E>` for one protected failure type or
`optional<variant<E...>>` for several. The slot joins handoffs or retains failure
payloads across protected-scope cleanup. Its layout stays fixed while catch and
residual paths narrow the candidate set. Outcome, direct-value, and slot sources
share typed projection and dispatch. The last candidate of a closed dispatch
transfers directly after earlier candidates have been excluded. Call dispatch
follows the success and test-stop checks. Handler patterns and guards retain
their selection.

Native payloads retain transport because copies, moves, addresses, and destruction
can be observed. Cleanup facts conservatively include every source-carrier
alternative, so native or owning success values also retain the slot. Source
placement preserves a handler's loop destinations by excluding loops within the
protected region; a try inside an outer loop resolves within its own region.

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
passes each failure alternative's `failure_if` projection to
`report_entry_failure` with the failure's module-qualified name, its structural
display emitter, and the entry's `SourceSite`, then returns zero or `EXIT_FAILURE`
from `<cstdlib>`. The report call ignores a null projection, so the wrapper
needs no branches.
The wrapper's result storage undergoes ordinary scope cleanup.

A local label realizes an exit for which C++ has no suitable structured form.
Labels carry a control purpose and must respect initialization barriers. The
label remains local to the source control construct it implements. Region exits
record use when emitting a jump. Only their owning construct can restore an
entry.
