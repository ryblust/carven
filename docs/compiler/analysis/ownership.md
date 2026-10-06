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
contents identify Carven storage owners (arrays, Strings, and owning sequences), closure owners,
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

### Recursive storage

Recursive storage uses direct backing edges. Call normalization preserves exact
identities for unambiguous interface roots and their inline callable/capture
storage. Other reachable objects are grouped by storage site
`(BodyID, slot, input-or-local, element-selection)`. Selection roles bound
abstract grouping; they do not establish exact storage identity. Query identity
retains a local site only when its
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

Body evaluation and call normalization share a referent graph. A place names
one storage node and its inline fields or array elements; selecting a checked
Sequence element follows an owns edge immediately. The checked type remains
part of selection identity, including when enum payload slots have different
types. Checked Sequence elements contain no loans or callable storage and cannot
be taken through their fields. Their availability follows all possible owning
sources in the current flow state, including after a retained query domain is
reused before local initialization.

Carrier ownership is a separate topology and is not copied with a value.
Straight-line selection uses carrier, index, and checked element type without
folding repeated types or limiting traversal depth. At loop feedback or recursive
summary boundaries, an allocation site and its first owned-selection family name
a finite node. Source and feedback sites, inline carrier paths, and constant or
unknown indices contribute to this key; owned ancestry and caller history do not.
A reused summary is many, and that property propagates through existing owns
edges. Owning-source sets contain only the query's fixed input and local roles;
unioning them preserves local lifetime and escape restrictions across aliases.

Carrier ancestors remain reachable evidence even without parameter roles;
non-interface recursive ancestors use storage-site grouping. Repeated recursive
sites form graph cycles rather than extending an ancestor chain. Unknown indices
denote many regions. Every known destination for a carrier/index/type selection
remains an alternative; selecting the first edge would make graph growth withdraw
previous backing facts.

One region projection supplies overlap and ancestor checks, hidden reader loans,
and retained accesses. Direct edges preserve the complete inline suffix after
an index, allowing independent fields to remain disjoint at arbitrary finite
depth. Recursive cycles widen to a selected descendant region for structural
protection. A common widened bound does not identify the fields of two referents.
Different paths to the same carrier remain alternatives; cycle summaries continue
to external ancestors.

Call completion retains new referents and their owns edges as well as result,
failure, and modified-storage relationships. Restoration maps all possible
referents to the caller graph and preserves finite returned field suffixes. Only
new callee edges are restored: existing input edges already have concrete caller
associations, and rebuilding grouped input edges as a cross product would invent
aliases. New summary relationships union all possible sources and use weak
updates. Known different indices remain disjoint for element updates; structural
carrier mutation invalidates their loans. Local-only domain slots stay stable
across reevaluation but have no published edges or restored sources, so they do
not enter later caller contexts.

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
bound query identity. Internal query, worklist evaluation, storage-node, and storage-edge counts
measure solver growth; evaluation counts exclude contract checks and do not count
individual loop iterations. Loop headers accumulate the entry and all reached
backedges instead of discarding earlier facts when a pending call has no answer.
Break, return, failure, and test-stop exits accumulate alongside the header.
Convergence requires both state equality and an unchanged topology revision;
new owns edges, owning sources, and many upgrades affect the next transfer even
if the current object rows happen to be equal.

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
retains its source owner. Sequence traversal also retains its carrier against
structural replacement while allowing element updates. Match guards additionally require stable subject storage.
Payload alias bindings resolve through the selected storage map; alternatives
retain the union of candidate projections. Their arms pin enclosing storage,
and leaving an arm restores the enclosing selection environment.
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
across normal and abrupt exits. Indirect places check address availability and target
access without assigning a local owner to the referent. Pointer targets are
leaves for owned-content, loan-content, and infinite-size containment queries.
