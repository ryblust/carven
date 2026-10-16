# Semantic representation

This reference defines identity, canonical data, structured operations, and facts
available to semantic-program consumers.

## Ownership and identity

Program IDs belong to one program. Binding, pattern, and lifetime IDs belong
to one body. Name frames exist only during name resolution; bound operations
retain binding identities and lifetimes. Owning query surfaces validate identity
and range. Expression, statement, and region occurrences are recursively owned
values, including during construction. Their shared owning-edge topology supports
iterative cleanup, including failed drafts and partially moved values. Each
binding selection creates a new occurrence; projections own their receiver and index.
Function names construct callable occurrences directly. Construction results read
types and constants from their owned expressions. `BodyType` and `BodyFailures` store
construction or resolved facts; their accessors explicitly select the required stage.
Published bodies expose only const access to the completed tree.

Closed declared callable shapes become canonical types during source type resolution.
Generic pointer expressions retain a pending pointee type and its access until
publication resolves them after ordinary type and failure completion.

Provenance resolves an origin directly to `ProgramSourceID` and `Span`.
Only diagnostic transport converts that identity to the source manager domain.

## Canonical stores

Every canonical type store establishes the complete builtin type domain at
construction. Builtin identities remain stable through publication; lookup is a
read-only operation independent of source usage and prior static execution.
Compound types are interned on demand by complete value equality. Published types
retain their insertion order. Generic construction retains parameterized constructor
descriptors whose children are construction type references, and separate symbolic
failure expressions. Pending inference terms stay outside the
canonical stores. Generic environments distinguish type and failure-set
arguments. Instance keys retain their kinds and declaration order, using only
canonical types and failure sets. Shared execution
may consume either a draft or a published program, but type queries have the
same guarantees in both contexts.
Operations use their resolved result types where available.

Numeric parse factories retain one program-owned callable per numeric type.
`NumberParseImplementation` records the constant Invalid case. The callable
signature owns the numeric result type and has one Read `str` parameter and a
singleton failure set; the Invalid constant carries its nominal error type. Calls use `SemCall`, so failure
inference, propagation, handlers, operand sequencing, and ownership use the same
contracts as source functions. `NumberParseError` is a source enum exported by
`std::core`. Ordinary declaration completion supplies
its nominal identity and nullary Invalid case to the builtin parse contract.

Record and enum declarations retain their source module and origin. Matching,
failure handling, display, ownership, and static execution consume their ordinary
declaration contracts. Native representation selection preserves those
identities.

`GenericDeclarationContract` retains the generic definition's source identity.
The standard `Read<T>` enum instantiates through ordinary nominal argument,
canonical instance, visibility, and publication machinery.
`GenericNominalInstance.origin` records the source use position for instance
diagnostics. It does not replace the generic definition's declaration identity
or its source declaration.

Published generic function instances retain their source declaration, canonical
arguments, and concrete callable. Target planning uses these facts for stable
content names and inline definitions at their uses, including when the source
template belongs to a fixed source library.

`Duration`, `RangeError`, and `Timeout` retain their standard-source identities
when used by duration literals and builtin timeout composition. Builtin
operations consume completed declarations through shared readers. The backend
generates their native representations through ordinary source-type realization.

Canonical facts and spellings remain stable across appends; moving or sealing their
owner ends outstanding borrows. Types still return copies during construction.
Constant interning uses a hash index with exact equality checks and deterministic
insertion-order IDs. Floating identity uses bits; numeric equality remains separate.

## Type contents

`SemIRProgram` owns immutable `TypeContents` computed after storage topology
validation. Canonical, construction, and rigid types share one shape rule and
worklist for content propagation. Their readers supply nominal members; generic
record construction and analysis consume the same cached member types.
Containment facts can coexist and propagate according to each property's storage
and borrowing rules. String storage containment excludes zero-length array
elements. Read queries derive storage-borrowing and value-snapshot guarantees
from these facts. Opaque native value containment
contributes to the same storage-borrowing query; compiler-known C string pointers
are value snapshots. Native Read uses C++ copy and destruction traits to select
const values or references. Retained native holders use const references through
the call chain.

Published-program consumers read these facts through an identity-checked query.
Static execution during construction queries the draft's type facts, including
types whose dependencies are still being completed.

The same containment worklist propagates constraints on inline value storage.
Opaque native values, dynamic owners, borrowed views, callable values, operations,
and unresolved parameters prevent this proof. Fixed arrays, Tuples, and nominal
fields propagate their children's constraints. This query is separate from Read
passing: owning an inline array does not itself make its transport observable.
The storage query does not change async admission or establish referent lifetimes.

## Async operations

Canonical operation identity contains the success type and solved nominal failure
set, independently of the producing function. Async execution kind belongs to the
callable contract; its invocation constructs a cold operation. Cold construction,
await, lexical child startup, and compiler-defined async operations have distinct
SemIR forms. Cancellation remains outside nominal failure identity.

Ownership analysis checks consumption, retained backing, child intent, and source
lifetime exits through its existing flow and interprocedural queries. Those proof
states remain private to analysis. Publication delivers the final executable body
under the same semantic gates as ordinary source. Backend preparation borrows that
body to derive coroutine-context and lexical child realization data. The structured
executable body carries the control and lifetime identities used for realization.

Published cancellation facts are derived from finalized executable bodies.
Callable queries describe outward cancelled completion;
await occurrence queries describe the completion of the observed producer.
Expression evaluation and the capability of a produced cold operation are distinct:
constructing cold work evaluates its operands without executing the delayed body.
Bindings, lexical children, and private wrappers retain that producer capability.
Recursive callables are solved to a fixed point; unknown producers may complete
cancelled. These facts preserve canonical operation identity and existing source
ownership and assignment rules. A request or yield alone does not produce
cancelled completion.

Async query, checkpoint, yield, and composition operations participate in
ordinary builtin lookup after visible source bindings and explicit native
selections. Analysis checks the selected operation's contract.
Cancellation statements carry a lexical child's identity. Published bodies retain
the selected async operations and child identities.

`SemWaitReady` retains a fixed list of lexical child identities and has Tuple
result `(usize, bool)`. `SemCloseChild` retains one child identity and has void
result. Both require execution and may suspend, including when their result is
discarded. They are direct awaited controls rather than cold producers. Their
completion is normal; selected child cancellation and failures do not contribute
to the parent's outward completion facts.

## Structured semantics

`SemColdCompose` records a composition policy, ordered Take inputs, and its
successful type independently of the deferred failure union. Static body execution
can use this successful type before inferred failure contracts are published.
Its final result is the canonical operation type with a concrete Tuple success
and the union of deferred input failures. Evaluating the node constructs an
owner without executing its children. Builtin name resolution selects the
composition policy before call elaboration; ordinary declarations with matching
names retain their own contracts.

Bodies retain conditionals, loops, matches, handlers, lexical scopes, and exits.
Places describe storage identity and projection evaluation. During body
construction, `PlaceExpression` carries the selected Read or Write access through
projections to address-taking, assignment, and argument binding. Values describe
computation. Initialization, assignment, and Take remain distinct operations.
Operations retain operand order and access; control nodes specify conditional
execution. Range values retain two ordered integer bounds and an upper-bound
inclusion flag. Range loops retain a single iterable expression. Pattern tables
retain static interval facts; match and catch arms own dynamic bound expressions
keyed by pattern identity. Recursive pattern selection executes those expressions
only when that pattern is attempted. Integer coverage partitions the type domain
at interval boundaries and composes with enum payload and or-pattern coverage.
Coverage searches for a value matched by a query pattern and not covered by a
set of patterns. This set-difference query supplies usefulness, redundancy,
exhaustiveness, and missing witnesses. A wildcard row covers the residual product;
without covering rows, each query column needs only one inhabitant. Catch
construction retains definite coverage per failure type; a preceding catch-all
also covers types first encountered in later arms.

`semantic.semir.children` visits direct child expressions and regions in stored
order, including inactive branches. `semantic.semir.traversal` walks them with an
explicit worklist.
Body construction uses direct children for ordinary effect propagation and
handles conditional execution and operation-local effects explicitly. Backend
adapters assign storage uses to these inputs. New operation kinds require a child
structure definition and an effect rule; fields added to an existing operation
must be included in its child definition where applicable.

### Slice operations

`semantic.semir.slice` defines each slice intrinsic's receiver shape, explicit
argument types, and result. Operands are Read; slice results preserve the receiver's
element type. Method selection, construction, and publication share this contract.
Construction retains extent facts. Ownership checks borrowing; the evaluator and
backend explicitly select each operation's implementation. Range checks and
execution budgets remain with execution.

### Text operations

`semantic.semir.text` defines each text intrinsic's ordered parameter types, access
modes, and result type. Construction resolves contextual types from this contract;
publication checks arity, operand/result types, and Write-place requirements.
`ByteLength`, `IsEmpty`, `Bytes`, and `Chars` accept `str` or `String` receivers;
byte-slice constraints require `[u8]`. Static root admission belongs to analysis,
and execution belongs to the evaluator.

Construction and mutation require evaluation even when their results are discarded.
Contextual String literals and `str as String` publish `FromStr`; contextual
String-to-str borrowing publishes `AsStr`, sharing the explicit APIs' ownership
and lowering paths.
Contextual array/Vector-to-slice borrowing publishes `SliceIntrinsic::View`,
sharing `slice()` storage checks and lowering. Unchecked scalar and UTF-8
construction publish text intrinsics with `u32` and byte-slice inputs respectively.
UTF-8 construction preserves the input storage loans; the caller must establish
content validity.

### Native types and calls

Generic native named applications retain construction type arguments in
`GenericCppNamedType`. Native expression queries use `BasicCppQueryType<Type>`
for calls, construction, static members, projections, and operators. Operands
retain type references and access modes. Substitution maps construction references
to canonical types; validation and dependency collection visit the same query
structure. Published concrete bodies and native lowering use `CppNamedType` and
`CppQueryType`.

`CppImportImplementation` retains the import origin and its global
`CppNameReference` target. Analysis resolves explicit bindings or supplies the
same-named global target before publishing the callable.

### Formatting facts

Formatting data belongs to `semantic.semir.format`. Normalization preserves
literal text, outer holes, and nested specification holes in `FormatSpec`.
Hole indices follow source operand order. `SemFormat` retains this source
specification and every Read operand. Direct `String.append_format` also retains
a Write String receiver selected before the operands; its result is void.
Owning interpolation produces String. Operand indices exclude the receiver.

Expression constant facts come from literals, module constants, and admitted
operations on operands that carry facts. Aggregate construction and field or
element projection preserve these facts. Local bindings supply none in a source
body; specialization replaces a local `const` or static parameter with its
value in the residual region. Format and print
operands retain their source expression trees and execution obligations.

Publication checks source operand order, Read access, result type, and the
String-place requirement for append. SemIR retains the source format and operands;
execution owns result limits and freezing, while backend preparation owns optional
materialization, encoding proofs, and residual formats.

Ownership keeps the receiver's Write access separate from formatting inputs and
enforces ordinary backing lifetimes. Owning formatting constructs independent
storage. Constant-function execution uses the same structured builtin formatter
and accounts for destination growth.

### Calls, bindings, and control

Builtin names use ordinary lookup. Direct calls publish `SemPrint` or
`SemReport` expressions; a builtin used as a value materializes a stateless
callable with its expected signature. Test-stop analysis uses each callable's
executable region, collecting direct stops and possible calls and propagating
effects through reverse call dependencies. Ordinary branches retain their
structural possibilities; explicit static selection contributes only its selected
operations. Callable views conservatively admit test stop.
Construction and publication use the same structural reachability and pattern
coverage rules. Body resolution computes `exits_test` for expressions and regions
from each operation tree and the solved callable effects. Backend consumers read
these completed effects.

Match construction checks coverage over the subject type. Redundant patterns
are unreachable. Each arm records `pattern_may_reject` for normal pattern
completion on entry: earlier unguarded patterns exclude their covered values,
while guarded patterns do not extend definite coverage. The current guard is
separate from its pattern and may still reject. Failure inference, test-stop,
ownership, nullability, and realization consume this common rejection fact.
Publication verifies a no-rejection proof through the same coverage query,
independently of a known subject value. Subject evaluation, dynamic pattern
bounds, guards, and alternative binding selection retain their execution
obligations. Explicit callable failure contracts and source constant-expression
admission remain independent.

Immutable locals initialized from a known function retain its identity through
copies and callable adaptation. Calls publish that target alongside the original
callee expression. Failure and test-stop solving use the target's contract;
ownership analysis still checks evaluation and availability of the callee.
Mutable slots, captured objects, and unresolved selections retain dynamic calls.

Bindings carry their role and access. Scope and full-expression boundaries
record lifetimes. Function return, failure propagation, loop transfer, and test
exit retain their destinations. Nested callables have separate boundaries.

Call argument binding validates Take against the source operand and records
its ownership transfer before target-type conversion. A conversion may produce
a value or borrow, but does not replace the source access requirement.

Match distinguishes a source place from an owned temporary. Pattern owners and
guards retain their order. Source analysis checks both arms of static selection
before specialization removes unselected execution paths.

Statement `reachable` and region `result_reachable` record whether evaluation
can enter that node. False excludes an execution path; true retains a possible
path. Construction combines the enclosing path with structural completion.
Specialization and realization consume these entry facts before processing the
subtree, including loop steps. Source checking retains the inactive operations.

Expression construction composes normal completion from completed operands.
`semantic.semir.completion` computes full exit sets with an iterative postorder
traversal where construction or specialization needs return, break, continue,
failure, and test-stop facts. Sequential statements advance only through normal
completion; loops consume their own break and continue exits. Conditional report
messages contribute exits only on their evaluation path. Ordinary constant
conditions do not narrow the exit set.

Completion borrows the body's pattern tables to follow enum tag selection,
payload checks, and ordered alternatives. A temporary flow retains known thrown
types and unknown call or rethrow failures while selecting catch paths. Pattern,
guard, and handler exits leave that try; test stops are never typed failures.
Exit queries return only their exit set. Pattern construction uses a query scoped
to one match or try to derive accepted and rejected entries from the same flow.
It retains completed pattern values while their bounds are added, and keeps no
addresses into the growing expression tree. These temporary facts are not
published as body metadata.

Specialization follows those entries again after selecting static control.
`SemUnreachable` replaces an expression edge that this stage proves cannot be
entered. It retains the edge's type, origin, and lifetime, has no value or normal
completion, and leaves the pattern's arity and binding structure intact.
Realization emits the existing semantic-proof unreachable operation; execution
of this internal leaf is a compiler invariant failure.

### Callable adaptation

`semantic.semir.callable` classifies completed callable adaptations as target
copy, function target, stateless closure, or object borrow. Ownership and backend
preparation share this query; array elements use the same classification.

## Static bodies and instances

During analysis, a staged declaration retains its checked source body. Each
static instance records its function, normalized static arguments, and a distinct
`CallableID` with its own `BodyID`. Its signature contains only runtime
parameters. Instance lookup uses the function and static argument identities.
Callable definition
placement determines whether a definition belongs to its owner artifact, each
using artifact, or has no executable implementation.

The instance reservation owns one pending, completed, or failed state. Repeated
requests retain the completed callable or the original construction failure.

A body under analysis stores its checked region and, when needed, one residual
region. Bodies that need no static rewriting retain their region directly.
Semantic tree copies use `semantic.semir.clone` to construct child storage and
iteratively fill it in place while retaining semantic identities. The shared
direct-child structure pairs source and destination storage. Instance and
iteration construction own local-table copying and remapping. After source
validation and surface collection, publication discards checked alternatives and
retains exactly one executable region per body.
Source templates, static tests, and module static blocks publish no body.
Their declarations and diagnostic provenance remain available. Removed bodies
leave vacant IDs; surviving body IDs and local table identities do not change.
Declarations and tests clear references to removed bodies.

Residual installation transfers its region and extended local tables together,
retaining the checked region and body inputs. The boundary checks table ownership
and source table ranges.

Instances own copies of the source binding, lifetime, and pattern tables.
Local IDs retain the copied tables' body identity; expanded iterations append
distinct IDs. Source ownership and nullability checks run before source regions
are discarded.

Checked source bodies store local static roots as `SemStaticBinding`, which owns
its typed initializer at a stable tree edge. The binding records the frozen local
type; the initializer records its execution result type. These types agree;
retained owning results keep their source type. Construction finds a root by its
binding identity and can read its value through a dependency-query execution
context; the initializer stays in place. Queries suppress output and cannot
perform host clock, input, or waiting operations. Completed module constants are
ordinary frozen value references rather than operations to replay.
Root failures and ownership transitions belong to static execution, independently
of the surrounding runtime region.

Residual regions contain ordinary semantic operations. Static bindings are
constant expressions; explicit static branches are selected regions; expanded
loops contain ordered iteration regions. A residual `SemCall` directly targets
the selected callable and contains only runtime arguments. Execution and
realization select its executable body through the ordinary callable interface.
`SemConstBlock` holds the region of a `const` block written in a body.
`SemStaticBinding` and `SemConstBlock` are consumed during specialization and
rejected at residual publication. Runtime initialization remains a separate
`SemInitialize` operation with an initializer of exactly the binding type.
Structural traversal includes static initializer roots; ordinary
evaluation-child traversal excludes them.

Publication records each source template's callable and type surface after
validation, together with closure reference order for each callable.
Artifact planning consumes this source-independent metadata to retain provider
dependencies and stable closure names without traversing template bodies or
depending on which instances callers requested.

## Known results and execution requirements

Constants have one normalized `ConstantFact` representation and `SemConstant`
occurrences. Module constant declarations retain binding metadata and a
`ConstantID`; the fact owns the type. Each occurrence records whether its
constant selects retained storage. Compound owners and owning String values expose
persistent Read storage; numeric, character, and pointer constants remain scalar
expressions, including in native construction queries. Value delivery reconstructs
contents independently of that Read storage.
Floating-point identity uses bits; language equality compares
numeric values and recursively compares aggregate contents. An expression’s
constant fact describes its value on normal completion. Syntax admission and
execution requirements are checked separately; known results retain required
operands and effects. Publication checks that every attached normal-completion
fact belongs to this program and has the expression's exact resolved type.

Constant facts serve folding, generated code, and proven normal-completion truth.
Ownership and pointer nullability evaluate condition effects and exits before
using an existing Boolean fact to select normal successors. Syntax reachability,
type, failure, and return admission remain structural and check every source
shape. A match arm's
`pattern_may_reject` is instead a structural coverage fact over the subject type
after preceding unguarded patterns, independent of the subject's constant fact.
Integer arithmetic wraps in both required and runtime execution.

`SliceIntrinsicOperation.result_extent` records a slice's length on normal completion.
Array borrowing uses the array type's extent, including for mutable array owners.
Subslice construction records `end - start` for known unsigned bounds with
`start <= end`, even when the receiver length is unknown. The checked slice must
still succeed; a known result length is not a bounds proof.

Immutable local views and their copies retain extent facts. Take preserves the
transferred extent and the ownership transition. Mutable slice slots, dynamic
bounds, unknown parameters, native results, control-flow joins, and
interprocedural propagation supply no additional extent facts. Extent metadata
describes storage shape and does not make a runtime receiver's `length` or `is_empty`
query a static expression. Queries retain receiver execution, bounds checks, and
backing loans. Publication requires array views to report the exact array extent and
rejects extent metadata on scalar query results. Extents do not change slice type identity.

`SemPrint` retains the original Read operands and normal-completion facts.
Optional conversion of known numeric, bool, or char values into print text
belongs to backend preparation. Text, String, unknown scalars, and dynamic
calls through builtin callable values keep their ordinary conversion paths.

The read-only SemIR evaluation contract classifies an operation as requiring
execution, requiring only its executed operands, selecting short-circuit
operands, or requiring no execution when discarded. It consumes resolved
operations, types, and constant facts. Storage reads are separate from execution
requirements, so a removable read can still require a snapshot before a later
mutation. Calls and native operations are conservative; checks and ownership
operations retain execution. Discarded scalar and SIMD arithmetic retain required
operand effects; the unit value has no construction effect. Whether a result can
be precomputed is a separate decision. The backend prepares and
consumes body-local execution, storage-read, lifetime, and exit summaries during
body realization. Shared type rules consume
the relevant stage's facts.

Known nonzero integer divisors and in-range shift counts remove the operation's
termination obligation. Discarded operations still evaluate required operands.
This fact is independent of whether C++ can express the value with a native
operator, including signed minimum divided by negative one.

## Program validation

Construction establishes semantic facts before publication.
Local construction checks its preconditions. Program validation checks owner
and range relations, type and call contracts, lifetime and control legality,
binding relations, declaration topology, and cross-body callable relations.
Each declaration and body has its required unique owner. A lexical closure has
one callable and one body; construction expressions create values of that type.
An unchanged executable region checks source contracts and executable constraints
in one traversal. Separate checked and rewritten regions each retain their
applicable checks; rewritten native construction may establish additional value
witnesses without changing the checked query's type identity.
