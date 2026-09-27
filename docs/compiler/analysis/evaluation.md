# Semantic execution

This reference describes the shared semantic executor, required evaluation roots,
execution storage, freezing, and interpreted execution. [Construction](construction.md)
owns source admission and completion requests; [semantic representation](semir.md)
defines the operations and canonical facts consumed here.

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

`semantic.semir.constant_access` defines `ConstantValueReader` queries and their
execution-specific extension, `ExecutionValueAccess`. `ProgramDraft` supplies
construction facts; `PublishedConstantValues` reads a sealed program. Execution
borrows these interfaces through const references. `analysis.constant.freeze`
retains completed execution results through a single `ProgramDraft`, which supplies
both type queries and canonical interning. Retained IDs belong to that program.

`SemanticExecutionContext` supplies callable lookup and completed typed bodies.
The analysis adapter builds a provenance-source requester index on the first call
within a root evaluation and caches completed body/parameter descriptors. It does not cache
call results or provisional completion failures.
Execution delivers structured diagnostics with semantic origins and its call
trace through that context. The adapter submits them immediately, preserving
multiple errors and their order. A dependency failure already diagnosed by its
construction owner is propagated without an additional diagnostic.

## Constant execution and freezing

### Required roots and admission

Initializer and extent construction use the same typed semantic operations as
function execution. Their transient lifetime table has an independent
`BodyIdentityDomain::EvaluationRoot` identity in the current program; it does
not reserve or publish a function body. The table lives through evaluation.
The expression adapter owns source admission, contextual conversions, shape
checks, and their diagnostics. Shared interpretation suppresses optional
computed folding for these roots; the executor evaluates their operations.

Each required root and its nested calls share one execution budget. Ordinary
semantic checks cover all source branches. The executor checks an operation's
capability after its required operands complete; bounds and other dynamic checks
run on selected paths. Queries, supported aggregate equality, and display borrow
compound views; operand evaluation retains its ordinary copy and access rules.
Projection can retain child identities, copy from local storage, or move from
temporary owners according to the operand's storage. Local initialization,
assignment, and parameter storage materialize retained aggregate children into
independently mutable values.

Required execution consumes completed structured function bodies after ordinary
contextual typing, name binding, operation selection and result completion.
Ordinary functions remain runtime callable but cannot be called from required
roots. Each `const fn` definition receives a local static capability check over
its signature and semantically reachable body paths. Direct callees on those
paths must also be declared `const fn`; each definition is checked separately.
The body producer records statement, tail, and expression-operation reachability
in `SemanticStatement`, `SemanticRegion`, and `SemanticExpression`. The validator
consumes those facts without reconstructing control flow. All functions remain
subject to normal type, effect, ownership, and publication validation.

`evaluation.execution` executes admitted operations using body-local slots,
structured control flow and direct callable identities. It reuses scalar
constant evaluation, including wrapping integer operations, and the builtin
constant-formatting implementation. Floating
execution uses host native scalar operations. Optional runtime folding retains a
separate admission boundary for floating arithmetic and ordering, so adding an
executor operation does not authorize host precomputation of runtime expressions.

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

Evaluator state and call stacks belong to one required root and end with that
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
object retains its identity. Scope exit releases local objects, and compile-time
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
inputs or execution-local contents without intermediate freezing. During required
execution, slices borrow live backing objects; copies and chained slices keep
their selected element identity. Only a completed constant root detaches the
selected values into `SliceConstant` before releasing the backing. Runtime views
retain ordinary ownership checks.

After constant execution, the bodies remain available
for normal failure solving, complete-program type and ownership validation, and
runtime lowering. A failure at any publication gate prevents artifact delivery.
Ordinary runtime calls remain `SemCall` operations; a function's use during
required execution does not add a known result fact to later runtime calls.

## Failure execution

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

`BlockSource` keeps an optional diagnostic label and source origin for both
`TestDeclaration` and `ConstantBodyRoot`. A test's explicit name is its label;
without one, `block_display_name` derives its file, line, and column from the origin.
Native and interpreted test reporting use that same fallback. The constant
analysis diagnostic adapter adds the root kind, optional label, and source
location to execution errors. The shared executor does not own presentation.

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

`const test` bodies use ordinary test construction and check executor capability
on executed paths. Test names are optional; only explicit names enter the module's
duplicate-name set. Anonymous reports use file, line, and column. Constant
block labels are diagnostic strings rather than symbols and may repeat.
Required initializers execute when declaration or body construction
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

`interpreter/` consumes a published `SemIRProgram`. It starts the entry or all
selected runtime tests through the shared structured executor, which checks
operation support on executed paths. Runtime tests are ordered by
canonical module path and source order. Each receives a fresh executor and budget;
its diagnostics are retained while later tests continue. Test operations are admitted
in helpers only for test execution. Published tests use the same body execution and
assertion control as static tests, including wrapping integer arithmetic.
Ordinary language analysis remains the authority for names, types, access, lifetimes,
failures, and entry uniqueness; interpretation does not introduce an AST checker.

The executor borrows construction or published bodies through `ExecutionBody`.
Execution needs read access to canonical values, compound type facts, and builtin
types through a const `ExecutionValueAccess` borrow. Analysis freezes completed
results into its program draft; interpreter execution reads the published program.
Published execution does not mutate semantic tables or clone operation trees.

The call context supplies bodies, output, diagnostics, and optional source trace
events. Required constant execution invokes explicit `const fn` bodies and uses
the same wrapping integer arithmetic as interpretation and generated code.
Output and errors follow the selected stage; the
normal compiler has no interpreter-mode branch. The driver owns command options
and diagnostic presentation.
