# Ownership analysis

This reference describes ownership flow, backing relationships, interprocedural
queries, and local pointer nullability. These analyses consume resolved semantic
facts and run before semantic publication.

## Ownership and backing relationships

### Flow state and type contents

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

Ownership and nullability use `semantic.analysis.pattern.control` to propagate
states through the pattern table and dynamic bounds, using type coverage to
identify exhaustive acceptance. Ordered alternatives follow rejection; enum
payloads follow acceptance. Range bounds execute in order,
and outward exits bypass later bounds and alternatives. Each analysis supplies
expression evaluation, state joins, binding projections, and its guard and catch
destinations.

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

Static execution queries the same type contents over completed declaration
fields during construction. This supplies storage observation rules for its
admitted types; native copy and destruction traits remain C++ responsibilities.

### Selected storage and loans

Expression results carry selected storage separately from their value's contained
relationships. Bindings and projections select existing objects; owned value
results establish temporary storage. Copying into a destination copies contained
relationships, not the source object's identity. Read parameters and range bindings
use the shared resolved-type storage policy. Guaranteed value Read parameters
copy contained relationships without retaining the source holder identity;
borrowing Read parameters retain selected objects, including multiple possible
backing objects of a slice. Native-containing Read types retain conservative
storage handling because their ABI depends on C++ traits. The backend consumes
the same policy.

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

### Call queries

Calls map borrowing parameters and captures to actual storage. Guaranteed value
Read parameters discard outer aliases before reachable-object discovery, while
retaining their contained loans and captures. Abstract root inputs use the same
policy. Argument evaluation still checks direct Read/Take conflicts before this
call-boundary normalization. Capture holders retain their existing storage rules.
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

Callable values read through indirect storage retain opaque target descriptions
when no tracked target is available. Unknown calls contribute normal completion
and declared failure paths; callable results retain opaque target descriptions.
Known capture relationships at a join continue through body analysis alongside
the opaque alternative. These descriptions supply neither backing storage nor
capture aliases; creating a new callable borrow still requires tracked backing.

### Recursive storage

Recursive storage uses direct backing edges. Call normalization preserves exact
identities for unambiguous interface roots and their inline callable/capture
storage. Other reachable objects are grouped by allocation site
`(BodyID, slot, input-or-local)`. Query identity retains a local site only when its
body shares the callee's recursion component, because only such a body can
allocate at that site again during the callee's execution. Other sites are
renamed to callee-relative input sites in first-occurrence order, preserving their
equality within the input without recording which ancestor allocated them.
Recursion components are strongly connected components of direct call targets;
a call without a concrete target conservatively reaches every body that is used
as a callable value outside an immediate call. A summary that may represent
multiple objects retains that property through subsequent calls. Exact inline traversal
stops at slice backing and ambiguous or unknown-index targets. The semantic restrictions
on callable-view storage in nominal types and captures bound inline callable
chains. Allocation identity is separate from diagnostic provenance.

### Joins and solver completion

Availability and outlives facts join by conjunction; possible relationships join
by union. A per-object modification bit distinguishes an untouched caller object
from a write whose abstract edges happen to be unchanged. Query entry clears
modification history. An unchanged completion leaves the caller object alone;
a modified singleton is restored exactly, while a modified summary weakly updates
all possible source objects. Summarized relationships restore all possible
backing alternatives. Ambiguous same-site summaries retain possible loans;
exact replacement requires a definite singleton target.

The ownership solver records a dependency whenever an active query reads a call
answer. Recursive calls read the current answer; changed semantic answers schedule
their readers again. Normal, typed-failure and test-stop answers join monotonically.
The finite source sites, inline paths, distinguished roles, and graph relations
bound query identity. Internal query and worklist evaluation counts measure solver
growth; evaluation counts exclude contract checks and do not count individual loop
iterations.

Each evaluation returns its transfer answer, local errors, and return-copy
observations. Diagnostic checks preserve transfers, including access and lifetime
witnesses. A query retains its latest diagnosis while the solver accumulates its
answer. When the worklist is empty, each diagnosis observes the final semantic
answers it consumed, and its evaluation's transfer must agree with the accumulated
answer under semantic equality.
Semantic equality excludes diagnostic origins and locations witnessing a Take.
Loops retain the witnesses selected by the converged join even when no semantic
fact changes.

Contract diagnostics follow source-body order; solved query order determines
execution-state diagnostic publication. An escaping callee-local relationship
stops solving and publishes the offending input's diagnosis. Return-copy
observations combine across successful contexts; warnings are published only
after every query succeeds. Solver and diagnosis state remain private to analysis.

### Control flow and diagnostics

Unfinished calls retain direct place and borrowed-target accesses. Array iteration
retains its source owner. Match guards additionally require stable subject storage.
Branches merge only real successors; loops include entry, backedges, and exits.
Diagnostic witnesses do not distinguish execution states.
Return, failure, and test-stop states have separate transfer paths. Test stop
propagates through Carven calls to the active test body.
Equal callable-view copies retain their target relationships rather than borrowing
the intermediate view storage. Expired callable backing is diagnosed before any
attempt to interpret its former capture state.
Loop diagnosis uses the converged loop-entry state.
All source operations receive contract checks independently of execution-state
analysis, including unreachable source.

## Pointer nullability

Pointer nullability analyzes structured operations locally and merges slot facts
across normal and abrupt exits. Comparisons with pure null operands refine stable
slots on their corresponding edges. Pure null operands are null constants and
pointer defaults; a computed null result does not establish that its evaluation
left another slot unchanged. Indirect places check address availability and target
access without assigning a local owner to the referent. Pointer targets are
leaves for owned-content, loan-content, and infinite-size containment queries.
