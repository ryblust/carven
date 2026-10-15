# Semantic construction

This reference describes declaration and expression construction, dependency
completion, solving, and delegated C++ operations.

## Module and name selection

`source.standard` owns provided module and export identities, per-name defaults,
and standard declaration roles. Frontend normalization resolves canonical paths
through the module catalog. A standard entry may contain both source and provided
declarations; they share name lookup and conflict rules. Each source module owns
its syntax and generated implementation.

`analysis/catalog/builder` registers module declarations before binding imports
and supplying defaults. `decl.cpp` owns source and provided declaration
registration; `imports.cpp` owns Carven and C++ selections and conflicts.
`catalog.cpp` owns read-only queries and import-use tracking.

The catalog stores source declarations with their source ownership. Name bindings
select either a declaration identity or a provided export identity. Explicit
selections, aliases, wildcards, conflicts, and defaults use this same binding
model. `select_catalog_entity` records the selected entity and import usage;
lexical and type-parameter lookup belong to their surrounding scopes. Semantic
construction dispatches on the selected identity and produces canonical types and
operations consumed by the existing analysis and C++ generation stages.

Standard nominal and generic types are source declarations with ordinary record,
enum, and generic identities. Builtin duration literals, numeric parsing, and
timeout composition select the standard declarations they require through the
catalog and ordinary declaration completion. Their standard roles do not add a
second type identity.

`source.standard` records a bounded set of standard declaration roles, default
bindings, and associated entries. Dependency discovery and semantic selection
share the associated-entry mapping. Source function selection finds the catalog
symbol and requests ordinary declaration completion. The completed callable
supplies its signature and source body; argument checking, borrow propagation,
and failures use ordinary calls.

Host input, monotonic sampling, and timer waits have provided callable contracts
available within their owning standard source module. Source functions compose
these operations into input handles, checked text, time points, and public waits.
These implementation bindings do not add module import forms.

Frontend source dependency discovery includes source imports, configured standard
defaults and associated entries, and standard declarations required by builtin
operations. Provided selections without source dependencies need no source
module. Workspace navigation consumes this same dependency closure. Explicit-input
compilation checks the supplied batch and does not load missing source declarations.

## Expression construction

Within `analysis/constant`, `literal` normalizes source literals, `admission`
checks const-function capability contracts, and `evaluation` connects static
execution to construction requests and source diagnostics.
`analysis.constant.root` constructs typed initializer and extent roots and owns
their construction state, lifetimes, admission policy, and budgets. `analysis/expr`
shares contextual typing and typed operation construction between static roots
and ordinary bodies. Expression sites supply source scope, admission, value
consumption, lifetime, and failure effects. Static root expressions retain
their construction admission rule, while called function bodies use their
ordinary semantic construction. `analysis.expr.interpret` dispatches syntax
to `scalar`, `member`, and `call` handlers, which recurse through the expression site.

`analysis.expr.async` constructs async queries, cold leaves, composition, and
timeout for body and static-root expressions. Sites analyze and consume operands
under their access and execution-context rules. Shared construction determines
arity, result types, and operation failure contracts.

### Expression construction results

`analysis.expr.result` defines `ExpressionResult<T>`, which carries `T` on success.
Failure carries either an existing `AnalysisFailure` token or
`ExpressionNotAdmitted`, which has no diagnostic yet. Static initializer, enum,
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

Shape matching and invariant storage checks share one structural type traversal.
Storage identity requires matching nominal types, pointer access, array extents,
and callable parameters, results, and failures. `constrain_invariant_type`
registers failure equalities during inference. Callable array elements may adapt
to views; other elements require invariant storage. Permission narrowing changes
only the outer pointer access. Conversions retain their checked destination type,
including contextual array-to-slice construction.

### Default initialization

`semir.initialization` defines default availability and native-construction
classification over type shapes. Construction queries and published-program
queries share the traversal. Construction queries also inspect known generic
shapes and unresolved array and slice type terms. Published queries read
`SemIRProgram` directly. Empty arrays and owning sequences have no
element-construction requirement; a rigid type parameter alone supplies no
default. Enum and callable types have no implicit selected value.

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

## Nominal type parameters

The declaration store reserves a generic definition identity rather than a concrete
struct or enum identity. Each parameter has a type or failure-set kind; an
application keeps those argument domains distinct. Declaration construction binds
parameter references, nominal applications, and array, Tuple, slice, Vector,
pointer, and Operation constructors into checked type expressions. These descriptors
contain semantic identities rather than AST nodes or unresolved names. Definition
checking validates application arity, parameter-flow growth, and symbolic by-value
storage cycles, including unused definitions.

`GenericDeclarationContract` retains the generic definition's source identity.
The standard `Read<T>` enum uses ordinary type-argument resolution and concrete
nominal-instance construction.

Instance formation keys an ordinary concrete nominal declaration by its
definition and ordered canonical arguments. It reserves that declaration before
completing its fields or cases; recursive references reuse the reservation.
Field and payload resolution reads the checked type expressions under the
canonical argument environment and directly interns each resulting concrete
type. The owned key keeps that environment stable during nested instance
construction. Symbolic definition checks retain substitution over rigid type
expressions. Failed instances retain their diagnosed failure. Source-head
completion and the concrete instance closure are separate gates.
Each `GenericNominalInstance` retains its source use origin for diagnostics,
independently of the definition's declaration identity.

Concrete declarations pass ordinary type, storage, ownership, and publication
checks. Enum equality is derived from those same concrete shapes. Declaration
audience is checked against the generic definition and its actual arguments;
instance declarations themselves are implementation details. Publication retains
definition audience contracts and concrete instance metadata for content identity and
C++ planning. Parameterized function bodies are not part of this path.

## Parameterized function bodies

A function definition owns rigid type and failure descriptors and one checked
`StructuredBodyDraft`. Ordinary body construction checks universal operations,
field access, calls, completion, and static admission against that environment.
The definition uses its own body identity domain. It does not borrow an identity
from the table of published concrete bodies.

Concrete instance formation reserves a callable before replacing types and
already bound generic calls in the checked body. It reuses that reservation for
recursive calls. Array, Tuple, slice, and Operation construction retains unresolved
failure terms until the shared failure solver completes them. Instantiation does
not revisit source syntax or re-run definition diagnostics.

A typed generic environment carries `ConstructionTypeRef`,
or `ConstructionFailureRef` according to its parameter kind. It may contain pending
inference terms. `GenericTypeID` and `GenericFailureID` retain distinct rigid
identities; neither supplies a placeholder canonical type or set. Deferred calls
keep the checked target and environment until their dependencies complete. The
shared type and failure solvers then normalize the environment. Only canonical
types and failure sets form the instance key.

The same argument resolver handles nominal applications, ordinary calls, and
static calls. Tuple types have fixed element lists; substituting an element type
preserves the product shape.

Ordinary batch closure and static demand use the same normalization boundary.
Normalization reads canonical arguments, immutable symbolic facts, and any
supplied failure solution without initiating a solve. Ordinary construction keeps
unresolved environments in deferred calls. Once source bodies complete, batch
closure shares a failure solution across those calls and closes newly formed
instances before static realization. Static demand first completes the callable
bodies reachable through its selected type and failure dependencies, then requests
a failure preview if normalization still needs one. A preview cannot infer an
empty set from an unfinished body. Publication solves and validates the complete
program. Instance substitution shares one failure-instantiation memo
across signatures, type descriptors, expressions, regions, calls, and handlers.
Recursive equations retain their graph edges until solving; direct members of
an unfinished equation are not a complete failure contract.

Definition failure safety checks require universal emptiness, subset, or equality
proofs over the rigid effect environment. They are checked at the definition and
are not copied as fresh instance constraints. Propagation whose set depends on a
parameter retains its checked operation when a concrete set becomes empty.
Concrete ordinary operands keep their nonempty propagation requirement.

Type formation requirements retain their checked type and source origin in the
definition. A shared checker distinguishes supported, rejected, dependent, and
delegated native requirements. The checked call graph substitutes and propagates
dependent requirements before instance validation. Parameter-flow checking rejects
constructor growth before recursive requirement traversal; unfinished callee bodies
remain pending until definition closure.

`analysis.model` supplies the shared body and type reader for concrete bodies and
checked definitions. Async admission uses that reader to reject known invalid
input and completion shapes in definitions and leaves type-dependent admission
for concrete instance checking. The same traversal checks both forms,
including fields of rigid nominal applications. Ownership and nullability also
use this reader. Unknown type inputs retain formal relationship bundles; call transfer
substitutes the actual relationships through the same solver and query graph.
Type-content propagation uses the shared fixed-point algorithm for both rigid
and concrete shapes. A snapshot keeps definition bodies and normalized type and
failure descriptors alive for that analysis. An operation type descriptor owns
its success type and failure expression; admission reads that same descriptor.
The post-solve checks share one model until source-body publication begins.
Publication and the backend consume only completed concrete bodies.

Async calls in a checked definition reuse their elaborated argument types and
operation completion contract for admission. Known invalid substitutions are
rejected even when the containing definition has no concrete instance.
Test-stop effects propagate through concrete and generic synchronous calls in
one call graph; the same reachability scanner and worklist compute both facts.

## Callable result completion

Ordinary functions and lambdas share return construction for block and expression
bodies. Declared or context-supplied results provide return type context. Inferred
results check independently typed returns for invariant compatibility. Missing
returns and result-inference cycles are checked during body and signature
completion, before static execution. Static execution consumes the completed
contract; call arguments do not specialize the function's result type.

## Static specialization

A named function parameter records its execution stage independently of its
access and type. Body construction checks static-expression admission for static
arguments, local `const` initializers, `const if` conditions, and `const for` ranges.
Static bindings are `const` parameters, local `const` bindings, and `const for`
indices. Ordinary locals, parameters, and range indices do not acquire that
admission through folding. Every source arm undergoes ordinary checking.

`build_static_expression` in `analysis/constant/root` constructs local initializers,
static call inputs, SIMD immediates, and static control inputs as typed static
roots. Their expressions use the owning body's expression lifetime and an
independent failure context. Root failures are consumed by static execution and
do not enter the enclosing runtime failure contract. Local initializers are
stored in `SemStaticBinding`. Ordinary source control receives the same name,
type, failure, ownership, and return checks regardless of constant operand facts.
`const if` selects the residual arm during specialization.

Fixed type formation, C++ construction type queries, and separately constructed
lambda bodies can read a closed source root during construction. The read follows
binding identities, computes a copy of the initializer in a dependency-query
context, and records the frozen value by binding; later queries reuse it. That
context discards output and disallows host clock reads, input, and waits at the
shared execution boundary, including calls through source wrappers. It can use
already completed module constants. The source initializer stays in place for
ordinary specialization; construction cannot pre-execute its host work.
An unbound static input cannot determine a fixed type or implicitly enter a
separate body. Retained owning values preserve their source type; static call
inputs retain their declared types. Explicit captures require runtime local storage.
C++ construction queries resolve explicit static references and propagate pure
operand facts; ordinary runtime calls retain their execution obligations.

`analysis/stage` copies a completed checked region and rewrites it in a static
binding environment. Static values become constant expressions; selected
`const if` regions replace their control; `const for` becomes `SemExpandedLoop`
with an ordered region per index. Local roots and `const` blocks execute once
per selected instance or expanded source occurrence, in source order. Ordinary
control retains its static roots and static arguments. Explicit instance, iteration,
node, and depth budgets bound this work per specialization root.

Type formation precedes selection and expansion, so an admitted dependency-query
root can be demanded even in an unselected arm. Host-dependent local roots cannot
be read this way; their values must first be completed in a module constant when
needed for type formation. A `const` block in a body is constructed in
place as a `SemConstBlock` region with its own lexical frame and failure
context. Construction rejects a runtime local declared outside the block and a
control transfer that would leave it. Runtime bodies first specialize static
inputs, then execute the ordinary residual region. Static blocks and static tests
execute all local bindings and controls in source order; calls select instances
from evaluated static arguments through the analysis execution context.

`ProgramDraft` owns instance reservations, completion or failure states, and
bodies; a stage session owns active roots and specialization budgets. Instances
are keyed by the source function and normalized static arguments, and each has
its own callable and body. Residual calls publish that callable and runtime
arguments. An instance copies the source
binding, pattern, and lifetime tables; expanded iterations append independent
local identities. Closure callable identities stay fixed while capture values
specialize.
Iteration binding remaps the selected region and extends one copy of its local
tables. Residual installation transfers that region and its tables while retaining
the checked region and body inputs.

Publication verifies residual regions through ordinary semantic contracts and
checks instance identities and argument types. Executable bodies contain no static
control or static initializers. After validation, publication computes
callable/type surfaces and closure reference order, then discards checked
source regions and static-only bodies. Declarations and tests clear references
to removed bodies. Surviving body IDs and local table identities remain stable.
Specialization does not change parameter, result, or layout types.

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
realization. Class `const fn` operations use the same execution capability and
lexical representation rules as ordinary function bodies. Direct class-operation
calls in module constant initializers, local `const`
initializers outside a static block or test, and type-forming constant expressions
require a `const fn` wrapper. These entry sites do not grant class representation
access.

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
calls. A call in a static root requests a completed typed body; requesting an
actively elaborated body diagnoses an unfinished-body dependency. Execution
recursion uses completed bodies under the evaluator's call-depth limit. Each body
is elaborated once; later requests reuse its completed or failed result.

Completion requests, contextual expression construction, and static execution
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
published query surfaces. Concrete declarations and executable bodies close
before publication; checked generic definitions retain their normalized
parameterized descriptors in the separate analysis snapshot. Failure inference
computes the least fixed point of the program's failure equations. Numeric enum
underlying types are concrete during declaration resolution; structure fields
and enum payload types require construction-type resolution.

Callable construction queries read owning callables, unresolved views, and
canonical views through one shape. A `ConstructionFailureRef` identifies a
mutable inference term, an immutable canonical failure set, or an immutable
symbolic failure expression. Declared contracts retain their set or symbolic
expression directly; reading a callable contract does not create an inference
term. Cold operation types use the same references, and the shared callable
shape retains synchronous or async execution. Adoption checks failure subsets,
while Write access requires equal contracts.

`analysis.expr` interprets each expression once, with concrete constant and body
sites. The interpreter owns contextual typing, operation selection, enum and
text rules, and diagnostics. Sites supply scope lookup and handle execution-only
syntax. Constant-expression admission is checked separately from the computed
value; declarations retain lazy completion and cycle diagnostics.

Structural terms in the construction type store reference previously appended
child terms. Construction type normalization visits them in storage order after
a failure solution is available. A closed type becomes a canonical `TypeID`; a
constructor with a dependent child or effect becomes a parameterized descriptor.
`TypeResolution::normalize` reads this partial mapping, which can retain
`GenericTypeID` rather than require every analysis type to be concrete.
`TypeResolution::resolve` is the stricter query for closed declarations and
executable bodies. Generic snapshots normalize every child reference and retain
the shared failure solution that owns symbolic inference equations.

Closed declared subtypes can be interned earlier through the same construction
operations. Function-view signatures can carry concrete failure sets or inferred
terms pending closure. Source callable views with rigid type or effect contracts
are rejected at admission. They do not enter canonicalization as unresolved
views. Callable recursion and recursive failure constraints retain their own
identities and solving rules.

Type-formation closure tracks the object status of every type-argument slot for
the `NonVoidObject` requirement. Closure retains the full environment for
substitution, while failure equations do not create distinct formation states.

Symbolic failure application uses normalized environment keys and reuses stable
recursive applications. Once source dependencies complete, exact producer rows
reduce through the same failure algebra before application lookup. Ordinary
union cycles reduce to their reachable contributions; application, mask, and
guard boundaries retain recursive equations that cannot be reduced this way.
Multiple actual producers of one inferred failure
parameter contribute shared equality requirements; term identity alone neither
proves nor disproves equal sets. Union, fixed nominal masks, and parameter
permutations have finite normalized flows. Nested guarded contributions are
normalized before application lookup. Recursive growth in guard depth or newly introduced opaque
inference-term atoms is rejected with `CV-TYPE-GENERIC-EXPANSION` when finite
substitution cannot be established. This gate is distinct from execution or
construction resource budgets.

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
