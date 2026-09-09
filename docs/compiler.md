# Compiler architecture

This document describes semantic construction and analysis, including publication
gates, ownership boundaries, and dependency direction.

## Pipeline

`compiler/` owns the compilation input contract and end-to-end orchestration.
`compiler.request` describes the closed source batch and depends only on source
types; the frontend consumes this contract independently. `compiler.compile`
sequences parsing, semantic analysis, and artifact generation. Command-line
input preparation and filesystem output belong to `driver/`.

```text
CompilationRequest → SyntaxProgram → ProgramDraft → SemIRProgram
```

`parse_program` parses the closed source batch and resolves module imports.
`analyze` constructs declarations and typed structured bodies, solves types and
failure sets, validates contracts, checks ownership and callable loans, and
publishes an immutable semantic program. Errors prevent delivery; warnings accompany
a successful result.

## Publication gates

The analysis driver collects declaration identities, resolves function heads and
required constants, and elaborates bodies. Function results are completed on demand;
a dependency on an active, unknown result produces an inference-cycle diagnostic.
Known signatures support recursive calls. Each body is elaborated once.

`ProgramDraft` owns pending function heads. A complete callable contract includes
its result. Declaration-head completion closes the nominal tables; solving requires
complete callable contracts and bodies. The catalog and import-use state end before
solving. Final semantic validation checks declaration surfaces, including closure
captures and solved failure sets.

`analysis.program` owns `ProgramDraft` and declaration/body reservations.
Its consuming `finish()` checks reservation completeness, solves failures and types,
and finalizes callable signatures, declarations, and bodies in that order. Each
body reservation has one slot containing its kind, test owner when applicable,
and optional completed draft; callable ownership remains in declarations.
Body completion resolves type and failure facts in the owned operation tree in
place, borrowing only the solutions and diagnostic sources. Completion and
read-only checks share the structural child traversal; completion owns only fact
updates and catch diagnostics. Binding and pattern
tables are converted to their final records. The `SemIRBody` boundary checks that
every operation and region contains resolved facts, including inactive source.

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
and binding relations used by global checks. Ownership analysis prepares and solves ownership facts.
The operation tree moves with its owning program. Successful checks deliver the
program.

Pointer nullability walks the existing structured operations and merges local
slot facts across normal and abrupt exits. It does not introduce CFG/SSA or
interprocedural analysis. Indirect places check address availability and target
access without assigning a local owner to the referent. Pointer targets are
leaves for owned-content, loan-content, and infinite-size containment queries.

## Ownership and identity

`SyntaxProgram` owns source provenance, syntax trees, and resolved imports.
`ProgramDraft` consumes it and owns mutable declarations, canonical interning,
construction types, failure constraints, and body construction. Declarations
may reserve identities for recursion and forward references.

Program IDs belong to one program. Binding, pattern, and lifetime IDs belong
to one body. Name frames exist only during name resolution; bound operations
retain binding identities and lifetimes. Owning query surfaces validate identity
and range. Expression, statement, and region occurrences are recursively owned
values, including during construction. Each binding selection creates a new
occurrence; projections own their receiver and index. Function names construct callable occurrences directly.
Construction results read types and constants from their owned expressions.
Temporary expressions have no identity table. `BodyType` and `BodyFailures`
store construction or resolved facts; their accessors explicitly select the
required stage. Published bodies expose only const access to the completed tree.

Solving consumes construction state. `SemIRProgram` retains resolved
semantic data and provenance; lowering does not query source syntax. Program,
provenance, plan, and table owners permit move construction where required, but
not replacement through move assignment. Borrows end before an owner moves or
is consumed. Construction readers return copies because storage may grow;
final views borrow immutable storage. Query boundaries check identity and bounds;
construction boundaries check unique definitions and structure.

Provenance resolves an origin directly to `ProgramSourceID` and `Span`.
Only diagnostic transport converts that identity to the source manager domain.

## Structured semantics

Bodies retain conditionals, loops, matches, handlers, lexical scopes, and exits.
Places describe storage identity and projection evaluation. Values describe
computation. Initialization, assignment, and Take remain distinct operations.
Children and operation contracts specify evaluation order. Range sources explicitly
select integer bounds or a sequence expression.

Bindings carry their role and access. Scope and full-expression boundaries
record lifetimes. Function return, failure propagation, loop transfer, and test
exit retain their destinations. Nested callables have separate boundaries.

Match distinguishes a source place from an owned temporary. Pattern owners and
guards retain their order. Constant-inactive source is validated but contributes
no executed operations or ownership transitions.

## Solving and validation

Canonical types, callable signatures, constants, and failure sets are the
published query surfaces. Construction types and failure terms are solved
before publication. Failure inference computes the least fixed point of the
program's failure constraints. Enum declarations already have their final
representation: numeric underlying types must be concrete integers during
declaration resolution. Their table seals directly; struct fields and enum-case
payload types still require construction-type resolution.

`analysis.expr` interprets each expression once, with concrete constant and body
sites. The interpreter owns contextual typing, operation selection, enum and
text rules, and their diagnostics. Sites provide real scope lookup, receive
judged results, and handle execution-only syntax. Children return to the same
interpreter. Static syntax admission is a separate node-local rule; declarations
retain lazy completion and cycle diagnostics.

Constants have one normalized `ConstantFact` representation and `SemConstant`
occurrences. Module constant declarations retain binding metadata and a
`ConstantID`; the fact owns the type, so their declaration table seals directly.
Floating-point identity uses bits; language equality compares
numeric values and recursively compares aggregate contents. An expression’s
constant fact describes its value on normal completion; it does
not establish static syntax admission or permission to delete execution. Body
operations retain operands and effects even when their result is known.

The read-only SemIR evaluation contract classifies an operation as requiring
execution, requiring only its executed operands, selecting short-circuit
operands, or requiring no execution when discarded. It consumes resolved
operations, types, and constant facts. Storage reads are separate from execution
requirements, so a removable read can still require a snapshot before a later
mutation. Calls and native operations are conservative; checks, ownership
operations, and floating computations retain execution. Before lowering a body,
one traversal prepares execution, storage-read, full-expression lifetime, and
external-exit facts from children to parents. The backend associates facts with
stable node addresses and releases them with that lowering. Shared type rules
consume the relevant stage's facts.

Local construction checks its preconditions. Program validation checks owner
and range relations, type and call contracts, lifetime and control legality,
binding relations, declaration topology, and cross-body callable relations.
Each declaration and body has its required unique owner; each closure has one
construction site and one body.

## Ownership and callable loans

An ownership flow has an optional normal completion containing its state and
result relationships. Every exit carries a state. Return exits carry result
relationships, failure exits carry a failure type, and loop transfers distinguish
break from continue.

An ownership batch prepares local object descriptions, relative temporary
positions, lifetime membership, and pattern acceptance once for each final body.
Type contents are prepared once from the final type and declaration stores,
without borrowing construction state. Coverage borrows pattern tables and stage type facts; exhaustive
queries and full diagnostics share its algorithm. Catch acceptance describes the
current arm, independently of coverage accumulated by earlier arms.

Queries map relative local positions after their external input objects. Only
aliases, accesses, availability, relationships, and execution state vary between
queries. Availability belongs to an
owner; holder relationships belong to storage positions within that owner.
Known field and element writes replace the relationships at that position;
unknown element writes merge possible relationships. Ordinary scalar elements
need no relationship rows.

Value captures carry copied relationships. Write captures refer to live
storage, so consumers follow the target's current contents. Lexical regions and
full expressions release their own objects on each normal and control exit.
A result's destination lifetime does not change the execution position.

Calls map parameters and captures to actual storage. Aliases share one state,
including the order of external writes. Call answers contain returned
relationships and external state for normal and typed failure completion;
completed locals are discarded. Queries normalize reachable input storage,
aliases, active accesses, and relative lifetimes. Relationship sets are sorted
and unique after projection and remapping; equivalent facts retain
a deterministic diagnostic witness without making provenance part of semantic
identity. Queries are indexed by body and reuse equal inputs.

A private dependency queue evaluates new queries and reevaluates consumers only
when an answer changes. Dependencies include reads of unfinished answers and
retain previously observed consumers when call contexts change. Recursive calls
read the current answer rather than recursively executing analysis. New contexts
can temporarily remove an exit, so the solver does not assume that each answer
only grows. Quiescence requires an empty queue after all new queries and answer
changes have propagated. Diagnosis then reads the sealed query set in discovery
order; it cannot create new queries or change solved answers. Query state remains private to semantic
analysis and never enters generation or runtime.

Unfinished calls retain direct place and borrowed-target accesses. Array
iteration retains its source owner. Match guards additionally require stable
subject storage. Branches merge only real successors; loops include entry,
backedges, and exits. Diagnostic witnesses do not distinguish execution states.
Return and failure states have caller consumers; test termination has none.
Equal callable-view copies retain their target relationships rather than borrowing
the intermediate view storage. Expired callable backing is diagnosed before any
attempt to interpret its former capture state.
Loop solving reuses its final stable result; only diagnosis replays the body
with the converged loop-entry state.
All source operations receive contract checks independently of execution-state
analysis, including unreachable source.

## Diagnostics and dependencies

Source operations retain origins. Implicit operations use linked expansion
origins. Diagnostics are produced at the rule owner. Compiler-private invariant
failures terminate at the violated boundary.

Frontend facilities depend on source and syntax. Semantic construction consumes
syntax; SemIR owns data, storage, and structural contracts without depending on
AST or analysis. Post-solve analysis consumes final operations. The backend consumes published
semantics. Runtime support and build orchestration do not determine Carven
access or ownership legality.

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

External types are named type expressions, intrinsic types, or `CppQueryType`
descriptions. Queries describe C++ expression shapes using operand types and
access.
Identical descriptions share a type record; this does not establish equivalence
between different C++ type expressions. Operation occurrences own diagnostics,
lifetimes and execution order. Query references describe type dependencies,
not the contents or ownership relationships of a result object.

Semantic construction owns result-query derivation for external operations.
Publication checks reference integrity and Carven-owned operation contracts.
Ownership analysis checks access, known callable borrows and Write captures;
C++ determines the validity and results of delegated native operations.
