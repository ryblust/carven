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
Unpassed holders that constrain backing storage remain concrete caller readers.
Passed Write holders retain their identity so exact replacement can release their
loans inside the callee. Aliases share one state, including the order of external
writes. Equivalent formal interfaces share answers; diagnostic provenance selects
a deterministic witness without becoming part of semantic identity.

### Recursive storage and function interfaces

Body evaluation and calls share one referent graph. A place names a storage node
and its inline field or array suffix. Selecting a checked Sequence element follows
an owns edge immediately. Checked element type participates in selection identity,
including enum payloads with different types. Sequence elements contain no loans
or callable storage and cannot be taken through their fields. Availability follows
all possible owning sources in the current flow, including when a retained query
domain is reused before a local declaration has initialized its owner.

A call interface includes parameter and capture roles, their contained relationships,
and owns paths connecting distinct referenced roles. Unrelated caller ancestors,
active selections, and unpassed reader holders do not enlarge query identity.
Direct empty owning-selection parameters receive callee-relative formal roles.
Contained backing and capture objects keep allocation provenance in the current
recursion component; otherwise recursive local arrays or closures would repeatedly
introduce new formal roles. Recursion components are strongly connected components
of direct calls. An unknown callable target conservatively reaches every body used
as a callable value outside an immediate call.

Exact current roles and definite inline callable/capture slots are protected from
site-only merging; actual possible aliases share conservative state. At an actual
SCC boundary, owning connectors between roles use the complete set of current
owning-anchor and first-edge families plus checked type. Each family retains the
first edge's full inline path and index. Current referenced roles stop propagation
and restart their own anchors. Family and connector ordering are canonical, and
historical SCC body sequences do not enlarge owning-context identity. The same
possible-alias closure governs anchor groups and state normalization.

Non-owning contained backing and capture histories instead retain their finite
allocation sites `(BodyID, slot, input-or-local, element-selection)` in the SCC.
A former formal role that becomes a connector keeps its semantic site, rather than
being renamed to the current object number. Outside SCC feedback, finite calls and
selections retain arbitrary depth and distinguish same-type fields, indices,
owners, and subtrees. Sites outside the callee's component are renamed in
first-occurrence order without retaining caller history.

Carrier ownership is topology, separate from copied value contents. Cardinality
and feedback provenance are separate facts: an unknown index is many and therefore
uses weak updates, but does not authorize folding a later straight-line selection.
Only loop or actual recursive feedback permits allocation/selection-site summaries.
A reused feedback summary is many, and this property propagates through existing
owns edges. Fixed input/local owning-role sets join by union and preserve every
local lifetime restriction. All destinations for a carrier/index/type selection
remain alternatives; selecting just one could withdraw previously possible aliases.

Region relations use finite automata over the same graph. Each owns edge contributes
its carrier's inline path and index; each queried place ends with its complete
inline suffix. Cycles represent recursive path languages rather than dropping the
terminal field or applying a depth threshold. A finite product walk starts at shared
ancestor objects and matches equal symbols or unknown-index wildcards. Prefix
intersection determines possible overlap; directed prefix intersection determines
ancestor protection, with a nonempty remaining path for strict ancestry. Equal-length
intersection alone identifies possible aliases for role grouping. Thus deep recursive
text backing can overlap a finite selection while independent `text`/`other` suffixes
and sibling owning paths remain disjoint. Scalar snapshot writes cannot reconstruct
a selected ancestor; aggregate replacement retains structural protection even when
its type contains no owning storage. Stable selection and loan checks still apply
to every write.

A query answer owns one retained referent domain and owns-edge set, a monotone union
of reachable write effects, and its normal, typed-failure, and test-stop completions.
Effects exist even when a function writes and then diverges without a completion.
They preserve structural invalidation, ordinary backing writes, and Take obligations.
Obtaining Write access is an independent guard obligation even for a known function
that performs no mutation; it does not by itself invalidate a borrowed reader or
iteration. Typed equal-length alias classes expand effect alternatives without
removing original targets or their full inline suffixes. Store, restoration, and
completion use the same alias domain for weak state updates, including modification
bits, available/taken state, and contained loan/capture facts. Strong replacement
requires a definite singleton with no other possible alias alternatives, so explicit
replacement of an exact input holder can still release its loans.
Callers instantiate them in the concrete caller graph against active selections and
independent live storage/callable/capture loans. Input-holder loans are proved in the
callee's ordered state, allowing an explicit replacement to release them.

Restoration maps all possible referents, returned values, failure payloads, and
modified relationships back to the caller graph. Only new callee owns edges are
restored: existing input edges already have concrete caller associations, and a
cross product of grouped input edges would invent aliases. Summary updates are weak.
An edge or effect is hidden only when all owning sources are local; mixed external
and local sources retain external obligations. Local-only domain slots stay stable
across reevaluation but have no published edges or restored sources, so they cannot
enter later caller contexts.

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
their readers again. Normal, typed-failure and test-stop completions and reachable effects join monotonically.
All retained completion states expand to the shared referent domain, including
exits temporarily absent while a newly requested callee answer is pending.
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
