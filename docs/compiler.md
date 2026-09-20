# Compiler architecture

This document describes semantic construction and analysis, including publication
gates, ownership boundaries, and dependency direction, from the closed source
batch through publication of an immutable semantic program.

## Pipeline

`source.batch` defines `SourceBatch` and its `SourceModuleInput` records using
source identities and canonical module paths.
`compiler.analysis` sequences parsing and semantic analysis through
`analyze_compilation`, returning a published program and structured diagnostics.
`compiler.compile` calls this entry and generates artifacts. Execution output is
delivered synchronously to the supplied recipient.

`driver/` owns command options, file loading, diagnostic presentation, artifact
output, and native process execution. `driver.process` owns POSIX/Windows
process launching, executable lookup, and temporary run-directory creation.
`driver.sources` owns executable-relative Crafts lookup and collection from
the toolchain and working-directory Crafts roots. It supplies physical source paths
and resolved module identities to checking, C++ generation, and execution.
It recursively collects all `.cv` and `.cpp` files in those roots, deduplicates
canonical filesystem paths, and sorts inputs by path spelling. Module imports
resolve within the collected Carven batch. The direct-run driver compiles collected
native sources together with generated implementations and executes the result.
`load_and_analyze_sources` prepares the batch, calls `analyze_compilation`, and
renders diagnostics using the source manager's line index for byte locations and
line ranges.
Compile and run commands send the program to the backend; interpret sends it to
the interpreter. Check completes after successful analysis without invoking a
backend or interpreter. Dump commands consume lexical or syntax results directly.

The driver owns optional timing measurements and their report. `support.timing`
uses a monotonic clock; parsing and analysis accumulate durations through a
borrowed recorder. Disabled scopes do not read the clock. Reporting follows
command-resource cleanup.

```text
SourceBatch → SyntaxProgram → ProgramDraft → SemIRProgram
```

`frontend.ast.tree` owns the syntax tree and its root. `frontend.ast.storage`
provides node storage and read-only views. `frontend.ast.topology` supplies
structural traversal for syntax construction and validation.

`parse_program` parses the closed source batch and resolves module imports.
`analyze` constructs declarations and typed structured bodies, solves types and
failure sets, validates contracts, checks ownership and callable loans, and
publishes an immutable semantic program. Errors prevent delivery; warnings accompany
a successful result.

## Design considerations

Carven establishes source types, coverage, evaluation order, ownership, and
failure contracts. Required constants, constant blocks, static tests, and
interpretation execute shared semantic operations with separate admission and
completion rules.
`const fn` uses ordinary function-body construction; admission checks the
completed body before execution.

The backend specializes operations using established semantic facts while
preserving effects, storage observations, and lifetimes. C++ supplies native type
properties, template instantiation, optimization, and machine code. Runtime
support uses standard-library numerical conversion.

`SemIRProgram` owns immutable `TypeContents` computed after storage topology
validation. Published-program consumers read these facts through an identity-checked
query. Constant execution during construction queries the draft's type facts,
including types whose dependencies are still being completed.

Semantic facts retain the identity and scope of the operation or storage they
describe. Ownership, nullability, normal-completion constants, and slice extents
have distinct propagation rules, described in their owning sections below.
Optional analyses define their fact domains, invalidation rules, and stopping
conditions; unknown facts retain the ordinary operation.

Backend preparation derives implementation plans from published facts without
modifying semantic stores. Realization preserves execution, storage, and cleanup
obligations while delivering the selected result; emission serializes target
syntax. Semantic analysis owns source facts, preparation owns implementation
selection, and runtime performs the remaining work.

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
| `backend/preparation/` | Borrowed body facts, operand demands, operation plans, and encoding/size proofs |
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
execution and backend preparation. It accepts numeric values, bools, chars, borrowed
text, and an unavailable-input alternative. Callers adapt their input storage and
supply a byte budget. Execution translates failures into diagnostics and accounts
for work; optional preparation uses runtime formatting on failure.
`execution` defines the required-evaluation entry and call context; `executor` owns execution
state, with `expr`, `control`, and `text` implementation slices. `limits`
names resource bounds.

Every canonical type store establishes the complete builtin type domain at
construction. Builtin identities remain stable through publication; lookup is a
read-only operation independent of source usage and prior constant evaluation.
Compound types remain demand-driven. Shared execution may consume either a draft
or a published program, but type queries have the same guarantees in both contexts.
Operations use their resolved result types where available.

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
require both operands to succeed. Binary expressions and integer ranges share
numeric operand context selection. It may require constructing the right operand
first when the left is a direct unsuffixed literal. Typed operations retain
source operand order for execution.

### Default initialization

`semir.initialization` defines default availability and native-construction
classification over type shapes. Construction queries and published-program
queries share the traversal, with unresolved array and slice type terms supported
during construction. Published queries read `SemIRProgram` directly. Empty arrays have no element-construction requirement. Enum and
callable types have no implicit selected value.

Nonempty structure construction maps source expressions to declared fields in
source order and requires every field exactly once. Published `SemStruct` values
retain that order and mapping. An empty `T {}` uses one `SemDefault` for the whole
value, keeping default aggregates compact independently of their array extents.

The executor realizes defaults using ordinary owned values and aggregate slots,
charging execution steps and aggregate work before materialization. Existing
admission and freezing rules apply. Publication rechecks type defaultability;
ownership treats defaults as fresh values without input loans, and nullability
invalidates exposed facts when defaults can invoke native construction. Native
constructor validity remains delegated to C++.

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

Completion requests, contextual expression construction, and required execution
suspend through `ContinuationTask`. A synchronous analysis entry drives a lazy,
depth-first continuation loop; dependency requests await their result without
replaying source operations. Active-dependency diagnostics and declaration order
remain part of the construction contract. Ownership and nullability analyses use
the same mechanism for dependent expression traversal. Syntax ownership checking
and the shared SemIR walker use explicit worklists; the latter preserves source
order and expression leave events.

Nominal equality capability is the conjunction of its reachable field and payload
capabilities. A temporary dependency graph propagates unsupported leaves to their
consumers. Construction queries use the currently completed reachable declarations;
head completion solves all nominal roots together.

`ProgramDraft` owns pending function heads. A complete callable contract includes
its result. Declaration-head completion closes the nominal tables; solving requires
complete callable contracts and bodies. The catalog and import-use state end before
solving. Final semantic validation checks declaration surfaces, including closure
captures and solved failure sets.

`analysis.program` owns `ProgramDraft` and declaration/body reservations.
Its consuming `finish()` checks reservation completeness, solves failures and
types, and finalizes callable signatures, declarations, and bodies in that order.
Test-stop effects are solved before body completion and publication.
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

`finish()` constructs a local `SemIRProgram`. Its constructor validates storage
facts and topology, then computes type contents. After releasing the consumed
draft, syntax, imports, and construction solutions, `finish()` checks body
contracts, global semantic contracts, ownership, and local pointer nullability,
in that order. All checks read `const SemIRProgram&`.
Source diagnostics use a separate channel. Body contracts establish the parameter
and binding relations used by global checks. Successful checks deliver the program.

Backend preparation derives operand uses and execution summaries from published
operations. Realization preserves their lifetime and control contracts and checks
that all emitted exits have receivers. Target verification checks the completed
C++ representation.

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
values, including during construction. Their shared owning-edge topology supports
iterative cleanup, including failed drafts and partially moved values. Each binding selection creates a new
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
execution. Range values retain two ordered integer bounds and an upper-bound
inclusion flag. Range loops retain a single iterable expression. Pattern tables
retain static interval facts; match and catch arms own dynamic bound expressions
keyed by pattern identity. Recursive pattern selection executes those expressions
only when that pattern is attempted. Integer coverage partitions the type domain
at interval boundaries and composes with enum payload and or-pattern coverage.

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
64 KiB materialization budget belong to `backend/preparation`.

Ownership keeps the receiver's Write access separate from formatting inputs and
enforces ordinary backing lifetimes. Owning formatting constructs independent
storage. Constant-function execution uses the same structured builtin formatter
and accounts for destination growth.

### Calls, bindings, and control

Builtin names use ordinary lookup. Direct calls publish `SemPrint` or
`SemReport` expressions; a builtin used as a value materializes a stateless
callable with its expected signature. Test-stop analysis collects possible calls
and direct stops, then propagates effects through reverse call dependencies.
Construction and publication use the same child-selection rules for known
conditions and coverage. Callable views conservatively admit test stop.
Backend consumers read the completed expression and callable effects.

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
constant evaluation and the builtin constant-formatting implementation. Floating
execution uses host native scalar operations. Optional runtime folding retains a
separate admission boundary for floating arithmetic and ordering, so adding an
executor operation does not authorize host precomputation of runtime expressions.
The builtin formatter validates its supported specifications and bounds width and
precision before calling the host C++ standard formatter. Steps,
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
structs and payload enums freeze recursively into `ArrayConstant`, `StructConstant`
and `PayloadEnumConstant` children
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

### Failure execution

`ExecutionFailure` distinguishes a source failure from an evaluator stop whose
reason has already been reported. `ExecutionSourceFailure` retains its nominal
type, immutable owned payload, throw origin and call trace. Calls, operands and
regions pass it through the common execution result. Root entry points diagnose
an escaping failure using their execution context. Resource exhaustion and evaluator errors remain outside
source recovery.

`try` matches the actual type and payload, then evaluates one guard and handler.
Rejected guards proceed to the next arm. Handler and guard failures propagate
outward. Each frame retains a stack of original caught failures independently of
pattern bindings, so Take and nested recovery cannot invalidate `rethrow`.
Binding copies use the normal text and aggregate work budgets. The same machinery
serves required constants, static tests and interpretation.

Required roots are constructed before global failure solving. Calls retain their
actual callee failure terms. A root `?` consumes its operand's pending terms and
registers the ordinary nonempty requirement; remaining unmarked terms register
ordinary empty-consumption requirements before evaluation. Successful execution
never replaces final failure solving or publication validation. Root propagation
can succeed for the selected inputs; an actual escaping failure is a diagnostic.
Static test roots retain their static no-escaping-failures requirement.

Declaration preparation includes failure payload types alongside parameter and
result types. Enum case construction prepares the complete nominal dependency
graph before execution shape queries. Shape and freezing validate case identity,
arity and payload types. Freezing preserves nominal field types.

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
Optional conversion of known numeric, bool, or char values into print text
belongs to backend preparation. Text, String, unknown scalars, and dynamic calls through builtin callable values keep their ordinary conversion paths.

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

An ownership flow has an optional normal completion containing state, result
relationships, and the storage selected during evaluation. Every exit carries a
state. Return exits carry result relationships; failure exits carry a failure type
and payload relationships; loop transfers distinguish break from continue.

An ownership batch prepares local object descriptions, relative temporary
positions, lifetime membership, and pattern acceptance once for each final body.
Type contents are read from the completed semantic program, without borrowing
construction state. Coverage borrows pattern tables and stage
type facts; exhaustive queries and full diagnostics share its algorithm. Catch
acceptance describes the current arm, independently of coverage accumulated by
earlier arms.

Queries distinguish body-local objects from caller inputs. Availability belongs
to an owner; holder relationships belong to storage positions within that owner.
Query inputs describe aliases, accesses, availability, relationships, and
execution state. Writes replace relationships for a definite singleton target.
Unknown elements and multiple possible targets merge possible relationships. Type
contents identify Carven storage owners (arrays and Strings), closure owners,
and callable views. These facts propagate to a fixed point over type
dependencies. Slices and native template arguments propagate only callable-view
restrictions; pointers do not propagate target contents. Relationship
construction skips types without closure owners or callable views. Native Read
passing is selected from C++ copy and destruction traits.

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

Value captures carry copied relationships. A call reads capture fields through
the current closure holder. Write captures resolve their current target set each
time the binding is evaluated. Selected storage remains attached to the evaluation
result across later callbacks, including closure replacement. Writes through a
set of possible targets retain each target's possible relationships. Lexical
regions and full expressions release their objects on each normal and control exit.
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

Recursive storage uses direct backing edges. Call normalization preserves exact
identities for unambiguous interface roots and their inline callable/capture
storage. Other reachable objects are grouped by allocation site
`(BodyID, slot, input-or-local)`. A summary that may represent multiple objects
retains that property through subsequent calls. Exact inline traversal stops at
slice backing and ambiguous or unknown-index targets. The semantic restrictions
on callable-view storage in nominal types and captures bound inline callable
chains. Allocation identity is separate from diagnostic provenance.

Availability and outlives facts join by conjunction; possible relationships join
by union. A per-object modification bit distinguishes an untouched caller object
from a write whose abstract edges happen to be unchanged. Query entry clears
modification history. An unchanged completion leaves the caller object alone;
a modified singleton is restored exactly, while a modified summary weakly updates
all possible source objects. Summarized relationships restore all possible
backing alternatives. Ambiguous same-site summaries retain possible loans;
exact replacement requires a definite singleton target.

The ownership solver tracks dependencies between call queries. Recursive calls
read the current answer; changed answers trigger dependent analysis. Normal,
typed-failure and test-stop answers join monotonically. The finite source sites,
inline paths, distinguished roles, and graph relations bound query identity.
An escaping callee-local relationship produces an invalid completion, stops
solving, and replays the offending input with diagnostics enabled to retain
the original access and lifetime witness. Diagnosis reads sealed answers without
creating queries or mutating results. This state remains private to analysis.

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

## Structural display and condition explanations

`SemPrint` reads logical values described by canonical types and completed
declarations. `ExecutionValueAccess::display_names` provides owned type, field,
and case spellings to the shared evaluator;
execution renders compound views without recovering source syntax. Depth, sequence,
and output limits match the runtime display contract.

Direct assertion and test calls retain the condition source and the operand
spellings of outer comparisons and short-circuit operations in `SemReport`.
The operation tree remains authoritative for execution. Publication verifies
this metadata's shape and spelling ownership. The executor observes values from
the original condition execution; constant tests report failures with their
structural explanations.

Direct `assert`, `check`, and `require` messages execute only on failure.
Ownership and nullability analysis split the success and failure paths: `check`
joins their normal continuations, `require` transfers its failure path to test
stop, and `assert` has no normal failure continuation. Shared execution reports
assertions without a test context. An assertion diagnostic ends an interpreted
run; the returned results retain earlier diagnostics. The interpreter delivers
diagnostics synchronously to its optional recipient. Report diagnostics retain
their operation kind; the driver uses it to select the compact presentation and
execution-stop note. Each report includes the current test identity when present.

## Compile-time blocks, output and tests

Constant execution delivers output bytes and a stream selection through its
context. The analysis adapter forwards them to the synchronous `ExecutionOutput`
recipient supplied to `analyze` or `compile`; an omitted recipient discards output.
The executor owns evaluation order and accounts for output and failed-check
diagnostic text within its cumulative text-work budget. The driver owns
stream presentation. Ordinary output is separate from error diagnostics and is
not stored in the published semantic program.

`ASTConstantBlock` is shared by module items and statements. Body construction
produces a `BodyKind::ConstantBlock` with independent local storage, a void result
and an inferred outward failure term. Registration snapshots the visible lexical
names. Constants retain their values; execution-frame bindings retain only their
lookup identity and cannot be read across the boundary.

Module, local and nested constant blocks are independent roots. The body batch
registers their syntax and lexical snapshots without building their bodies, so
they introduce no dependency from the enclosing callable. After callable bodies
are complete, it builds the blocks and executes them alongside static tests,
using a fresh executor per root. Body kind selects test reporting.

Closures and constant blocks preserve the source identity of inherited constants.
The batch records uses and reports unused locals after all bodies are built;
copying a lexical snapshot does not count as a use.

Completed block bodies remain in the semantic body store for failure solving,
ownership analysis and publication validation. They have no callable, test
declaration or target module item. An escaping failure diagnoses the root;
successful execution need not have an empty static failure set.

`const test` bodies use ordinary test construction and shared constant-body
admission. Required initializers execute when declaration or body construction
requests them; these outputs can precede queued roots. Later validation can still
reject the program. Failed checks report diagnostics while allowing execution to continue;
requirements stop the root through the executor's failure transport. Test errors
prevent publication. Published test declarations retain their explicit execution
stage so target planning selects only runtime tests.

## Interpreted execution

Each source batch admits at most one entry: an explicit `main` or the implicit
function containing one file's top-level statements. Multiple entries are rejected
by shared semantic analysis. Declaration-only files do not acquire empty entries.
Compilation and checking do not require an entry; native and interpreted program
execution do. Native and interpreted test execution select runtime tests instead of
the entry and require at least one runtime test. Required constant execution and
static tests remain part of ordinary analysis in every mode.

`interpreter/` consumes a published `SemIRProgram`. It validates the interpreter
operation subset from the entry or all selected runtime tests through direct
callees, then invokes the shared structured executor. Runtime tests are ordered by
canonical module path and source order. Each receives a fresh executor and budget;
its diagnostics are retained while later tests continue. Test operations are admitted
in helpers only for test execution. Published tests use the same body execution and
assertion control as static tests, with runtime arithmetic supplied by the context.
Ordinary language analysis remains the authority for names, types, access, lifetimes,
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
