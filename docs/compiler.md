# Compiler architecture

This document describes semantic construction and analysis, including publication
gates, ownership boundaries, and dependency direction, from the closed source
batch through publication of an immutable semantic program.

## Pipeline

`compiler.request` defines the closed source batch using source types.
`compiler.analysis` sequences parsing and semantic analysis through
`analyze_compilation`, returning a published program and structured diagnostics.
`compiler.compile` calls this entry and generates artifacts. Execution output is
delivered synchronously to the supplied recipient.

`driver/` owns command options, file loading, diagnostic presentation, artifact
output, and native process execution. `load_and_analyze_sources` prepares the batch,
calls `analyze_compilation`, and renders diagnostics using the source manager.
Compile and run commands send the program to the backend; interpret sends it to
the interpreter. Dump commands consume lexical or syntax results directly.

```text
CompilationRequest → SyntaxProgram → ProgramDraft → SemIRProgram
```

`parse_program` parses the closed source batch and resolves module imports.
`analyze` constructs declarations and typed structured bodies, solves types and
failure sets, validates contracts, checks ownership and callable loans, and
publishes an immutable semantic program. Errors prevent delivery; warnings accompany
a successful result.

## Design considerations

When extending the language, first consider whether existing semantic operations
and construction paths can express the feature. Reusing a path can also reuse its
typing, ownership, failure handling, and realization rules. New syntax may select
existing operations; a new context may add admission or completion requirements.
For example, `const fn` shares ordinary function-body construction. Admission
checks the completed body, and required evaluation executes that body.

At stage boundaries, consider which layer has the information and native
mechanisms needed for a decision. Carven retains source evaluation, ownership,
and failure contracts. C++ types, initialization, scopes, and template constraints
can realize those contracts and resolve native type properties. Types and explicit
interfaces can carry the required distinctions and check the handoff. Structured
operations preserve information useful to both source analysis and C++ generation;
additional representations should serve a concrete semantic or implementation need.

Assess reuse by the semantic rules shared and the coordination an extension
requires. Evaluation order, diagnostics, ownership, and supported behavior are
part of that assessment. New control flow, storage lifetimes, or completion
requirements may call for extending or revising the underlying contracts. These
considerations guide exploration; the appropriate boundary depends on the
feature's semantics.

Generated code can use established semantic facts to express the required
behavior directly. Where type context and native C++ constructs satisfy the
contract, prefer those forms. Additional storage, conversions, and helper calls
should serve a concrete evaluation, lifetime, or representation requirement.

## Subsystem ownership

Directories group semantic responsibilities. Construction requests constant
execution when its results determine declaration types or array extents.

| Owner | Responsibility |
| --- | --- |
| `frontend/` | Source syntax, literals, parsing, and the closed syntax program |
| `semantic/analysis/` | Contextual typing, declaration and body completion, source admission, diagnostics, and validation |
| `semantic/evaluation/` | Typed execution values, constant operations, bounded structured execution, and freezing |
| `semantic/semir/` | Semantic operations, structured format data, canonical values, identity, and publication contracts |
| `semantic/format/` | Source format syntax, serialization, and bounded builtin formatting of observed values |
| `backend/preparation/` | Optional precomputation, runtime specialization, and encoding/size proofs |
| `backend/` | Representation selection and C++ realization from published semantic facts |
| `crafts/carven/runtime/` | Shared native support for language operations under their semantic contracts |
| `crafts/carven/std/` | Standard-library APIs, algorithms, containers, and their native implementation support |

Within `analysis/constant`, `literal` normalizes source literals, `admission`
checks the supported const-function definition subset, and `evaluation` connects
required evaluation to construction requests and source diagnostics.
`analysis.constant.root` constructs typed initializer and extent roots and owns
their construction state, lifetimes, admission policy, and budgets. `analysis/expr`
shares contextual typing and typed operation construction between required roots
and ordinary bodies. Expression sites supply source scope, admission, value
consumption, lifetime, and failure effects. Required roots and const-function
bodies have distinct admission rules. `analysis.expr.interpret` dispatches syntax
to `scalar`, `member`, and `call` handlers, which recurse through the expression site.
In `semantic/evaluation`, `value` owns execution representations, text and compound
read views, and equality over execution values. `shape` caches supported compound element types,
nesting depth, and retained slot count. Counts saturate at the element limit plus one;
size admission checks that count separately. Incomplete declarations and types beyond
the depth limit, including containment cycles, produce no cache entry. Cached subtrees
remain subject to the caller's nesting depth. `operation` supplies
checked scalar operations and retained-input queries. `freeze` interns completed
results. `semantic.format.builtin` supplies the bounded builtin formatter to both
execution and backend preparation. It accepts integers, bools, chars, borrowed
text, and an unavailable-input alternative. Callers adapt their input storage and
supply a byte budget. Execution translates failures into diagnostics and accounts
for work; optional preparation uses runtime formatting on failure.
`execution` defines the required-evaluation entry and call context; `executor` owns execution
state, with `expr`, `control`, and `text` implementation slices. `limits`
names resource bounds.

`semantic.semir.constant_access` separates immutable `ConstantValueReader` queries
from `ConstantValueAccess` construction writes. `ProgramDraft` implements construction
access; `PublishedConstantValues` adapts a sealed program for read-only consumers.
Canonical facts and spellings remain stable across appends; moving or sealing their
owner ends outstanding borrows. Types still return copies during construction.
Constant interning uses a hash index with exact equality checks and deterministic
insertion-order IDs. Floating identity uses bits; numeric equality remains separate.
`SemanticExecutionContext` supplies callable lookup and completed typed bodies.
The analysis adapter builds a provenance-source requester index on the first call
within a root evaluation and caches completed body/parameter descriptors. It does not cache
call results or provisional completion failures.
Execution delivers structured diagnostics with semantic origins and its call
trace through that context. The adapter submits them immediately, preserving
multiple errors and their order. A dependency failure already diagnosed by its
construction owner is propagated without an additional diagnostic.

The structured executor consumes typed semantic operations and an execution context.
The analysis adapter supplies declaration completion and diagnostics. Evaluation
depends on semantic representations; its interfaces contain no analysis, AST, or
target syntax types. Backend preparation consumes published constants and operation
facts.

### Callable result completion

Ordinary functions and lambdas share return construction for block and expression
bodies. Declared or context-supplied results provide return type context. Inferred
results check independently typed returns for invariant compatibility. Missing
returns and result-inference cycles are checked during body and signature
completion, before constant admission. Constant execution consumes the completed
contract; call arguments do not specialize the function's result type.

### Expression construction results

`analysis.expr.result` defines `ExpressionResult<T>`, which carries `T` on success.
Failure carries either an existing `AnalysisFailure` token or
`ExpressionNotAdmitted`, which has no diagnostic yet. Required initializer, enum,
and extent consumers diagnose non-admission in their own source context. Ordinary
body construction reports invalid source forms directly and passes only diagnosed
failures back to body analysis. Constant-name lookup returns an optional constant
identity; its type comes from the canonical constant fact.

`analysis.expr.operand` resolves a call argument's outer access marker and operand.
An unmarked argument has Read access. Calls and C++ construction use this result;
parameter access checks report mismatches in their own contexts.

Index construction checks the index expression when its receiver is not admitted;
a diagnosed receiver failure stops construction. Index type and bounds rules
require both operands to succeed. Binary contextual typing may require constructing
the right operand first.

### Craft implementation boundary

Standard crafts express `.cv` implementations through Carven declarations and
operations. Lowering supplies the native dependencies of language operations.
Runtime header imports and runtime implementation names are reserved for C++
support source.

A craft owns its library API and may include C++ implementation support alongside
its source modules. That support may include runtime headers. Runtime provides
shared language facilities and does not depend on standard crafts.

Standard and user crafts use the same type, ownership, constant-execution, and
publication rules. Library identity comes from resolved declarations. Native C++
imports retain their delegated contracts.

## Publication gates

The analysis driver collects declaration identities, then `analysis.construction`
owns `ProgramConstruction`, which coordinates declaration resolution and body
elaboration. Its `ConstructionRequests`
interface provides completion requests for declarations, function signatures,
function bodies and type dependencies. Declaration resolution and required
constant execution request bodies through this interface; the coordinator owns
the declaration resolver and body elaborator.

Function results are completed on demand. A dependency on an active, unknown
result produces an inference-cycle diagnostic. Known signatures support recursive
calls. A required constant call requests a completed typed body; requesting an
actively elaborated body diagnoses an unfinished-body dependency. Execution
recursion uses completed bodies under the evaluator's call-depth limit. Each body
is elaborated once; later requests reuse its completed or failed result.

`ProgramDraft` owns pending function heads. A complete callable contract includes
its result. Declaration-head completion closes the nominal tables; solving requires
complete callable contracts and bodies. The catalog and import-use state end before
solving. Final semantic validation checks declaration surfaces, including closure
captures and solved failure sets.

`analysis.program` owns `ProgramDraft` and declaration/body reservations.
Its consuming `finish()` checks reservation completeness, solves failures and
types, and finalizes callable signatures, declarations, and bodies in that order.
It then computes test-stop effects per callable before publishing the program.
Body completion resolves type and failure facts in the owned operation tree and
finalizes binding and pattern tables. Identity array adoptions are removed while
preserving the source storage and the consumer's access.
The `SemIRBody` boundary requires resolved facts in every operation and region,
including inactive source.

Structural type terms reference only previously appended terms. Canonicalization
consumes them in storage order into one final type mapping after failure solving.
Closed declared subtypes can be interned earlier through the same construction
operation; that path accepts no inferred failure terms.
Callable recursion and recursive failure constraints retain their own identities
and solving rules.

`finish()` constructs a local `SemIRProgram`, releases the consumed draft and its
syntax, imports, and construction solutions, then checks that final program.
Checks run in this order: program facts and topology, body contracts, global
semantic contracts, ownership, then local pointer nullability. All checks read
`const SemIRProgram&`.
Source diagnostics use a separate channel. Body contracts establish the parameter
and binding relations used by global checks. Successful checks deliver the program.

Backend body preparation checks the execution and control relationships it
introduces at completion. It borrows frozen operations and checks membership in
the published lifetime and pattern tables. Semantic publication owns type and
ownership analysis; the backend owns realization invariants and Target verification.

Pointer nullability analyzes structured operations locally and merges slot facts
across normal and abrupt exits. Indirect places check address availability and target
access without assigning a local owner to the referent. Pointer targets are
leaves for owned-content, loan-content, and infinite-size containment queries.

## Ownership and identity

`SyntaxProgram` owns source provenance, syntax trees, and resolved imports.
`ProgramDraft` consumes it and owns mutable declarations, canonical interning,
construction types, failure constraints, and body construction. Canonical type
interning compares complete values before reusing an ID. Published types retain
their insertion order. Declarations may reserve identities for recursion and
forward references. Each callable body has one owning callable; the immutable
declaration store retains that relation for lookup.

Program IDs belong to one program. Binding, pattern, and lifetime IDs belong
to one body. Name frames exist only during name resolution; bound operations
retain binding identities and lifetimes. Owning query surfaces validate identity
and range. Expression, statement, and region occurrences are recursively owned
values, including during construction. Each binding selection creates a new
occurrence; projections own their receiver and index. Function names construct
callable occurrences directly.
Construction results read types and constants from their owned expressions.
`BodyType` and `BodyFailures` store construction or resolved facts; their accessors
explicitly select the required stage. Published bodies expose only const access to
the completed tree.

Solving consumes construction state. `SemIRProgram` retains resolved
semantic data and provenance; lowering does not query source syntax. Program,
provenance, plan, and table owners permit move construction where required, but
not replacement through move assignment. Borrows end before an owner moves or
is consumed. Construction readers return copies because storage may grow;
final views borrow immutable storage. Query boundaries check identity and bounds;
construction boundaries check unique definitions and structure.

Provenance resolves an origin directly to `ProgramSourceID` and `Span`.
Only diagnostic transport converts that identity to the source manager domain.

Within the pipeline, `draft` names mutable `ProgramDraft` state, `semantic` names
published `SemIRProgram` data, and `compilation` names a `PlannedCompilation`
owner. Source-module and semantic-module identities remain distinct. Failure
terms, solved failure sets, and control-flow completion name separate facts.

## Structured semantics

Bodies retain conditionals, loops, matches, handlers, lexical scopes, and exits.
Places describe storage identity and projection evaluation. Values describe
computation. Initialization, assignment, and Take remain distinct operations.
Operations retain operand order and access; control nodes specify conditional
execution. Range sources select integer bounds or a sequence expression.

`semantic.semir.children` visits direct child expressions and regions in stored
order, including inactive branches. `semantic.semir.traversal` supplies recursion.
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
`Len`, `IsEmpty`, `Bytes`, and `Chars` accept `str` or `String` receivers;
byte-slice constraints require `[u8]`. Constant admission belongs to analysis,
and execution belongs to the evaluator.

Construction and mutation require evaluation even when their results are discarded.
Contextual String literals and `str as String` publish `FromStr`; contextual
String-to-str borrowing publishes `AsStr`, sharing the explicit APIs' ownership
and lowering paths.
Contextual array-to-slice borrowing publishes `SliceIntrinsic::FromArray`,
sharing `as_slice()` storage checks and lowering. Unchecked scalar and UTF-8
construction publish text intrinsics with `u32` and byte-slice inputs respectively.
UTF-8 construction preserves the input storage loans; the caller must establish
content validity.

### Formatting facts

Formatting data belongs to `semantic.semir.format`. Normalization preserves
literal text, outer holes, and nested specification holes in `FormatSpec`.
Hole indices follow source operand order. `SemFormat` retains this source
specification and every Read operand. Direct `String.append_format` also retains
a Write String receiver selected before the operands; its result is void.
Owning interpolation produces String. Operand indices exclude the receiver.

Expression constant facts describe values on normal completion, independently of
execution and ownership obligations. Body construction remembers bounded integer,
bool, char, and literal-backed str facts for immutable local owners and direct
copies. Format and print operands publish those facts on their original expression
trees; captures and parameters do not inherit them.

Publication checks source operand order, Read access, result type, and the
String-place requirement for append. It contains no implementation preparation
states. Required initializer and extent evaluation execute the same source
`SemFormat` directly with execution-local values, a 1 MiB result-text limit, and
source diagnostics for unsupported conversions. Initializer freezing is a separate
boundary. Optional formatting choices, encoding proofs, residual formats, and their
64 KiB materialization budget belong to `backend/preparation` (see backend.md).

Ownership keeps the receiver's Write access separate from formatting inputs and
enforces ordinary backing lifetimes. Owning formatting constructs independent
storage. Constant-function execution uses the same structured builtin formatter
and accounts for destination growth.

### Calls, bindings, and control

Builtin names use ordinary lookup. Direct calls publish `SemPrint` or
`SemTestReport` expressions; a builtin used as a value materializes a stateless
callable with its expected signature. Test-stop analysis follows direct calls
by callable identity and conservatively admits test stop through callable views.

Bindings carry their role and access. Scope and full-expression boundaries
record lifetimes. Function return, failure propagation, loop transfer, and test
exit retain their destinations. Nested callables have separate boundaries.

Call argument binding validates Take against the source operand and records
its ownership transfer before target-type conversion. A conversion may produce
a value or borrow, but does not replace the source access requirement.

Match distinguishes a source place from an owned temporary. Pattern owners and
guards retain their order. Constant-inactive source is validated but contributes
no executed operations or ownership transitions.

## Solving and validation

Canonical types, callable signatures, constants, and failure sets are the
published query surfaces. Construction types and failure terms are solved
before publication. Failure inference computes the least fixed point of the
program's failure constraints. Numeric enum underlying types are concrete during
declaration resolution; structure fields and enum payload types require
construction-type resolution.

`analysis.expr` interprets each expression once, with concrete constant and body
sites. The interpreter owns contextual typing, operation selection, enum and
text rules, and diagnostics. Sites supply scope lookup and handle execution-only
syntax. Constant-expression admission is checked separately from the computed
value; declarations retain lazy completion and cycle diagnostics.

### Constant execution and freezing

Initializer and extent construction use the same typed semantic operations as
function execution. Their transient lifetime table has an independent
`BodyIdentityDomain::EvaluationRoot` identity in the current program; it does
not reserve or publish a function body. The table lives through evaluation.
The expression adapter owns source admission, contextual conversions, shape
checks, and their diagnostics. Shared interpretation suppresses optional
computed folding for these roots; the executor evaluates their operations.

Each required root and its nested calls share one execution budget. Type and
admission checks cover all source branches; bounds and other execution checks
run on the selected paths. Queries on retained aggregate inputs do not materialize
a complete mutable copy. Sequence queries use compound views; slicing retained
input constructs the selected range of slots. Projection can retain child identities,
copy from local storage, or move from temporary owners according to the operand's
storage. Local initialization, assignment, and parameter storage materialize retained
aggregate children into independently mutable values.

Execution frames hold owned values or aliases to an owner slot and a projection
path. Reads and writes resolve that path through the same storage operations.
Array iteration evaluates its source once, retains temporary owners until loop
exit, and binds elements using the source Read or Write policy. Scalar Read
bindings snapshot each element; borrowed aggregate bindings retain its location.
Nested loops compose projection paths. Loop exits release their bindings and
retained temporary storage, including early returns and execution failures.

`const fn` execution consumes `StructuredBodyDraft`, after ordinary contextual
typing, name binding, operation selection and result completion.
`analysis.constant.admission` checks
the completed signature, bindings, patterns and entire operation tree against a
bounded builtin and aggregate subset before the body enters the
draft, including uncalled definitions and inactive source. These bodies also
undergo normal semantic and ownership validation.

`evaluation.execution` executes admitted operations using body-local slots,
structured control flow and direct callable identities. It reuses checked scalar
constant evaluation and the builtin constant-formatting implementation. Steps,
nested calls, text construction, aggregate size and copying work are bounded.
`ExecutionLimits` supplies cumulative step, text-work, and aggregate-work
allowances at the execution entry. Nested calls share those allowances; each root
starts fresh. Per-value size, aggregate depth, and call depth retain fixed limits.
Text work counts constructed, copied, and appended bytes; formatted append charges
its completed format result and the destination append. Aggregate work counts
constructed and copied slots, including nested children. Reads of retained values
and storage transfers create no additional slots.
Evaluator state and call stacks belong to one required root and end with that
request. Existing `ConstantID` values borrow retained inputs. Computed `ConstantAtom`
values remain execution-local and cannot carry compound storage; they hold scalar
data or an immutable text identity. Immutable `ExecutionText` shares owned bytes;
`ExecutionOwnedText` preserves String ownership across calls. Formatting,
queries, and equality read these values without freezing them. At the initializer
boundary, owning String becomes canonical `str` whose bytes no longer depend
on evaluator storage.
Execution slots distinguish uninitialized, available, and taken states. Whole-binding
Take moves owned storage and marks its source unavailable; assignment may initialize
that source again. Aggregate values own typed elements; enum values separately own
their selected case and payload. Fixed arrays and
structs freeze recursively into `ArrayConstant` and `StructConstant` children
without changing element types, nominal identities, or field types. Struct fields
use declaration order. Publication verifies the type, arity, and exact child
types; compound constants reference already interned children.

At a constant initializer, an explicit slice destination or `as_slice()` can
produce an execution-local slice and retain it through `evaluation.freeze`.
The selected freeze preserves each element's type. `SliceConstant` records ordered canonical element IDs under
a `SliceTypeValue`; it contains no host address, allocator state, or capacity.
Store construction checks child identity and availability; publication checks the
slice type and exact child types. Initializer slice queries operate on retained
inputs or execution-local contents without intermediate freezing. Slice execution inside `const fn` remains unsupported; runtime views
retain ordinary ownership checks.

Execution places identify a local slot and an already evaluated field/index path.
This representation remains valid when element vectors are replaced. The shared type
contents query selects delayed Read observation for array and String storage,
including array-bearing structs. Other admitted values capture their value at
the source position. Constant and runtime constructors share array shape rules
and struct initializer selection in `analysis.operations`.

After constant execution, the bodies remain available
for normal failure solving, complete-program type and ownership validation, and
runtime lowering. A failure at any publication gate prevents artifact delivery.
Ordinary runtime calls to `const fn` remain `SemCall` operations; the qualifier
does not add a known call-result fact.

### Known results and execution requirements

Constants have one normalized `ConstantFact` representation and `SemConstant`
occurrences. Module constant declarations retain binding metadata and a
`ConstantID`; the fact owns the type.
Floating-point identity uses bits; language equality compares
numeric values and recursively compares aggregate contents. An expression’s
constant fact describes its value on normal completion. Syntax admission and
execution requirements are checked separately; known results retain required
operands and effects. Publication checks that every attached normal-completion
fact belongs to this program and has the expression's exact resolved type.

`SemSliceIntrinsic.result_extent` records a slice's length on normal completion.
Array borrowing uses the array type's extent, including for mutable array owners.
Subslice construction records `end - start` for known unsigned bounds with
`start <= end`, even when the receiver length is unknown. The checked slice must
still succeed; a known result length is not a bounds proof.

Immutable local views and their copies retain extent facts. Take preserves the
transferred extent and the ownership transition. Mutable slice slots, dynamic
bounds, unknown parameters, native results, control-flow joins, and
interprocedural propagation supply no additional extent facts. Known `len` and
`is_empty` results retain receiver execution, bounds checks, and backing loans;
they do not expand source constant-expression admission. Publication requires
array views to report the exact array extent and rejects extent metadata on
scalar query results. Extents do not change slice type identity.

`SemPrint` retains the original Read operands and normal-completion facts.
Optional conversion of known integer, bool, or char values into print text belongs
to backend preparation. Text, String, floating-point values, unknown scalars, and
dynamic calls through builtin callable values keep their ordinary conversion paths.

The read-only SemIR evaluation contract classifies an operation as requiring
execution, requiring only its executed operands, selecting short-circuit
operands, or requiring no execution when discarded. It consumes resolved
operations, types, and constant facts. Storage reads are separate from execution
requirements, so a removable read can still require a snapshot before a later
mutation. Calls and native operations are conservative; checks, ownership
operations, and floating computations retain execution. The backend prepares and
consumes body-local execution, storage-read, lifetime, and exit summaries during
body realization. Shared type rules consume
the relevant stage's facts.

### Program validation

Local construction checks its preconditions. Program validation checks owner
and range relations, type and call contracts, lifetime and control legality,
binding relations, declaration topology, and cross-body callable relations.
Each declaration and body has its required unique owner; each closure has one
construction site and one body.

## Ownership and backing relationships

An ownership flow has an optional normal completion containing its state and
result relationships. Every exit carries a state. Return exits carry result
relationships, failure exits carry a failure type and payload relationships, and
loop transfers distinguish break from continue.

An ownership batch prepares local object descriptions, relative temporary
positions, lifetime membership, and pattern acceptance once for each final body.
Type contents are prepared once from the final type and declaration stores,
without borrowing construction state. Coverage borrows pattern tables and stage
type facts; exhaustive queries and full diagnostics share its algorithm. Catch
acceptance describes the current arm, independently of coverage accumulated by
earlier arms.

Queries distinguish body-local objects from caller inputs. Availability belongs
to an owner; holder relationships belong to storage positions within that owner.
Query inputs describe aliases, accesses, availability, relationships, and
execution state.
Known field and element writes replace the relationships at that position;
unknown element writes merge possible relationships. Type contents recursively
identify Carven storage owners (arrays and Strings), closure owners, and callable
views. Native template arguments propagate callable-view restrictions. Native
Read passing is selected from C++ copy and destruction traits.

Constant execution queries the same type contents over completed declaration
fields during construction. This supplies storage observation rules for its
admitted types; native copy and destruction traits remain C++ responsibilities.

Expression results carry selected storage separately from their value's contained
relationships. Bindings and projections select existing objects; owned value
results establish temporary storage. Copying into a destination copies contained
relationships, not the source object's identity. Read parameters and range bindings
use the shared resolved-type storage policy to retain selected objects, including
multiple possible backing objects of a slice. The backend consumes that same policy.

Storage loans record known Carven backing separately from callable loans and Write
captures. Backing can select projected owner storage. Literal storage needs no
loan; an empty storage-loan set makes no claim about native storage lifetime.
Named holders follow lexical lifetimes. Temporary owners retain their contained
relationships until their lifetime region ends; consumers also carry the
relationships of the values they receive. Callable borrowing uses the selected
backing object's lifetime to distinguish full-expression storage. Actual writes check
overlapping live loans; assignment checks after its RHS completes.

Value captures carry copied relationships. Write captures refer to live
storage, so consumers follow the target's current contents. Lexical regions and
full expressions release their own objects on each normal and control exit.
A result's destination lifetime does not change the execution position.
Lifetime exits check that returned and failed borrowed values retain live backing.
Catch selection and guards hold the original failure independently of copied
bindings; rethrow forwards its payload relationships.

Calls map parameters and captures to actual storage, preserving Read storage aliases.
Unpassed holders that constrain reachable storage backing contribute reader loans
without introducing holder identities into recursive queries. Passed Write
holders retain their identity so exact replacement can release their loans.
Aliases share one state, including the order of external writes. Call answers
contain returned relationships and external state for normal and typed failure
completion;
completed locals are discarded. Equivalent query inputs share answers. Input
normalization preserves reachable storage, aliasing, access, and relative
lifetimes. Diagnostic provenance selects a deterministic witness without becoming
part of semantic identity.

The ownership solver tracks dependencies between call queries. Recursive calls
read the current answer; changed answers trigger dependent analysis. Dependencies
include unfinished answers and persist when call contexts change. Answers are
not assumed to grow monotonically: a new context can temporarily remove an exit.
Solving ends after all new queries and changed answers have propagated. Diagnosis
then reads sealed answers without creating queries or mutating results. This
state remains private to semantic analysis.

Unfinished calls retain direct place and borrowed-target accesses. Array iteration
retains its source owner. Match guards
additionally require stable subject storage. Branches merge only real successors; loops include entry,
backedges, and exits. Diagnostic witnesses do not distinguish execution states.
Return, failure, and test-stop states have separate transfer paths. Test stop
propagates through Carven calls to the active test body.
Equal callable-view copies retain their target relationships rather than borrowing
the intermediate view storage. Expired callable backing is diagnosed before any
attempt to interpret its former capture state.
Loop diagnosis uses the converged loop-entry state.
All source operations receive contract checks independently of execution-state
analysis, including unreachable source.

## Diagnostics and dependencies

Source operations retain origins. Implicit operations use linked expansion
origins. Diagnostics are produced at the rule owner. Compiler-private invariant
failures terminate at the violated boundary.

Frontend facilities depend on source and syntax. Semantic construction consumes
syntax; SemIR owns data, storage, and structural contracts without depending on
AST or analysis. Post-solve analysis consumes final operations. The backend
consumes published semantics. Runtime support implements native operations;
build orchestration supplies source batches and native build inputs.

## External C++ delegation

Type and value lookup share external name admission, identifier validation and
import-use recording in semantic name analysis. `CppNameReference`
combines a lookup path with its declaration environment; global paths also
retain the context module. Explicit import selections end after name resolution
and import-use diagnostics. Published header dependencies retain namespace openings.

Body construction distinguishes typed results from external name and member
selections. A selection has no object type and may denote an overload set.
Value, place and call consumers consume selections before publication.
Published calls explicitly distinguish named, member and typed-value callees.

`analysis.body.delegation` shares argument elaboration between C++ calls and
construction. Elaboration resolves the source access marker, consumes a value or
place, and records whether the argument completes normally. Both operations
inherit argument completion; native overload and constructor selection remain
delegated to C++.

External types are named type expressions, intrinsic types, or `CppQueryType`
descriptions. Queries describe C++ expression shapes using operand types and
access.
Identical descriptions share a type record; this does not establish equivalence
between different C++ type expressions. Operation occurrences own diagnostics,
lifetimes and execution order. Query references describe type dependencies,
not the contents or ownership relationships of a result object.

Semantic construction owns result-query derivation for external operations.
Publication checks reference integrity and Carven-owned operation contracts.
Ownership analysis checks access, known storage and callable borrows, and Write
captures. C++ determines the validity and results of delegated native operations.
Native retention, returned aliases, and indirect storage obey the provider/caller
contract. Native results, including representation conversions, establish no
inferred storage loans. Native Write view slots retain their possible old storage loans.

## Compile-time output and tests

Constant execution delivers output bytes and a stream selection through its
context. The analysis adapter forwards them to the synchronous `ExecutionOutput`
recipient supplied to `analyze` or `compile`; an omitted recipient discards output.
The executor owns evaluation order and accounts for output and failed-check
diagnostic text within its cumulative text-work budget. The driver owns
stream presentation. Ordinary output is separate from error diagnostics and is
not stored in the published semantic program.

`const test` bodies use ordinary test construction and shared constant-body
admission. The body batch executes them once after all bodies have been constructed,
using the existing declaration-request adapter and a fresh executor per test.
Required initializers execute when declaration or body construction requests them;
these outputs can precede static tests. Later validation can still reject the
program. Failed checks report diagnostics while allowing execution to continue;
requirements stop the root through the executor's failure transport. Test errors
prevent publication. Published test declarations retain their explicit execution
stage so target planning selects only runtime tests.

## Interpreted execution

`interpreter/` consumes a published `SemIRProgram`. It validates the interpreter
operation subset from the entry through direct callees, then invokes the shared
structured executor. Ordinary
language analysis remains the authority for names, types, access, lifetimes,
failures, and entry uniqueness; interpretation does not introduce an AST checker.

The executor borrows construction or published bodies through `ExecutionBody`.
Execution needs read access to canonical values, compound type facts, and builtin
types through `ExecutionValueAccess`. `ConstantValueAccess` additionally permits
interning completed constants and spellings during construction and freezing.
Published execution does not mutate semantic tables or clone operation trees.

The call context supplies bodies, output, diagnostics, and optional source trace
events. Required constant execution admits const functions and uses checked
integer arithmetic. Interpretation admits supported ordinary functions and uses
runtime integer arithmetic. Output and errors follow the selected stage; the
normal compiler has no interpreter-mode branch. The driver owns command options
and diagnostic presentation.
