# Sequence views and pipeline calls

- **Status:** Accepted
- **Implementation:** Not started
- **Scope:** Pipeline call syntax, bounded sequence builtins, reusable lazy Read views, and their static execution and realization

## Summary

Provide a small set of sequence operations that compose through ordinary calls
and a `|>` operator. The first version contains `filter`, `map`, `take`,
`collect`, and `fold`. Existing `for` loops consume the resulting views.
Functions can package a processing rule by returning a view with an inferred
concrete type.

Views describe demand-driven traversal without constructing intermediate result
collections. This supports reusable selection and transformation while leaving
imperative processing available at the consumption site. `collect` explicitly
creates an owning result; `fold` explicitly performs ordered accumulation.

The design assigns the five API identities, concrete source and callback checks,
and view type derivation to the compiler. Ordinary functions package processing
rules.

Source semantics are independent of target APIs. Ordinary nested calls and
pipeline calls provide the same checked contracts to semantic execution and
C++ generation.

The examples describe the accepted design, which is not yet implemented.

## Context

Carven has integer `Range<T>` values, fixed arrays, Read slices `[T]`,
owning `Vector<T>`, concrete closures, and inferred function results. `for`
recognizes a fixed set of iterable types. It does not currently provide these
sequence adapters or a public protocol for user-defined iterables.

Read snapshots pure values and borrows storage-containing values. A live slice
protects its backing owner against mutation, replacement, Take, and destruction.
Write is nonexclusive. These rules remain the basis for sequence access and
lifetimes.

Existing builtins demonstrate bounded compiler knowledge. `print` checks a
heterogeneous argument list without a public parameter-pack mechanism. Async
composition derives operation results and failures without requiring users to
express the composition implementation through general generic facilities.
Sequence APIs follow this separation between usable contracts and publicly
expressible implementations.

## First version

The initial source families are Read slices `[T]`, integer `Range<T>`, and views
produced by the three lazy adapters. Arrays and vectors enter through their
existing slice conversion or `.slice()` operations.

Traversal is synchronous, ordered, finite, and restartable. Every traversal has
fresh progress. Callbacks run again on another traversal; restartability does
not guarantee identical results when callbacks observe or modify other storage.
Integer ranges retain their existing reversed-range and closed-endpoint rules,
including traversal through an integer type's maximum value.

Elements have Read access and callbacks have no outward typed failures.
Ordinary `for` consumes views, including within static execution; its body keeps
existing mutation, control flow, and failure handling.

Custom iterable protocols, mutable or consuming views, fallible or asynchronous
traversal, additional combinators, and ECS facilities are outside this version.

## Source design

### Pipeline calls

Pipeline call composition is:

```carven
source |> operation(a, b)
// Same call as operation(source, a, b).

source |> first(a) |> second(b)
// Same calls as second(first(source, a), b).
```

Pipelines associate to the left. Each step inserts its left operand as the
first explicit argument of the call on the right. The selected callable follows
ordinary lookup, arity, contextual typing, access, stage, and failure rules.
The operator is available to ordinary user functions as well as sequence APIs.

The right side is one explicit call to a name or qualified name, optionally with
existing type arguments and a trailing `?`. A name can resolve to a local
callable binding. Bare callables, member-call rewriting, arbitrary callee
expressions, partial application, and argument placeholders are not admitted.

`|>` binds less tightly than ranges and all ordinary binary operators. It adds
one expression level below the existing range production:

```ebnf
expression = access-expression | pipeline-expression;
pipeline-expression = range-expression, { "|>", pipeline-call };
pipeline-call = named-callee, [ type-arguments ], call-operation,
                [ propagation-operation ];
named-callee = [ "::" ], IDENTIFIER,
               { "::", identifier-shaped-name };
```

The reused productions keep their current meaning. Using a pipeline result in a
postfix or binary operation requires grouping, as in
`(values |> collect()).slice()` or `(values |> fold(0, add)) + 1`.
A trailing `?` propagates from that step's inserted call:

```carven
0..10 |> take(3)          // take(0..10, 3)
value |> convert()?      // convert(value)?
values |> inspect(&log)  // inspect(values, &log)
```

The inserted argument is unmarked Read. Grouped Write or Take inputs are
rejected; calls requiring either on their first parameter use ordinary syntax.
Arguments on the right retain their access markers. A leading marker still
covers the whole expression: `use(&value |> produce())` is
`use(&produce(value))`, subject to normal access checking.

Every left operand and explicit argument is evaluated once. Evaluation order,
callee selection, full-expression temporary lifetimes, and cleanup are those of
the equivalent ordinary calls. In particular, ordinary calls select their
callees before evaluating arguments; pipeline spelling does not introduce
separate statement boundaries or a different callee-selection order.

Calls execute normally; deferred element processing belongs to each adapter's
contract. Pipeline spelling supplies no implicit access, propagation, or stage
change.

### Builtin API identity

The APIs belong to `std::sequence` and use existing import selection rules:

```carven
import std::sequence using map;
import std::sequence using { filter, map, take, collect, fold };
```

Single-name selections may omit braces; multiple-name selections require them.
Builtin recognition follows the resolved declaration identity, including import
aliases. A user declaration named `map` keeps its own meaning. Builtins use
direct calls, including pipeline calls; passing a builtin itself as a callable
value is outside the first version. User wrapper functions are ordinary callables.

The contracts below describe concrete call checking. `T`, `U`, and `A` in the
table are explanatory type names, not proposed public generic declarations.

| Operation | Checked inputs | Result and execution |
| --- | --- | --- |
| `filter(source, predicate)` | Source element `T`; Read callback `T -> bool` | Lazy view retaining accepted source elements in order |
| `map(source, transform)` | Source element `T`; Read callback `T -> U` | Lazy view of callback results in order |
| `take(source, count)` | Admitted source; nonnegative `usize` count | Lazy view delivering at most `count` elements |
| `collect(source)` | Source element `T` admitted for `Vector<T>`, including sequence views | Eager independent `Vector<T>` of delivered elements |
| `fold(source, initial, combine)` | Copyable Read initial value `A`; Read callback `(A, T) -> A` with two explicit parameters | Eager owning accumulator after traversal in source order |

`filter` and `take` preserve the origin of an element. `map` produces a new
result value for each element that reaches it. Borrowed contents in that result
retain their ordinary backing requirements.

`fold` establishes its own accumulator from the Read `initial` after argument
evaluation. Its concrete type must admit ordinary owning-copy initialization,
including for empty input and temporary initializers. A noncopyable `Operation`
or a Take initial argument is rejected. Pure arguments retain their snapshots;
borrowed holders retain their observation rules. Fresh temporaries may use
ordinary value delivery. A named initial owner remains available, and contained
borrows keep their backing requirements.

`combine` reads the current accumulator and element, then supplies the next
accumulator through ordinary result delivery. Relationship checks cover the
result and external writes before the previous holder is released. Empty input
returns the independent accumulator without invoking `combine`; a Vector initial
therefore yields a separate Vector owner. Accumulation stays in source order.
Copy admission uses existing concrete type rules.

Concrete named functions and concrete closures are the initial callback forms.
The compiler checks their existing parameter, capture, result, and failure
contracts. Closure parameters require explicit types; results use existing
inference rules. `map` admits `void` as the ordinary unit value `()`. Each
callback invocation produces one unit element under the same demand rules as
other results. Callback type formation must complete before a builtin accepts
it, including its outward failure set.

Generic bodies retain their definition-site restrictions on unknown types.
Existing whole-value generic forwarding remains available.

### Constructing and composing views

Calling `filter`, `map`, or `take` evaluates its source, callback expression, and
other arguments under normal call rules. It establishes the descriptor and
capture storage required for later traversal. It does not invoke the
element-processing callback or request an element.

The storage model copies slice descriptors and snapshots integer range
bounds. Composition retains upstream descriptors and concrete callback storage
without borrowing an expiring upstream adapter temporary. Named descriptors and
closures use owning-copy rules; fresh temporary results use normal value
delivery. Captured owned values are copied as required, while contained borrows
and Write captures retain their referents. Each traversal uses the callback
storage retained by the view; it does not make another callback copy per element.

Adapter construction creates no intermediate result collection. Callback
construction and capture copies can still allocate or perform ordinary effects.
Copying a view copies its descriptor and owned callback state, preserving source
loans without copying the borrowed collection or extending its lifetime.

View types have no source spelling. Inferred locals and function results,
whole-value generic forwarding, composition, and ordinary closure captures
preserve the concrete type. Storage, copies, and whole-owner Take follow existing
concrete-value and borrowing rules. Sequence views are admitted Vector elements,
including as fields of otherwise admitted element products. Callable-view
admission is unchanged; explicit native import/export signatures do not admit
sequence views.

Diagnostics display the source shape, such as `map([i32], <closure>)`, with the
element type and a callback declaration or source location when needed. This
display is descriptive and is not a public type expression.

### Traversal and demand

Consumption requests elements from the outermost view. A request proceeds
through upstream stages in source order until it produces an element or reaches
the end. The rules are:

- `filter` evaluates its predicate once for each candidate it receives. A
  rejected candidate does not reach downstream stages.
- `map` invokes its transform once for each element it receives. The resulting
  value remains available to downstream stages processing that element;
  downstream inspection does not recompute the transform.
- `take` counts delivered elements. Once its count is satisfied, it makes no
  further upstream request. `take(source, 0)` makes no element request, even
  though construction arguments have already been evaluated normally.
- `collect` and `fold` consume until the source ends. `for` consumes until the
  source ends or its existing control flow leaves the loop.

For example, `source |> map(transform) |> filter(predicate) |> take(2)` runs
`transform` on each candidate needed to find two accepted results. Replacing
this with a filter before the map can change callback work and observations.

`break` stops further requests. `continue` advances to the next request. Each
traversal starts with fresh cursor and limit state. Element results are not
cached across traversals, and constructing or copying a view does not traverse
it. Stateful captures follow their normal owned-value or alias semantics; they
are not automatically reset when traversal restarts.

### Access and lifetimes

Sequence traversal exposes Read elements. Pure values follow the existing
snapshot policy; storage-containing elements retain their holders while they
are used. A mapped result has an iteration-local holder that survives all
downstream processing and the current loop body, then receives normal cleanup.
Read loop bindings cannot be written or taken.

Views retain all backing relationships through composition, copies, calls, and
returns. A view borrowing a slice keeps that slice's whole-owner protection.
Access uses existing whole-owner analysis and nonexclusive Write rules. A
callback can have ordinary effects on separately accessible storage; Read input
and an empty typed failure set do not establish callback purity.

Returning a view borrowing caller-owned storage is valid when the caller's
storage outlives its uses. Returning a view borrowing a callee-local array or
vector is invalid. Saving a view of a full-expression temporary owner beyond
that owner's lifetime is also invalid. Retaining callback state must not retain
a borrowed callable view whose target expires after construction.

`collect` copies elements borrowed from source storage into an independent
vector. Independently produced mapped values can be delivered into the result
under normal ownership rules. This internal delivery grants no source-level
Take access to borrowed elements. Existing Vector element admission, allocation,
and capacity contracts apply with the sequence-view admission above. Borrowed
text and slices, bare callable values, native values, and async operations remain
excluded as elements or by-value fields outside a checked sequence view.

A stored view owns its processing state under the defined view-copy rules. Its
upstream and callbacks are checked as parts of that view, not as independent
Vector elements. The vector retains the view's contained borrows and Write
capture targets. Inserting, copying, indexing, removing, replacing, or clearing
view elements uses ordinary value and lifetime rules. Copying or collecting a
view does not copy its borrowed source or extend that source's lifetime.

A Vector containing views can outlive the expression that constructed the
adapters, but it cannot outlive their borrowed backing. Returning such a vector
with a view into callee-local storage is invalid. Borrows into an element's owned
callback state protect the vector's element storage against structural mutation
under the existing carrier rules.

Results of `map` or `fold` must pass ordinary lifetime checks. A borrow into an
iteration-local mapped holder or a replaced accumulator cannot escape its
holder. The builtin provides no lifetime extension to make that result valid.

Consumption also checks callback writes to external storage. Each implicit
callback invocation uses the existing call query and relationship-transfer
rules: bind Read parameters to the current element or accumulator holder,
resolve captures to their stored values and Write targets, and propagate returned
relationships and modifications to externally reachable holders. These checks
are required for predicates returning `bool` as well as transforms and
combiners returning values.

For example, a predicate that stores `row.slice()` in an outer Write-captured
slice slot is invalid when `row` belongs to an iteration-local mapped Vector.
The saved slice would survive the mapped holder. The same assignment can be
valid for an element of a caller-backed collection when its backing remains
live under the ordinary rules; it is not rejected solely because it occurs in a
callback.

Construction establishes descriptor and capture loans without applying delayed
callback-body writes. Consumption applies the callback summaries through the
existing ownership flow and loop joins. Advancing past a rejected element,
replacing an accumulator, ending an iteration, and leaving by `break`, `return`,
or failure all release their holders through the normal lifetime-exit checks.
Unprovable relationships retain the existing conservative analysis behavior.

### Failures and static execution

The initial callbacks have no outward typed failure effect. A function that
handles failures internally can qualify if its completed outward contract is
empty. A fallible callback is diagnosed at the adapter or fold call. Async
callbacks and suspending traversal are outside this scope.

Ordinary argument evaluation can still fail and uses existing propagation and
handling rules. A consuming `for` body can call fallible functions with `?` or
handlers in the usual way. Existing runtime traps and Vector allocation
termination retain their contracts.

The same sequence operations participate in explicit static execution under
separate construction and consumption checks. Construction checks the admitted
descriptor and capture values without invoking element callbacks. It may retain
the identity of an ordinary runtime function as delayed work. A `const fn`
returning such a view can therefore pass its definition check when its own
reachable operations and values satisfy existing admission. This does not make
that view statically consumable or publishable as a retained constant.

Static consumption requires concrete named callbacks admitted as `const fn`.
Native and interpreted runtime consumption support concrete named functions and
concrete closures. Pure Carven sequence code has the same demand, capture,
ownership, and cleanup contracts in both runtime modes. Native interactions
without an executable provider retain the interpreter's existing admission
boundary. This version accepts neither callable-view callbacks nor static closure
callbacks. Ordinary calls through executable named callable views retain their
existing static-execution contract.

At a semantically reachable consuming `for`, `collect`, or `fold` in a `const fn`,
the existing definition-site validator checks the implicit adapter callbacks
and, for `fold`, the combiner. Concrete view types retain the callback identities
needed to perform this check through bindings, returned views, and ordinary
forwarding. An unproven or non-`const fn` target produces `CV-CONST-ADMISSION` at
the consuming definition even if that function is never called. The first
version checks the callbacks of the consumed view shape conservatively; an empty
input or a limit does not require a new demand-dependent capability solver.
Existing semantic reachability still determines which consumption operations
need admission.

Static initializer roots apply the same consumption checks at their existing
admission boundary. The executor retains its normal operation-support checks
for executed paths. Ordinary type, effect, access, and lifetime checking runs
before execution; executor rejection does not replace definition-site checking.
Knowing arguments at runtime does not automatically change their stage.

Static traversal uses the same demand and callback-count rules. It shares the
normal execution budgets and result publication checks. `const for` uses that
same traversal and retains the source until expansion ends. Each bound element
must have a frozen representation; discarded elements need none. The source view
itself need not be frozen. A fully consumed result can be frozen as an admitted
scalar or Vector constant. Retained constants do not admit unconsumed views,
directly or by value inside a collection or product.

## Examples

The proposed module and builtins are used below. The surrounding syntax follows
the existing function, aggregate, ownership, and static-execution rules.

### Reusable data processing

```carven
import std::sequence using { filter, map, take, collect };

struct Reading {
    valid: bool,
    value: i32,
}

const fn is_valid(reading: Reading) -> bool => reading.valid;
const fn reading_value(reading: Reading) -> i32 => reading.value;

const fn valid_values(readings: [Reading]) =>
    readings
        |> filter(is_valid)
        |> map(reading_value);

fn preview(readings: [Reading]) -> Vector<i32> =>
    readings
        |> valid_values()
        |> take(10)
        |> collect();
```

`valid_values` packages a lazy processing rule using an ordinary function.
`preview` materializes at most ten accepted values. Both pipeline calls and
`collect(take(valid_values(readings), 10))` have the same behavior.

### Imperative consumption and repeated traversal

```carven
let readings: [Reading; 3] = [
    {valid: true, value: 10},
    {valid: false, value: 99},
    {valid: true, value: 20},
];

let values = readings.slice() |> valid_values();

var total: i32 = 0;
for value in values {
    total += value;
}

let first: Vector<i32> = values |> take(1) |> collect();
// total is 30; first contains [10]. The second traversal reruns callbacks.
```

The returned view borrows `readings`; it owns no copied Reading collection.
The caller can consume it through ordinary imperative control flow and later
start a separate traversal.

### Views and unit values as elements

```carven
import std::sequence using { map, take, collect };

const fn prefix(limit: i32) => (0..limit) |> take(2);
const fn discard(_: i32) -> void => ();

let windows = (1..4) |> map(prefix) |> collect();
// Inferred Vector of three views: [0], [0, 1], and [0, 1].

var total: i32 = 0;
for window in windows {
    for value in window {
        total += value;
    }
}
// total is 2. Collecting stored the views without traversing them.

let units = (0..3) |> map(discard) |> collect();
// Inferred Vector<void> containing three ordinary unit values.
```

### Static consumption

```carven
import std::sequence using { filter, map, take, fold };

const fn even(value: i32) -> bool => value % 2 == 0;
const fn square(value: i32) -> i32 => value * value;
const fn add(total: i32, value: i32) -> i32 => total + value;

const total: i32 =
    (0..1_000_000)
        |> filter(even)
        |> map(square)
        |> take(4)
        |> fold(0, add);
// 56: delivers 0, 4, 16, 36 and stops without requesting the next source item.
```

This result follows bounded static traversal. It does not require constructing
the million source values or emitting C++ constant expressions.

### Construction and consumption admission

```carven
import std::sequence using map;

fn runtime_only(value: i32) -> i32 => value + 1;

const fn delayed(values: [i32]) => values |> map(runtime_only);
// Construction retains a function identity and invokes no element callback.

const fn invalid_consumer(values: [i32]) -> i32 {
    var total: i32 = 0;
    for value in delayed(values) {
        total += value;
    }
    return total;
}
// Reject invalid_consumer at its definition with CV-CONST-ADMISSION,
// even when unused: traversal would invoke runtime_only.
```

Changing the consumer to an ordinary `fn` permits runtime traversal. Declaring
the callback `const fn` enables consumption in the static stage as well.

## Compiler design

### Checking and publication

Analysis resolves the selected builtin and publishes its concrete source shape,
element and result types, callback identities and captures, operand order,
access, retained backing requirements, and construction versus traversal
contracts. Pipeline elaboration produces the same calls and facts as ordinary
syntax, including through wrapper functions and returned views.

- Concrete view types use canonical type and signature completion. Their finite
  shape records adapter kind, upstream type, callback type, and derived element
  type. Named function and closure types retain callable identities. Bounds,
  counts, and capture contents remain ordinary values. Existing completion,
  cycle, and resource limits apply.
- Type-content queries derive Read passing, copying, and borrowed backing.
  Consumption supplies implicit callback invocations and their holders to the
  existing const-function validator and ownership call queries. Construction
  supplies argument, copy, and capture effects without delayed callback-body
  effects. Ownership proof states stay private; published operations and
  `ReadPassingFacts` provide downstream contracts.
- View-containing Vector elements expose their inline state, owned captures,
  loans, and callable targets through the same referent graph and typed element
  selections. The analysis cannot rely on those elements being loan-free or
  callable-free. Unknown-index selections use existing conservative joins;
  copies, extraction, structural mutation, and cleanup retain the corresponding
  ownership and lifetime checks.
- Before checked source regions are released, callable/type surfaces and closure
  reference ordering retain view types, upstream descriptors, callbacks,
  captures, and required callable references. This includes module results,
  forwarding, and staged bodies. Returning views across ordinary Carven modules
  and crafts is supported.
- Target naming and artifact planning own carrier names, placement, complete
  definitions, and callable dependencies. Private dependencies keep their Carven
  visibility through existing interface closure; native support uses existing
  header-provider metadata. Backend planning and emission consume published
  facts without querying source syntax or repeating ownership analysis.

Sequence operations participate in the existing child traversal, validation,
cloning, effect queries, execution, and publication.

### Semantic execution

The structured executor represents view state and traversal progress using
logical values and storage. Construction and element requests are separate
actions. Callback invocation uses ordinary cleanup, control flow, checks, and
budgets; execution does not depend on generated C++.

Interpreted execution represents concrete closures by callable identity and
capture storage. Value captures follow ordinary owning-copy rules; Write
captures retain coordinates into live execution storage. Closure construction,
copying, and invocation preserve existing object-selection and capture-observation
rules. Ordinary closure calls and sequence callback calls share invocation and
capture binding to the published body. This uses completed concrete types and
contracts without public higher-order constraints or repeated source analysis.

Required static execution, optional runtime folding, and freezing remain
separate boundaries. Adding closure execution does not change static callback
admission. A known final result does not authorize removal of observable callback
work, capture construction, reads, or cleanup.

### Target realization

Realization preserves demand, evaluation order, ownership, retained callback
state, and cleanup. Adapter construction creates no intermediate result
collection; callbacks and capture copies retain their ordinary allocation and
effects. C++ carriers and loop forms are implementation choices governed by
these contracts.

Published source shapes and lifetime facts support these realization choices:

- A view consumed locally by `for`, `collect`, or `fold` has a concrete source
  shape and callback identities. Realization can use those published facts to
  express traversal as a simple loop with conditional filtering, one holder per
  mapped result, and a delivered-element limit. For example, the pipeline
  `map(f) |> filter(p) |> take(n)` can stop after `n` accepted results while
  preserving one `f` call per candidate and the normal cleanup of rejected
  results. Construction arguments still complete before traversal begins.
  Ordinary C++ inlining and optimization can simplify the resulting code.
- A saved, returned, or collected view can use a concrete carrier retaining its
  upstream and callback state, with fresh progress for each traversal. Canonical
  view types and existing artifact planning provide the type and dependency
  information for such a carrier. Bounds and counts remain values. Local
  consumption may eliminate carrier storage where its copies, effects, backing,
  and cleanup permit it; the source contract stays the same in either form.
- An admitted static consumer can use the structured executor to request only
  the needed prefix and freeze the completed scalar or Vector result. This can
  avoid runtime traversal entirely without constructing intermediate result
  collections or expressing the computation through C++ constant evaluation.
  Existing admission, budgets, and freezing boundaries determine when this
  applies.

Implementation choices use measurements of the code being implemented, with
runtime performance, allocation, generated-code size, or compiler cost assessed
where relevant. Comparisons with equivalent imperative code can help assess
the cost of the abstraction on representative workloads. This proposal sets no
benchmark threshold or required optimization. Source behavior determines
correctness.

## Decision record

| ID | Agreed decision and reason |
| --- | --- |
| `DEC-01` | Include `\|>` as left-associated first-argument call composition for readability; retain ordinary call semantics |
| `DEC-02` | Start with five bounded sequence APIs and existing `for`; domain processing uses ordinary functions |
| `DEC-03` | Check concrete sources and callbacks without public parameter packs or higher-order constraints |
| `DEC-04` | Start with ordered, finite, restartable Read views; construction and element processing have separate timing |
| `DEC-05` | Require empty outward callback failure sets in the first version; ordinary consuming loop bodies keep existing failure handling |
| `DEC-06` | Keep source contracts independent of target APIs; ordinary calls and pipelines supply the same checked contracts |
| `DEC-07` | Separate construction from static consumption; the latter uses existing definition-site admission and concrete named const callbacks |
| `DEC-08` | Apply existing call relationship transfer at consumption, including callback writes to external holders and iteration-local lifetime exits |
| `DEC-09` | Give fold an owning accumulator from a copyable Read initial value; retain normal delivery and copying rules without implicit Take |
| `DEC-10` | Publish view types and dependencies through existing type contents, callable surfaces, closure ordering, naming, and artifact planning |
| `DEC-11` | Support concrete named functions and closures in native and interpreted sequence execution; retain separate static callback admission and native-provider boundaries |
| `OPEN-01` | Closed: one named call per step, precedence below ranges, explicit per-step propagation, and an unmarked Read inserted argument |
| `OPEN-02` | Closed: retain upstream descriptors and concrete callback storage by ordinary copy or value delivery; preserve source loans and clean up per-element holders |
| `OPEN-03` | Closed: use `std::sequence`, inferred concrete view types including Vector elements, ordinary copying and borrowing, source-shape diagnostics, and existing publication gates |

## Implementation

Add a `|>` token and the defined expression level to the lexer, parser, and
formatter. Elaborate pipeline forms into ordinary checked calls with source
provenance for diagnostics and tooling. Preserve range parsing, argument access,
postfix propagation, and call evaluation order.

Provide the five identities in `std::sequence`. Add concrete view type facts,
callback checking, descriptor construction, and Read iteration admission. Use
existing owning-copy rules for retained callbacks and fold initialization.

Integrate view operations with the checking and publication mechanisms above.
Extend the structured executor and backend realization for construction,
requests, and consumption. Add concrete closure values, capture storage, and
capture binding to the executor's ordinary invocation path for interpreted
execution, while retaining static admission at its checking boundary. Extend
Vector element admission for checked sequence views and retain their contained
relationships through the existing referent graph, owns edges, call queries, and
lifetime exits. Apply this to element construction, copies, selections,
extraction, replacement, and destruction. Other element restrictions remain in
the existing recursive check. Cross-module composition passes the normal
publication gates.

Implementation includes language, compiler, craft API, and formatter coverage
for the selected contract and corresponding reference documentation.

## Validation

| Contract | Required evidence |
| --- | --- |
| Pipeline grammar | Cover range and logical inputs, grouping, type arguments, named callable bindings, per-step propagation, and whole-expression access markers; reject bare or member callees and marked inserted inputs |
| Pipeline equivalence | Compare ordinary and pipeline calls with argument effects, aliased callable state, nested calls, and temporary lifetimes; include qualified and shadowed names |
| Bounded checking | Diagnose wrong source types, callback arity, access, predicate result, accumulator result, unfinished failure contracts, and unsupported callable forms |
| Demand | Count callback execution for rejected filters, mapped candidates, nested limits, empty sources, `take(0)`, short input, early `break`, reversed ranges, and closed ranges at integer maxima |
| Reuse | Return, copy, forward, and capture views under existing storage rules; consume with both `for` and terminals and repeat traversal with owned and aliased callback state |
| Borrowing and cleanup | Admit caller-backed returns; reject local backing and expiring source or callback holders; check returned and externally written callback borrows and holder cleanup on rejected elements, accumulator replacement, and every loop exit |
| Collection and folding | Verify independent Vector owners, preserved backing for collected views, unit-valued map results, other element restrictions, copied Vector fold initials and empty returns, rejected noncopyable or Take initials, and strict accumulation order |
| View element storage | Collect, insert, copy, select, remove, replace, and clear views, including views held in element products; track owned captures and borrowed sources, reject escapes of local backing, protect borrowed element storage, and release backing loans on cleanup |
| Native and interpreted execution | Compare pure Carven sequence programs using named functions, ordinary closure calls, and closure callbacks; cover owned and Write captures, callable selection before arguments, view copies and Vector storage, repeated traversal, effects, and cleanup; reached native interactions without a provider retain interpreter diagnostics |
| Static admission | Admit construction retaining a runtime function identity; reject unused const consumers invoking it through returned or forwarded views, including fold combiners; admit named const callbacks and reject static closure invocation under the first-version boundary |
| Static execution | Use the same admitted named callbacks and inputs at runtime and in static roots; verify the `56` example, `const for` demand and bound-element freezing, discarded elements, budgets, failure diagnostics, and rejected unconsumed view constants, including views inside collections or products |
| Module publication | Return, forward, copy, and consume views across Carven modules; include concrete closure captures and legal private dependencies; verify native definitions and headers use published facts after source-region release |
| Realization | Verify demand, effects, cleanup, retained state, and absence of intermediate result collections independently of the chosen C++ form |
| Effects | Preserve callback and construction work for known results, repeated traversals, aliased captures, and opaque native calls |

Follow existing test responsibilities: parser and semantic diagnostics in
internal tests, observable behavior and static roots in language tests, provided
API visibility in craft tests, and pipeline formatting in formatter fixtures.
During implementation, build with `./xmakew build` before the relevant test
groups. Explicit native view signatures are not part of this validation scope.

## References

- [Expression grammar](../docs/language/grammar.md#7-expressions)
- [Functions and callable values](../docs/language/functions.md)
- [Access and ownership](../docs/language/ownership.md)
- [Static execution of functions](../docs/language/constants.md#static-execution-of-functions)
- [Semantic execution](../docs/compiler/analysis/evaluation.md)
- [C++ generation](../docs/compiler/backend/README.md)
