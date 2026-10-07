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
contents identify Carven storage owners (arrays, Strings, and owning sequences),
storage views, closure owners, and callable views. These facts propagate over
aggregate and owning-element dependencies. A slice is a storage view; its element
types and native template arguments propagate only callable-view restrictions.
Pointers do not propagate target contents. Abstract root relationship construction
skips types without closure owners or callable views. Native Read passing is
selected from C++ copy and destruction traits.

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
loans inside the callee. When the callee needs storage relations, repeated
references to the same actual object share its state; distinct possible aliases
retain their identities. Equivalent formal interfaces share answers. Diagnostic
provenance selects a deterministic witness without becoming part of semantic
identity.

### Recursive storage and function interfaces

Body evaluation and calls share one referent graph. A place names a storage node
and its inline field or array suffix. Selecting a checked Sequence element follows
an owns edge immediately. Checked element type participates in selection identity,
including enum payloads with different types. Sequence elements contain no loans
or callable storage and cannot be taken through their fields. Availability follows
all possible owning sources in the current flow, including when a retained query
domain is reused before a local declaration has initialized its owner.

Checked input shapes and body operations determine whether evaluation observes
storage relations. Input loans and captures, Take, non-scalar assignments, and
stable place selections require this context. Unresolved call targets retain it
conservatively; native operations contribute their checked access and type
contracts.

Relationship production and possible storage writes also propagate through the
existing call graph. A body that combines both needs the relationship context,
including when different callees contribute each property. A read-only producer
without other relationship observations uses independent symbolic formal roles
in the same ownership solver. Local holder and escape checks remain in that
analyzer.

Calls that do not need relationship context use these symbolic roles. Actual
caller places remain substitutions for returned relationships, write effects,
and ownership-state restoration. Caller guards and live loans are checked against
those actual places. Bodies that require relations retain the full referent
interface described below.

A call interface includes parameter and capture roles, their contained
relationships, and owns paths connecting referenced roles. Projection retains
these paths and direct possible-alias relays at their endpoints. Unrelated caller
ancestors, active selections, and unpassed reader holders do not enlarge query
identity. Recursion components are strongly connected components of direct calls.
An unknown callable target conservatively reaches every body used as a callable
value outside an immediate call.

Interface roles are fixed by the formal or capture binding, relationship use,
holder path, and referent inline path. They cover storage, loan referents, and
captured targets. Each role has a set of source candidates; candidate ordinals
and allocation history do not create additional roles.
Singleton sources and definite inline callable/capture slots bound traversal.

At actual recursive feedback, sources without contained relationships, including
direct sources and owning connectors, use their complete role memberships and
forward and reverse boundary families as a region descriptor.
Forward families retain the first owning selection from a role's source set;
reverse families retain the last selection toward it. Each selection includes
its full inline path and index. A direct alias relay has no owning step, unlike
a selection with an unknown index. Arrival at a singleton boundary contributes
its family before traversal stops.

An analysis-phase interner names each complete descriptor together with its
checked type and callee. The resulting region site is the sole identity carried
between query domains; allocation, input, and region sites have distinct kinds.
The interner stores identity, while query inputs carry availability, multiplicity,
lifetime constraints, topology, and holder state. Actual source mappings remain
responsible for caller restoration.

Sources with the same region identity share a summary whose interior owning paths
join as alternatives. This finite widening retains boundary distinctions rather
than full interior path correlations; path depth and caller history do not enter
the descriptor. Sources with contained relationships and non-interface backing
or capture histories retain their site provenance. Outside recursive feedback,
finite calls and selections retain arbitrary depth and distinguish fields,
indices, owners, and subtrees. Sites outside the callee's component are renamed
in first-occurrence order.

Carrier ownership is topology, separate from copied value contents. Cardinality
and feedback provenance are separate facts: an unknown index is many and therefore
uses weak updates, but does not authorize folding a later straight-line selection.
Only loop or actual recursive feedback permits allocation/selection-site summaries.
A reused feedback summary is many, and this property propagates through existing
owns edges. Fixed input/local owning-role sets join by union and preserve every
local lifetime restriction. All destinations for a carrier/index/type selection
remain alternatives; selecting just one could withdraw previously possible aliases.

Recursive feedback queries partition inputs by their body, formal and capture
mapping, object type and site, availability and holder relationships, accesses,
reader holders, and outlives constraints. Within a fixed partition and object
layout, owns edges and possible-alias pairs join by union; multiplicity and
feedback flags join by disjunction. Queries in the same partition share this
input lattice. Relation-sensitive ordinary calls and root inputs retain exact
input identity.
Input joins preserve the role layout and holder states while accumulating
possible topology facts within a recursive partition.
Input growth schedules the query and its consumers. A pending input snapshot is
installed only at the next evaluation boundary, keeping the active evaluation's
borrowed input immutable. The input revision participates in closure checks.

Region relations interpret owns edges and inline suffixes as finite path
languages. Each owns edge contributes its carrier path and index; a queried
place retains its complete terminal suffix. A topology-owned index computes
path intersections and is rebuilt when topology changes. Pairwise alias facts
retain possible equality across call interfaces when the common caller ancestor
is absent. Owns edges describe proper storage embedding; alias pairs describe
possible equality between distinct roles.

The relation API distinguishes equal-length aliasing, overlap, ancestry, and
strict ancestry. Matching follows equal symbols or unknown-index wildcards.
Overlap permits either path to prefix the other; ancestry is directed, and
strict ancestry requires a nonempty remaining path. Cycles retain recursive
paths without dropping terminal fields or imposing a depth threshold.

Possible aliases remain pairwise relationships, not object identities or
equivalence classes. An unknown element may alias two known siblings without
making those siblings alias each other. Independent terminal fields and owning
paths remain distinct. Aggregate replacement protects selected descendants;
scalar snapshot writes do not reconstruct their ancestors. Stable selection and
loan checks apply to every write.

A query answer retains its referent domain, published topology, reachable write
effects, and normal, typed-failure, and test-stop completions.
Effects exist even when a function writes and then diverges without a completion.
They preserve structural invalidation, ordinary backing writes, and Take obligations.
Obtaining Write access is an independent guard obligation even for a known function
that performs no mutation; it does not by itself invalidate a borrowed reader or
iteration. Effects retain the actual written source and its complete inline suffix.
A store updates that source and its direct possible-alias neighbors once, using
weak updates for uncertain targets. Availability, modification, and contained
loan/capture facts follow that update. Strong replacement requires a definite
singleton with no other possible aliases, so explicit replacement of an exact
input holder can release its loans.
Callers instantiate them in the concrete caller graph against active selections and
independent live storage/callable/capture loans. Input-holder loans are proved in the
callee's ordered state, allowing an explicit replacement to release them.

Restoration maps all possible referents, returned values, failure payloads, and
modified relationships back to the caller graph. Owns edges between query input
objects are entry assumptions, including facts joined by a recursive partition.
Summary publication excludes these input-to-input edges and publishes edges
involving a fresh callee object. Restoration maps the published edges against the
concrete caller sources. Restored source mappings are sets of concrete object IDs:
distinct paths to the same referent do not add
duplicate alternatives or multiply downstream edge restoration. Distinct referents
remain distinct even when they may alias. Completion restores each modified
source once; uncertain source mappings use weak updates.
An edge or effect is hidden only when all owning sources are local; mixed external
and local sources retain external obligations. Local-only domain slots stay stable
across reevaluation but have no published edges or restored sources, so they cannot
enter later caller contexts.

### Joins and solver completion

Availability and outlives facts join by conjunction; possible relationships join
by union. A per-object modification bit records changes to ownership state;
reached writes remain effects even when that state is unchanged. Query entry
clears modification history. An unchanged completion leaves caller ownership
state alone; a modified singleton is restored exactly, while a modified summary
weakly updates all possible source objects.
Summarized relationships restore all possible backing alternatives. Ambiguous
same-site summaries retain possible loans; exact replacement requires a definite
singleton target.

The ownership solver records a dependency whenever an active query reads a call
answer. Recursive calls read the current answer; changed semantic answers schedule
their readers again. Normal, typed-failure and test-stop completions and reachable
effects join monotonically.
Retained identities stay fixed; graph facts grow monotonically. Completion states
expand to the shared referent domain, including exits temporarily absent while a
callee answer is pending. Finite source sites, inline paths, interface roles,
and graph relations bound query identity; solver-growth budgets provide evidence
for representative source families. Loop headers
accumulate the entry and reached backedges, including while a pending call has no
answer.
Break, return, failure, and test-stop exits accumulate alongside the header.
Convergence requires both state equality and an unchanged topology revision;
new owns edges, owning sources, and many upgrades affect the next transfer even
if the current object rows happen to be equal.

Each evaluation returns its transfer answer, diagnosis, and return-copy
observations. A query retains its latest diagnosis while the solver accumulates
an inductive upper bound. For a candidate answer `A` and a fresh transfer
`F(A)`, closure means `join(A, F(A)) == A` with unchanged topology and input
revisions.

Answer, input, or topology growth invalidates closure and schedules reevaluation
of the query and its readers. An empty worklist requires every query's closure
certificate. Each retained diagnosis then observes the final callee answers and
checks a complete transfer covered by its accumulated answer. Semantic equality
excludes diagnostic origins and locations witnessing a Take.
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
structural replacement while allowing element updates. Match guards additionally
require stable subject storage.
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
