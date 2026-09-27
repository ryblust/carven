# Semantic representation

This reference defines identity, canonical data, structured operations, and facts
available to semantic-program consumers. The [compiler overview](../README.md) defines
owner lifetimes and publication order; [construction](construction.md) establishes
the facts described here.

## Ownership and identity

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

Provenance resolves an origin directly to `ProgramSourceID` and `Span`.
Only diagnostic transport converts that identity to the source manager domain.

## Canonical stores

Every canonical type store establishes the complete builtin type domain at
construction. Builtin identities remain stable through publication; lookup is a
read-only operation independent of source usage and prior constant evaluation.
Compound types remain demand-driven. Shared execution may consume either a draft
or a published program, but type queries have the same guarantees in both contexts.
Operations use their resolved result types where available.

Canonical facts and spellings remain stable across appends; moving or sealing their
owner ends outstanding borrows. Types still return copies during construction.
Constant interning uses a hash index with exact equality checks and deterministic
insertion-order IDs. Floating identity uses bits; numeric equality remains separate.

## Type contents

`SemIRProgram` owns immutable `TypeContents` computed after storage topology
validation. Containment facts can coexist and propagate according to each
property's storage and borrowing rules. String storage containment excludes
zero-length array elements. Read queries derive storage-borrowing
and value-snapshot guarantees from these facts. Native value containment lets
the backend preserve C++ copy and destruction behavior within Carven aggregates;
Read passing for native values without owned Carven storage is resolved through
C++ traits.

Published-program consumers read these facts through an identity-checked query.
Constant execution during construction queries the draft's type facts, including
types whose dependencies are still being completed.

## Structured semantics

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
`Len`, `IsEmpty`, `Bytes`, and `Chars` accept `str` or `String` receivers;
byte-slice constraints require `[u8]`. Required root admission belongs to analysis,
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

Match construction checks coverage over the subject type before applying a known
subject value. Pure patterns that cannot match are unreachable; reachable arms
can record that their pattern always matches. Failure inference, test-stop,
ownership, nullability, and realization consume these facts. Subject evaluation
and guards retain their execution obligations. Selection handles root literals,
static ranges, payload-free enum cases, and unconditional patterns; dynamic bounds
and compound binding paths retain ordinary matching. Publication verifies a
recorded match success against the subject fact and pattern. Explicit callable
failure contracts and source constant-expression admission remain independent.

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
guards retain their order. Constant-inactive source is validated but contributes
no executed operations or ownership transitions.

### Callable adaptation

`semantic.semir.callable` classifies completed callable adaptations as target
copy, function target, stateless closure, or object borrow. Ownership and backend
preparation share this query; array elements use the same classification.

## Known results and execution requirements

Constants have one normalized `ConstantFact` representation and `SemConstant`
occurrences. Module constant declarations retain binding metadata and a
`ConstantID`; the fact owns the type.
Floating-point identity uses bits; language equality compares
numeric values and recursively compares aggregate contents. An expression’s
constant fact describes its value on normal completion. Syntax admission and
execution requirements are checked separately; known results retain required
operands and effects. Publication checks that every attached normal-completion
fact belongs to this program and has the expression's exact resolved type.

Immutable Boolean locals supply normal-completion facts to conditions, guards,
and Boolean selection before failure solving. Their initializers retain required
execution. These facts do not change admission for source constant expressions;
integer arithmetic wraps in both required and runtime execution.

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

Known nonzero integer divisors and in-range shift counts remove the operation's
termination obligation. Discarded operations still evaluate required operands.
This fact is independent of whether C++ can express the value with a native
operator, including signed minimum divided by negative one.

## Program validation

Local construction checks its preconditions. Program validation checks owner
and range relations, type and call contracts, lifetime and control legality,
binding relations, declaration topology, and cross-body callable relations.
Each declaration and body has its required unique owner; each closure has one
construction site and one body.
