# Semantic construction

This reference describes declaration and expression construction, dependency
completion, solving, and delegated C++ operations. The [compiler overview](../README.md)
defines the phase ownership and [publication order](../README.md#publication-gates).

## Expression construction

Within `analysis/constant`, `literal` normalizes source literals, `admission`
checks const-function capability contracts, and `evaluation` connects required
evaluation to construction requests and source diagnostics.
`analysis.constant.root` constructs typed initializer and extent roots and owns
their construction state, lifetimes, admission policy, and budgets. `analysis/expr`
shares contextual typing and typed operation construction between required roots
and ordinary bodies. Expression sites supply source scope, admission, value
consumption, lifetime, and failure effects. Required root expressions retain
their construction admission rule, while called function bodies use their
ordinary semantic construction. `analysis.expr.interpret` dispatches syntax
to `scalar`, `member`, and `call` handlers, which recurse through the expression site.

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

### Contextual record construction

Contextual record construction reuses the ordinary construction operation.
The AST preserves the absence of a written type; expression analysis obtains
that type from its existing expected-type input, prepares the nominal definition,
and applies the same representation-access and initializer checks as explicit
construction. No unresolved contextual construction reaches semantic publication
or the backend. Parsing distinguishes named construction from branch blocks by
tokens, independently of expected types.

## Callable result completion

Ordinary functions and lambdas share return construction for block and expression
bodies. Declared or context-supplied results provide return type context. Inferred
results check independently typed returns for invariant compatibility. Missing
returns and result-inference cycles are checked during body and signature
completion, before required execution. Constant execution consumes the completed
contract; call arguments do not specialize the function's result type.

## Ordinary class declarations

`ASTRecordDecl` carries the source record kind, fields, and nested operation
identities. Catalog class operations have explicit owner, visibility, and receiver
metadata and do not enter module lookup. Both record forms use the nominal product
storage currently identified by `StructID`; `RecordKind` preserves the source
distinction through semantic publication. Published class operations are ordinary
function declarations; the catalog retains their source ownership during analysis.

Body elaboration supplies lexical class authority and inherits it only into
lexically nested lambdas. It checks representation access before constructing
field or aggregate operations. A dot-call receiver is elaborated once and passed
to the ordinary argument binder, preserving its place until access is applied.
The completed operation uses `SemCall` with an ordinary receiver argument.
Declaration analysis checks the receiver contract; recursive default
initialization rejects classes in both construction and published-program queries.
Private representation remains available to type contents, ownership, and target
realization. Required constant evaluation rejects class values and operations.

## Completion requests

The analysis driver collects declaration identities, then `analysis.construction`
owns `ProgramConstruction`, which coordinates declaration resolution and body
elaboration. It owns a concrete `ConstructionRequests` port alongside the
declaration resolver and body elaborator. The port borrows its noncopyable,
nonmovable coordinator and provides completion requests for declarations,
function signatures, function bodies and type dependencies. Its implementation
routes to the domain owners. Construction establishes borrows without executing
requests. Requests and their continuations finish before the coordinator dies or
the draft is consumed.
Declaration and body completion retain their separate state and cycle policies.

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

Enum declarations retain their resolved equality support. Numeric enums support
comparison; payload enums require equality support from every payload type.
A temporary dependency graph propagates unsupported leaves through arrays and
enum payloads without repeatedly expanding shared dependencies. Construction
queries use the currently completed reachable declarations; head completion
solves all enum roots together. Structure and class types are unsupported leaves.

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

Structural type terms reference only previously appended terms. Canonicalization
consumes them in storage order into one final type mapping after failure solving.
Closed declared subtypes can be interned earlier through the same construction
operation; that path accepts no inferred failure terms.
Callable recursion and recursive failure constraints retain their own identities
and solving rules.

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
