# Semantic execution

This reference describes the shared semantic executor, static roots,
execution storage, freezing, and interpreted execution. Construction owns source
admission and completion requests; execution consumes typed semantic operations
and canonical facts.

## Execution interfaces

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
`execution` defines execution entries and their call context; `executor` owns
execution state, with `expr`, `control`, and `text` implementation slices.
`memory` owns addressable objects and resolves their projections. `limits`
names resource bounds.

Value and structured control contexts share operation execution, step accounting,
and capability checks. Value contexts receive normal-completion values; structured
control contexts also carry return, break, and continue to their enclosing
boundaries. Value-form controls obey the language's transfer boundary and cannot
transfer control out of an operand.
Call arguments and unsupported-operation inputs retain their storage or value
demands during acquisition. Native Read preserves exact storage borrows; Carven
Read follows the type's snapshot rules. Value initialization and captures retain
their copying and transfer rules.

`semantic.semir.constant_access` defines `ConstantValueReader` queries and their
execution-specific extension, `ExecutionValueAccess`. `ProgramDraft` supplies
construction facts; `PublishedConstantValues` reads a sealed program. Execution
borrows these interfaces through const references. `analysis.constant.freeze`
retains completed execution results through a single `ProgramDraft`, which supplies
both type queries and canonical interning. Retained IDs belong to that program.

`SemanticExecutionContext` supplies callable lookup and completed typed bodies.
`prepare_call` returns an `ExecutionBody` whose inputs identify its parameter
bindings. The analysis adapter completes and realizes requested bodies. Execution
delivers `ExecutionEvent` reports with semantic origins and call traces
synchronously. The adapter presents them in order; a dependency failure already
diagnosed by its construction owner propagates without another report.

## Static execution and freezing

### Static roots and admission

Initializer and extent construction use the same typed semantic operations as
function execution. Standalone expression roots have an independent
`BodyIdentityDomain::EvaluationRoot` identity in the current program and do not
reserve or publish a function body. Local static roots belong to their
source body's lifetime domain.
The expression adapter owns source admission, contextual conversions, shape
checks, and their diagnostics. Local static roots may retain pure constant
facts; computing a fact does not execute a static root. Other admitted
operations are evaluated by the executor when the root executes or a type
reads its value.

Each static root and its nested calls share one execution budget. Ordinary
semantic checks cover all source branches. The executor checks an operation's
capability after its required operands complete; bounds and other dynamic checks
run on selected paths. Queries, supported aggregate equality, and display borrow
compound views; operand evaluation retains its ordinary copy and access rules.
Projection can retain child identities, copy from local storage, or move from
temporary owners according to the operand's storage. Local initialization,
assignment, and parameter storage materialize retained aggregate children into
independently mutable values.

Static execution consumes completed structured function bodies after ordinary
contextual typing, name binding, operation selection and result completion.
Ordinary functions remain runtime callable but cannot be called from static
roots. Each `const fn` definition receives a local capability check over
its signature and semantically reachable body paths. Direct callees on those
paths must also be declared `const fn`; each definition is checked separately.
The body producer records statement, tail, and expression-operation reachability
in `SemanticStatement`, `SemanticRegion`, and `SemanticExpression`. The validator
consumes those facts without reconstructing control flow. All functions remain
subject to normal type, effect, ownership, and publication validation.

`evaluation.execution` executes admitted operations using body-local slots,
structured control flow and direct callable identities. It reuses scalar
static execution, including wrapping integer operations, and the builtin
constant-formatting implementation. Floating
execution uses host native scalar operations. Optional runtime folding retains a
separate admission boundary for floating arithmetic and ordering, so adding an
executor operation does not authorize host precomputation of runtime expressions.

### Explicit static inputs

Bound-expression execution supplies only explicitly admitted static bindings to
a completed body. Missing runtime slots stay unavailable. Specialization evaluates
and freezes static call arguments in source order with their declared types.
Execution then evaluates the residual call's runtime arguments in source order.
Inside a `const` block or `const test`, all arguments execute in source order.
The executor then calls the context's `bind_call` adapter. Only the analysis
context identifies static parameters, freezes their values, requests an
instance, and retains runtime arguments with their original access. The
executor provides bounded argument detachment without exposing its storage;
ordinary contexts preserve the callable and arguments. All contexts then use
`prepare_call` with the selected callable. An ordinary call to a `const fn`
remains an ordinary runtime call.

Static specialization evaluates local initializer roots while constructing
residual regions. A body root executes once for its instance; each expanded
occurrence executes in its iteration's static environment. A value that type
formation or an independent body's construction read earlier was computed from
a copy with `ExecutionOutputMode::Discard`; the root still executes here. A body
or instance specialized during such a computation executes its own static stage
and writes its output. Distinct call-argument roots
execute independently even when their values select the same instance. Unselected
`const if` arms contribute no local
initializer, block, or call-argument evaluation during specialization.

The semantic executor consumes a body and its selected region in one execution
mode. Ordinary functions, closures, tests, and `const` blocks use their realized
regions. Staged calls select the instance's residual region and bind runtime
arguments. Analysis can construct a required instance on demand; the interpreter
uses published instances without mutating semantic stores or replaying static
argument effects.

Static selection is explicit. Body specialization resolves `const if` and
`const for` before runtime execution. Inside a static block or test these
constructs execute as ordinary controls in the shared executor. Ordinary
control in a runtime body retains its runtime semantics and static roots.

`analysis.stage.session` owns active roots, specialization budgets, in-progress
bodies, and failed body realization results. `ProgramDraft` owns instance
reservations and their pending, completed, or failed state. Static roots always
enter the executor; optional normal-completion facts do not bypass execution.
Ordinary expression construction uses one operation-folding dispatcher. C++ type queries use that
dispatcher after type-time static reads to retain scalar witnesses without
discarding operand effects. Substituting static values
into runtime code does not eagerly evaluate the resulting arithmetic: a trap
behind runtime control remains a runtime operation.

### Execution values and storage

Execution frames hold owned values or aliases to an addressable object and a
projection path. `SemanticExecutor` and `ExecutionMemory` are noncopyable and
nonmovable; one execution domain has exactly one object store. Places contain a
fresh `ExecutionIdentity`, an object index and a projection path. Domain identities
and object indices are never reused; exhaustion terminates. Local pointers retain
these coordinates without retaining the target's lifetime. Reads and writes
resolve coordinates through the same storage operations.
Array and slice iteration use the same sequence view as indexing and slice
queries. Iteration evaluates its source once, retains temporary owners until loop
exit, and binds elements using the source Read or Write policy. Scalar Read
bindings snapshot each element; borrowed aggregate bindings retain its location.
Nested loops compose projection paths. Loop exits release their bindings and
retained temporary storage, including early returns and execution failures.

Evaluator state and call stacks belong to one static root and end with that
request. Existing `ConstantID` values borrow retained inputs. Computed `ConstantAtom`
values remain execution-local and cannot carry compound storage; they hold scalar
data or a retained text identity. `ExecutionValue` is move-only: host movement
transports execution results, while `copy_value` performs language copying and
accounts for aggregate and text work. Atoms, coordinates and immutable text handles
remain copyable.
`ExecutionTextStorage` privately holds owned bytes or a borrowed retained spelling.
The move-only `ExecutionOwnedText` encapsulates String ownership;
`ExecutionText` either shares immutable content or weakly borrows String storage.
`as_str` creates the weak borrow without copying bytes or charging text work.
String copying creates fresh storage;
source Take and mutation replace its storage identity, whereas host moves preserve
it. Releasing that storage expires its weak borrowers. Formatting,
queries, and equality read text without freezing it. At the initializer boundary,
borrowed results detach before execution storage is released, and owning String
becomes canonical `str`.

`ExecutionMemory` owns a weak identity index and registers one `TextBytes` backing
per text identity on demand. Each execution domain keeps its own byte coordinates.
Byte slices and their copies retain its coordinate and range. `TextBytes` weakly
borrows the text storage; reads observe `u8` values on demand through
`ExecutionByteView`, without constructing a complete byte array. Byte projections
use the same backing identity; resolving a byte pointer locks the weak borrow
and rejects expired storage.

Execution places identify an object and an already evaluated path through
fixed-array elements, struct fields, or text bytes. Assignment to a live owner
preserves its object identity; Take and scope exit end it. Text backing has its
own lifetime, separate from the String owner slot. Read operands use the
expression's storage-selection fact. The shared type-contents query selects delayed
Read observation for array and String storage, including aggregate fields.
Other admitted values capture their value at the source position. Constant and runtime constructors share array shape rules
and struct initializer selection in `analysis.operations`.

### Resource limits

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

### Freezing and completion

Execution slots distinguish uninitialized, available, and taken states. Whole-binding
Take moves owned storage and ends its object identity; assignment may initialize
the source as a new object, without reviving prior pointers. Assignment to a live
object retains its identity. Scope exit releases local objects, and static
dereference rejects a coordinate whose target is no longer alive. Aggregate
values own typed elements; enum values separately own
their selected case and payload. Fixed arrays and
structs and payload enums freeze recursively into `ArrayConstant`, `StructConstant`
and `PayloadEnumConstant` children
without changing element types, nominal identities, or field types. Struct fields
use declaration order. Publication verifies the type, arity, and exact child
types; compound constants reference already interned children.

At a constant initializer, an explicit slice destination or `as_slice()` can
produce an execution-local slice and retain it through `analysis.constant.freeze`.
The selected freeze preserves each element's type. `SliceConstant` records ordered canonical element IDs under
a `SliceTypeValue`; it contains no host address, allocator state, or capacity.
Store construction checks child identity and availability; publication checks the
slice type and exact child types. Initializer slice queries operate on retained
inputs or execution-local contents without intermediate freezing. During static
execution, slices borrow live backing objects; copies and chained slices keep
their selected element identity. Only a completed static root detaches the
selected values into `SliceConstant` before releasing the backing. Runtime views
retain ordinary ownership checks.

After static execution, the bodies remain available
for normal failure solving, complete-program type and ownership validation, and
runtime lowering. A failure at any publication gate prevents artifact delivery.
Ordinary runtime calls remain `SemCall` operations; a function's use during
static execution does not add a known result fact to later runtime calls.

## Failure execution

`ExecutionResult` distinguishes normal completion from `ExecutionFailure`.
The failure alternatives are `ExecutionHalt`, `ExecutionSourceFailure`, and an
already diagnosed `ExecutionDependencyFailure`. A halt owns the event delivered
synchronously at its source, including its cause and `StopRoot` or `Abort` scope.
Call and root boundaries move it without reporting again. Static and interpreted
adapters select diagnostic codes from its cause. A failed `check` produces a report
and normal completion; execution results determine control.

`ExecutionSourceFailure` retains its nominal type, immutable owned payload, throw
origin and call trace. Calls, operands and regions pass it through the common
execution result. Root entry points report an escaping source failure and turn it
into a halt. Resource exhaustion and evaluator errors remain outside source recovery.
An adapter's call failure supplies either a new event for the executor to deliver
or an existing execution failure to propagate unchanged.

`try` matches the actual type and payload, then evaluates one guard and handler.
Rejected guards proceed to the next arm. Handler and guard failures propagate
outward. Each frame retains a stack of original caught failures independently of
pattern bindings, so Take and nested recovery cannot invalidate `rethrow`.
Binding copies use the normal text and aggregate work budgets. The same machinery
serves static roots, static tests and interpretation.

Static roots are constructed before global failure solving. Calls retain their
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

## Structural display and condition explanations

`SemPrint` reads logical values described by canonical types and completed
declarations. `ExecutionValueAccess::display_names` provides owned type, field,
and case spellings to the shared evaluator;
execution renders compound views without recovering source syntax. Depth, sequence,
and output limits match the runtime display contract.
Display and formatting read the retained bytes of `CStringConstant`; its execution
value keeps the external pointer type.

Direct assertion and test calls retain the condition source and the operand
spellings of outer comparisons and short-circuit operations in `SemReport`.
The operation tree remains authoritative for execution. Publication verifies
this metadata's shape and spelling ownership. The executor observes values from
the original condition execution; constant tests report failures with their
structural explanations.

Direct `assert`, `check`, and `require` messages execute only on failure.
Ownership and nullability analysis split the success and failure paths: `check`
joins their normal continuations, `require` transfers its failure path to test
stop, and `assert` has no normal failure continuation. Execution events carry
their termination contract: continue, stop the current root, or abort the run.
Failed `check` continues; failed `require` and `fail` stop their test; assertions
and runtime traps abort interpreted execution.
Resource exhaustion and unsupported operations stop the current test.
The interpreter delivers reports synchronously and retains them in its results.
Each event has one cause: a language `ReportKind` or an `ExecutionIssue` with an
explicit stop scope. Report kinds determine their titles and termination. Per-test
results retain reports and record execution termination; the interpreter associates
each report with the active test.

Events retain their cause and ordered text fields. Message assembly and byte
accounting use one field-layout helper, including empty and multiline fields.
Native report writers stream the same layout. Structural explanations reuse values
from the condition's original execution.

## `const` blocks, output and tests

Static execution delivers output bytes and a stream selection through its
context. The analysis adapter forwards them to the synchronous `ExecutionOutput`
recipient supplied to `analyze` or `compile`; an omitted recipient discards output.
The executor owns evaluation order and accounts for output and failed-check
diagnostic text within its cumulative text-work budget. The driver owns
stream presentation. Ordinary output is separate from error diagnostics and is
not stored in the published semantic program.

`ASTConstBlock` is shared by module items and statements. A module block becomes
a `BodyKind::ConstBlock` with independent local storage, a void result, and an
inferred outward failure term. A body-local block becomes a `SemConstBlock` region
with its own lexical scope. Its enclosing static bindings are available;
enclosing runtime storage is unavailable.

`BlockSource` keeps an optional diagnostic label and source origin for both
`TestDeclaration` and `StaticBodyRoot`. A test's explicit name is its label;
without one, `block_display_name` derives its file, line, and column from the origin.
Native and interpreted test reporting use that same fallback. The static
analysis diagnostic adapter adds the root kind, optional label, and source
location to execution errors. The shared executor does not own presentation.

A module-scope `const` block is an independent root with its own body. The
body batch builds it in source order and executes it alongside static tests,
using a fresh executor per root. A block in a body is a `SemConstBlock` of that
body: specialization rewrites its region in the current static environment and
executes it in the body's frame, once per occurrence. The enclosing body's kind
selects test reporting.

Closures preserve the source identity of inherited constants.
The batch records uses and reports unused locals after all bodies are built;
copying a lexical snapshot does not count as a use.

Completed block bodies remain in the semantic body store for failure solving,
ownership analysis and publication validation. They have no callable, test
declaration or target module item. An escaping failure diagnoses the root;
successful execution need not have an empty static failure set.

`const test` bodies use ordinary test construction and check executor capability
on executed paths. Test names are optional; only explicit names enter the module's
duplicate-name set. Anonymous reports use file, line, and column. Constant
block labels are diagnostic strings rather than symbols and may repeat.
Static initializers execute when declaration or body construction
requests them; these outputs can precede queued roots. Later validation can still
reject the program. Failed checks report diagnostics while allowing execution to continue;
requirements stop the root through the executor's failure transport. The static
adapter retains the root's diagnostic failure, so failed checks prevent publication
even when execution completes normally. Previously diagnosed dependencies retain
their owner's failure. Published test declarations retain their execution stage so
target planning selects runtime tests.

## Interpreted execution

Each source batch admits at most one entry: an explicit `main` or the implicit
function containing one file's top-level statements. Multiple entries are rejected
by shared semantic analysis. Declaration-only files do not acquire empty entries.
Compilation and checking do not require an entry; native and interpreted program
execution do. Native and interpreted test execution select runtime tests instead of
the entry and require at least one runtime test. Static execution and
static tests remain part of ordinary analysis in every mode.

`interpreter/` consumes a published `SemIRProgram`. It starts the entry or all
selected runtime tests through the shared structured executor, which checks
operation support on executed paths. Runtime tests are ordered by
canonical module path and source order. Each receives a fresh executor and budget.
A stopped test leaves later tests available; an abort ends the run. Test operations
are admitted in helpers only for test execution. Published tests use the same
body execution and assertion control as static tests, including wrapping integer
arithmetic.
Ordinary language analysis remains the authority for names, types, access, lifetimes,
failures, and entry uniqueness; interpretation does not introduce an AST checker.

Semantic display owns bounded text storage, escaping, and indentation in
`semantic/evaluation/display.cpp`. Native display owns these operations in the
runtime display component. Both implement the language's byte, depth, and sequence
limits using their own value access and scalar formatting.

The executor borrows construction or published bodies through `ExecutionBody`.
Execution needs read access to canonical values, compound type facts, and builtin
types through a const `ExecutionValueAccess` borrow. Analysis freezes completed
results into its program draft; interpreter execution reads the published program.
Published execution does not mutate semantic tables or clone operation trees.

The call context supplies bodies, output, reports, and optional source trace
events. Static execution invokes explicit `const fn` bodies and uses
the same wrapping integer arithmetic as interpretation and generated code.
Output and errors follow the selected stage; the
normal compiler has no interpreter-mode branch. The driver owns command options
and diagnostic presentation.

## SIMD execution

`U8x16`, `Mask16`, `F32x4`, `Mask4`, `U8x32`, `Mask32`, `F32x8`, and `Mask8`
are fixed logical builtin types.
One `SIMDConstant` stores lane representation bits; its semantic type determines
lane count and interpretation. Mask lanes contain only zero or 255. Publication
and execution share the constant type contract, including mask encoding and
floating bit identity. The executor treats these values as atoms, validates
primitive bounds, charges lane work, and uses live sequence storage for loads.
`SemIntrinsic` owns value operands in source order; its SIMD operation and
result or receiver type determine the shared `simd_contract`.
Extract offsets and byte shift counts are static operands with checked ranges.
A type layout supplies the element, vector, mask and memory shapes; construction,
publication and backend realization consume that same contract. The executor
shares bounded lane algorithms across layouts and never calls native intrinsics.
Floating arithmetic uses the scalar floating semantics; ordinary floating runtime
expressions retain the native execution environment.

The backend maps these logical values to the separately included
`carven/runtime/simd/simd.hpp`. Its shared public layer realizes operations through
one compile-time backend: AArch64 NEON, x86 AVX2 when enabled, or portable lanes.
Wide logical vectors use two NEON registers or one AVX2 register. The craft composes
block traversal, byte classification and explicit static controls using ordinary
Carven functions. Native target guards do not change static-execution semantics.
