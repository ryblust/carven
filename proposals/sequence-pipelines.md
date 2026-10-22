# Sequence views, methods, and pipeline calls

- **Status:** Accepted
- **Implementation:** Not started
- **Scope:** Bounded sequence operations through free functions and methods, reusable lazy Read views, pipeline calls, and shared static execution and realization; the joint delivery includes Generator sources and explicit per-step pipeline input access

## Summary

Provide a small set of sequence operations that compose through methods,
ordinary calls, and a `|>` operator. The first version contains `filter`, `map`,
`take`, `collect`, and `fold`, with shared operation contracts across entries.
Existing `for` loops consume the resulting views.
Functions can package a processing rule by returning a view with an inferred
concrete type.

Views describe demand-driven traversal. `collect` creates an owning result;
`fold` performs ordered accumulation. Use ordinary `for` bodies for imperative
processing at the consumption site.

Source semantics are independent of target APIs. Methods, ordinary nested calls,
and pipeline calls supply the same operation contracts to semantic execution
and C++ generation, using each form's existing argument/receiver binding rules.

Examples distinguish the accepted design from draft extensions. Neither is
implemented yet.

The synchronous-generator proposal selects a draft joint delivery with all five
operations through methods and free functions, saved/returned views, one-shot
sources, explicit-access pipes, and static execution. Its Generator extension
remains Draft; the restartable Read-view design remains
Accepted. Joint-delivery pipeline steps use explicit argument access, including
for temporary inputs. Vector-held views remain accepted but are outside that
joint implementation slice. Completing that slice is partial implementation here.

## Context

Carven has integer `Range<T>` values, fixed arrays, Read slices `[T]`,
owning `Vector<T>`, concrete closures, and inferred function results. `for`
recognizes a fixed set of iterable types. It does not currently provide these
sequence adapters or a public protocol for user-defined iterables.

Read snapshots pure values and borrows storage-containing values. A live slice
protects its backing owner against mutation, replacement, Take, and destruction.
Write is nonexclusive. These rules remain the basis for sequence access and
lifetimes.

The proposed sequence APIs use concrete source and callback checks, following
the bounded call-checking model used by heterogeneous printing and async
composition.

## Design

### First version

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

### Ownership and delivery

The five APIs are builtins in `std::sequence`. Compiler knowledge supplies
concrete view and result types, callback admission, retained backing, and stage
checks. Type methods and free functions bind to shared operation contracts;
pipeline syntax elaborates to ordinary calls. Standard Craft and user
functions package processing rules using these calls; no public iterator or
higher-order generic protocol is required by this delivery. Shared native view
storage and traversal support belong to Runtime Craft, with realization using
the published concrete shape.

The accepted contract includes reusable returned views and view-containing
Vector elements. Assess their implementation costs separately: direct and
returned views need descriptor/capture ownership and cross-module publication;
collection-held views additionally need typed element selections, unknown-index
joins, and contained callable/loan tracking. Select implementation slices from
those examples and existing ownership support. Record supported storage positions
for each slice and require its complete lifetime contract before admission.
The design's collection-held view semantics remain accepted; completion of a
smaller slice is partial implementation of this proposal.

### Generator sources and joint delivery

The Draft [synchronous-generator proposal](generators.md) owns the Generator
type, reserved `yield`, explicit advancement, producer ownership and failure,
early destruction, and static execution. It adds a separate one-shot source
mode to these five APIs; it does not change restartable views into generators.
The contracts below continue to describe restartable Read views unless that
proposal explicitly specifies the one-shot extension.

| Operation | Accepted restartable path | Draft one-shot extension |
| --- | --- | --- |
| `filter`, `map`, `take` | Read source; copyable descriptor; fresh traversal progress | Take source; move-only owning adapter; progress advances once |
| `collect` | Copy borrowed elements or deliver independent mapped values | Transfer independent delivered items into admitted Vector storage |
| `fold` | Read source and copyable Read initial | Take source; same Read initial and combiner rules |
| Failure | Empty outward callback failures and infallible traversal | Retain producer failures through adapters; terminal calls and next handle them explicitly |
| `for` | Read-view traversal with ordinary bindings | Explicit Take of an infallible one-shot source, including temporaries; fallible iteration uses next |
| Pipeline | First-argument insertion into ordinary Read calls | General per-step input markers: omitted means Read, `&` means Write, and `&&` means Take; markers remain mandatory for temporaries |
| Methods | Existing Read `self` binding; reusable view remains available | Existing `&&self` binding; named and temporary receivers are consumed alike |

The proposed joint implementation includes all five APIs as methods and free
functions with shared semantics, pipeline syntax,
local/saved/returned views, concrete runtime callback closures, cross-module
publication, const execution with named const callbacks, and one-shot generator
composition. Vector elements containing reusable views remain part of this
accepted design but are deferred from that implementation slice. Their contained
callable/loan tracking and unknown-index handling are not prerequisites for
local/returned composition. Generator and one-shot-adapter Vector elements are
also outside the slice and require their own move-only storage contract.

The accepted decisions and validation requirements below retain their scope
except for the Read-only input restriction, extended by `DEC-12` with explicit
per-step markers, and extended by `DEC-14` with real sequence methods. `DEC-13`
is withdrawn; temporary omission and grouped-input access changes are outside
the delivery. The joint delivery does not complete
or remove collection-held views or establish acceptance of the generator draft.

### Source design

#### Sequence methods and shared operation contracts

The joint delivery provides the five operations as real methods on Read slices,
integer Range values, restartable views, Generator, and one-shot adapters. Arrays
and owning Vectors enter through their existing slice conversion or `.slice()`;
this delivery adds no separate array/Vector method family. The methods and the
existing free functions are public entries of `std::sequence`.

| Operation | Restartable-source receiver | One-shot-source receiver | Other arguments |
| --- | --- | --- | --- |
| `.filter(predicate)` | `self` (Read) | `&&self` (Take) | Read predicate |
| `.map(transform)` | `self` (Read) | `&&self` (Take) | Read transform |
| `.take(count)` | `self` (Read) | `&&self` (Take) | Read usize count |
| `.collect()` | `self` (Read) | `&&self` (Take) | None |
| `.fold(initial, combine)` | `self` (Read) | `&&self` (Take) | Existing copyable Read initial and Read combine |

Generator and one-shot adapters additionally provide `.next()` with `&self`,
sharing the free `next(&source)` advancement contract owned by the generator
proposal. It requires a mutable available receiver and does not consume it.

These are existing instance-receiver modes. A named Generator and a temporary
Generator use the same `&&self` binding rule; a consuming method makes a named
receiver unavailable. No call-site `&&` is required on an implicit receiver and
no rule distinguishes named and temporary receivers. Partial-owner Take,
non-owning Read receivers passed to consuming methods, and live-loan conflicts
remain invalid under ordinary instance-operation checking.

```carven
let source = parsed_values(input);
let result = source
    .filter(positive)
    .map(square)
    .take(3)
    .collect()?;
// source is consumed by filter; it cannot be reused.
```

This changes no explicit argument rule. Every explicit Take argument still
requires `&&`, including temporary results; Write arguments still require `&`.
The free-function and pipe versions therefore keep the explicit source marker
at every consuming call:

```carven
let result = parsed_values(input)
    |> &&filter(positive)
    |> &&map(square)
    |> &&take(3)
    |> &&collect()?;
```

The two examples are alternative inputs to the same checked operation contracts,
not separate processing algorithms. Method and free-function identities bind to
one operation kind with the same source mode, callback contract, concrete
adapter/result type, captures, and failure set. Share admission, demand, lifetime,
cleanup, static execution, interpreter operations, and runtime realization. Do
not introduce an extra adapter or generator frame just to expose a method entry.

In particular, both entries preserve these contracts:

- Restartable views copy descriptors/callback state under the existing construction
  rules and retain source backing. Composition never borrows an expiring receiver
  descriptor merely because the source expression uses a dot.
- One-shot adapters own the receiver after Take and retain all upstream backing.
  Each request, rejected item, mapped holder, prefix limit, failure, and early
  destruction follows the same rules as the free-function path.
- Lazy methods do not advance the producer or invoke element callbacks during
  construction. The specified `take(0)` close and final-item close behavior is
  identical on both paths.
- Callback outward failures remain empty in this delivery. Producer failures
  remain latent through lazy methods and are handled on advancement or terminals
  with ordinary `?` or catches. A method does not erase a source failure set.
- Const capability, known producer/callback proof, execution budgets and freezing
  checks are identical. Method syntax does not make an unknown or non-const
  target statically consumable.

Methods preserve normal receiver-before-explicit-arguments evaluation. Free calls
and expanded pipes preserve normal callee-first and left-to-right arguments.
For the fixed provided operation identities, matched source/callback expressions
have the same observable construction and consumption order. Do not rewrite a
method through a mutable or shadowed free-function binding to force textual
identity between different call forms.

Method discovery uses the canonical receiver type or concrete adapter shape.
It does not search for a same-named local/free function, depend on importing that
free-function name, or turn arbitrary functions into extension methods. A local
binding named map can shadow an imported free function but cannot replace the
sequence method. Type aliases and inferred cross-module adapter results retain
method availability through their canonical published type facts. This adds no
public iterable protocol or extra capability on an otherwise unknown generic
receiver. Method values remain excluded under existing instance-operation rules.

Prefer method chains in sequence-focused teaching examples. Use ordinary calls
and pipes to compose module/user functions, select imported aliases, or make
individual source-access boundaries explicit. Mixed expressions retain the same
normal syntax, for example `source.filter(positive).collect()? |> summarize()`
when summarize takes Read. A pipe's right side remains a named call; coexistence
does not add member-call rewriting there.

#### Pipeline calls

Unmarked pipeline call composition retains Read input:

```carven
source |> operation(a, b)
// Same call as operation(source, a, b).

source |> first(a) |> second(b)
// Same calls as second(first(source, a), b).
```

Pipelines associate to the left. Each step inserts its left operand as the first
explicit argument of the named call on the right. The selected callable follows
ordinary lookup, arity, contextual typing, access, stage, and failure rules.
The operator applies to ordinary user functions and local callable names as well
as sequence APIs.

The joint delivery adds an optional input access marker to each pipeline step:

```carven
source |> &operation(a, b)
// Same call as operation(&source, a, b).

source |> &&operation(a, b)
// Same call as operation(&&source, a, b).
```

The step marker specifies the inserted first argument's access. It is syntax
inside the named pipeline-call production, not an access expression applied to
the callee or result. Omission means Read, `&` means Write, and `&&` means Take.
All explicit arguments on the right keep their own access markers.

A Take parameter always requires written `&&`, including when its argument is
a literal, construction, function result, or preceding adapter result. A Write
parameter always requires written `&`. Apply the same exact marker matching to
ordinary calls and pipelines, at every argument position. Existing value/place
and lifetime facts still validate a marked operand; they do not make the marker
optional. No implicit copying, last-use inference, or temporary-specific
omission is introduced.

This rule concerns explicit call arguments and the pipe's inserted first
argument. Existing implicit instance-receiver binding through `self`, `&self`,
or `&&self` remains unchanged; a pipe step is not a method receiver.

Each step states its input access independently. There is no inherited mode:

```carven
source |> &&first(a) |> &&second(b)
// second(&&first(&&source, a), b)

source |> &&first(a) |> inspect()
// inspect(first(&&source, a)); inspect takes Read.

source |> &&fold(initial, combine)
// fold(&&source, initial, combine); initial and combine remain Read.
```

This rule is independent of Generator identity and applies to all named calls.
A missing or incorrect marker gives the corresponding ordinary-call diagnostic,
even when an unmarked temporary could physically be moved. Read-view APIs do
not require Take merely because the operator supports it.

The right side is one explicit call to a name or qualified name, with the
optional input access marker, existing type arguments, and optional trailing
`?`. Bare callables, member-call rewriting, arbitrary callee expressions, partial
application, and argument placeholders are outside the initial grammar.

`|>` binds less tightly than ranges and ordinary binary operators:

```ebnf
expression = access-expression | pipeline-expression;
pipeline-expression = range-expression, { "|>", pipeline-call };
pipeline-call = [ access-marker ], named-callee, [ type-arguments ],
                call-operation, [ propagation-operation ];
named-callee = [ "::" ], IDENTIFIER,
               { "::", identifier-shaped-name };
```

Use grouping for postfix or binary operations on an entire pipeline result,
such as `(values |> collect()).slice()` or `(values |> fold(0, add)) + 1`.
A trailing `?` propagates from that step's ordinary call:

```carven
0..10 |> take(3)          // take(0..10, 3)
value |> convert()?      // convert(value)?
values |> inspect(&log)  // inspect(values, &log)
source |> &&collect()?   // collect(&&source)?
```

Grouped access expressions on the left are not an alternate pipeline input
annotation: use `source |> &&operation()` or `source |> &operation()` instead.
The withdrawn draft's transparent-group argument-marker rule is not introduced.
Parenthesized ordinary inner calls remain valid when their own access and
lifetime checks pass.

A leading access marker outside a pipeline step still applies to the whole
expression under the existing precedence:

```carven
use(&value |> produce())
// use(&produce(value))

use(&&value |> produce())
// use(&&produce(value))
```

The inserted input in these examples remains Read. A step marker is required to
change it. No existing access expression is reinterpreted because a pipe occurs
inside its operand.

Every expression is evaluated once. Callee selection, argument evaluation,
full-expression temporary lifetimes, propagation, and cleanup follow the nested
ordinary calls. A pipe does not introduce sequential local declarations or
statement boundaries. A later argument failure does not restore an earlier
taken owner; already acquired arguments receive ordinary cleanup. Complete-owner,
writable-place, access-conflict, and native-boundary checks remain unchanged.

Lazy execution belongs to the selected API contract. Pipe syntax does not
implicitly start a producer, observe advancement failures, or establish const
capability. Static admission and interpretation use the same expanded calls.

#### Ownership notation and alternatives

The selected rule optimizes predictability: the declared parameter access tells
the caller which marker to write. Users do not classify temporary expressions
before deciding whether a Take marker is optional. Fewer source characters alone
do not establish lower user complexity.

| Alternative | Consequence | Selection |
| --- | --- | --- |
| Omit Take for temporary results | Users distinguish existing storage from independent results; calls and tools need a new eligibility rule | Withdrawn; retain exact marker matching |
| Carry Take across a complete pipe | Access becomes ambient and must interact with intermediate Read and Write calls | Do not introduce |
| Adapt a borrowed mutable generator | Source lifetime, aliasing, pause/resume, and destruction differ from owning adapters | Evaluate only for an independently needed borrowed-source API |
| Declare real sequence instance methods | Reuse existing Read/Take receivers and bind both public forms to one operation contract | Included in this delivery; explicit pipes and free functions remain available |
| Package a recurring domain pipeline in an ordinary function | Callers cross one explicit ownership boundary; the helper keeps explicit markers internally | Use when the processing rule is independently reusable |

For a recurring infallible numeric processing rule, ordinary code can expose:

```carven
fn first_positive_squares(&&source: Generator<i32>, limit: usize) -> Vector<i32> =>
    source
        |> &&filter(positive)
        |> &&map(square)
        |> &&take(limit)
        |> &&collect();

// In a caller with its own source:
let result = source |> &&first_positive_squares(3);
```

The names above illustrate a domain rule. This requires no new function or
ownership mechanism and does not waive internal argument markers. Keep one-off
pipelines explicit; do not add a standard abstraction merely to hide `&&`.
Formatter alignment and parameter hints can improve readability without changing
source semantics, but are not new requirements of this delivery.

The selected methods use ordinary receiver contracts, including consuming named
owners without an explicit argument marker. They are genuine type operations,
not syntax aliases for arbitrary free functions. Keep that ownership behavior
explicit in method documentation; it is the existing instance-operation model,
not temporary-specific omission. The shared method contract is defined above.

#### Builtin API identity

The APIs belong to `std::sequence` and use existing import selection rules:

```carven
import std::sequence using map;
import std::sequence using { filter, map, take, collect, fold }
```

Single-name selections may omit braces; multiple-name selections require them.
Builtin recognition follows the resolved declaration identity, including import
aliases. A user declaration named `map` keeps its own meaning. Builtins use
direct free or method calls, including expanded pipeline calls; passing a
builtin itself as a callable value is outside the first version. Method lookup
uses its receiver's canonical type and needs no import of the corresponding
free-function binding. User wrapper functions remain ordinary callables.

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

#### Constructing and composing views

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

#### Traversal and demand

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

#### Access and lifetimes

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

#### Failures and static execution

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

### Examples

The proposed module and builtins are used below. The surrounding syntax follows
the existing function, aggregate, ownership, and static-execution rules. The
one-shot composition examples also use the general per-step input access rule.

#### Explicit input access

```carven
let result = parsed_values(input)
    |> &&filter(positive)
    |> &&map(square)
    |> &&take(3)
    |> &&collect()?;
```

Every adapter and terminal takes its upstream explicitly, even though the first
input and all intermediate results are temporary. Omitting a required marker
produces the same access diagnostic as in the ordinary-call form.

The same syntax applies to an ordinary Write-input function:

```carven
fn increment(&value: i32) -> i32 {
    value += 1;
    return value;
}

var count = 0;
let result = count |> &increment();
// increment(&count): both count and result are 1.
```

Read, Write, and Take steps can be mixed according to the selected callables'
contracts. No step changes the access of the next one. Read-view sequence APIs
retain their existing unmarked calls.

#### Reusable data processing

```carven
import std::sequence using { filter, map, take, collect }

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

#### Imperative consumption and repeated traversal

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

#### Views and unit values as elements

```carven
import std::sequence using { map, take, collect }

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

#### Static consumption

```carven
import std::sequence using { filter, map, take, fold }

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

The method entry uses the same Read sources, named const callbacks, demand, and
freezing contract:

```carven
const method_total: i32 = (0..1_000_000)
    .filter(even)
    .map(square)
    .take(4)
    .fold(0, add);
// Also 56; no free-operation import is needed for method lookup.
```

This result follows bounded static traversal. It does not require constructing
the million source values or emitting C++ constant expressions.

#### Construction and consumption admission

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

### Compiler design

#### Checking and publication

Analysis resolves the selected builtin and publishes its concrete source shape,
element and result types, callback identities and captures, operand order,
access, retained backing requirements, and construction versus traversal
contracts. Free and method entries normalize to one operation kind and checked
source/callback contract; method receiver binding retains its existing access.
Pipeline elaboration produces ordinary calls with the same facts, including
through wrapper functions and returned views. Entry spelling must not select a
different carrier type or duplicate traversal, effect, or cleanup analysis.

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
  forwarding, and staged bodies. Publish method availability and canonical
  operation identities for inferred adapter types, independent of free-function
  import bindings. Returning views across ordinary Carven modules and crafts is
  supported.
- Target naming and artifact planning own carrier names, placement, complete
  definitions, and callable dependencies. Private dependencies keep their Carven
  visibility through existing interface closure; native support uses existing
  header-provider metadata. Backend planning and emission consume published
  facts without querying source syntax or repeating ownership analysis.

Sequence operations participate in the existing child traversal, validation,
cloning, effect queries, execution, and publication.

#### Semantic execution

The structured executor represents view state and traversal progress using
logical values and storage. Construction and element requests are separate
actions. Callback invocation uses ordinary cleanup, control flow, checks, and
budgets; execution does not depend on generated C++.

Interpreted closure callbacks require shared executor support for concrete
closures. Invoke them through the ordinary-call path with the view's retained
capture storage. Value captures use owning-copy rules; Write captures retain
live execution coordinates. Preserve callee selection before argument evaluation.
Sequence traversal owns demand, per-element holders, and relationship transfer.

Keep required static execution, optional runtime folding, and freezing separate.
Static callbacks remain concrete named `const fn` functions. Preserve observable
callback work, capture construction, reads, and cleanup when using known results.

#### Target realization

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

Use equivalent imperative code as a baseline for realization experiments. Measure
runtime, allocation, generated-code size, and compiler cost where affected.
Source behavior determines correctness; this contract requires no performance
threshold or particular optimization.

## Decision record

| ID | Decision and reason |
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
| `DEC-12` | Joint-delivery choice: optional per-step `&`/`&&` marks the inserted first argument for all named calls; omission stays Read and Take markers remain required even for temporaries; extends only OPEN-01's Read-only restriction |
| `DEC-13` | Withdrawn: omit Take for independent temporary results and admit grouped input access; exact marker matching is retained to avoid user-side value-category decisions |
| `DEC-14` | Joint-delivery choice: include genuine methods for the five operations alongside free functions and explicit-access pipes; reuse Read/Take receivers and one shared operation contract, without temporary omission or extension-method rewriting |
| `OPEN-01` | Closed original rule: one named call per step, precedence below ranges, explicit per-step propagation, and unmarked Read input; DEC-12 adds explicit Write/Take steps without changing unmarked semantics |
| `OPEN-02` | Closed: retain upstream descriptors and concrete callback storage by ordinary copy or value delivery; preserve source loans and clean up per-element holders |
| `OPEN-03` | Closed: use `std::sequence`, inferred concrete view types including Vector elements, ordinary copying and borrowing, source-shape diagnostics, and existing publication gates |

## Implementation

The Draft joint delivery described above starts with local, saved, and returned
views plus Generator sources and all five operations through methods, free
functions, and explicit-access pipes. It defers the accepted
Vector-held-view slice. The steps below describe the full accepted Read-view
contract; finish and validate only the selected storage positions before calling
that slice implemented, and retain Partial status for this proposal as a whole.

Use direct traversal, a returned reusable view, and a Vector of views to identify
shared work and storage-specific work before selecting the first implementation
slice. Interpreted closure execution supplies concrete closure construction and
calls; named-function traversal can be implemented independently.

Add a `|>` token and the defined expression level to the lexer, parser, and
formatter. Elaborate pipeline forms into ordinary checked calls with source
provenance for diagnostics and tooling. Preserve range parsing, argument access,
postfix propagation, and call evaluation order.

For the joint delivery, parse one optional input access marker in each named
pipeline call and retain its span. Expand a marked step into an ordinary
`ASTAccessExpr` on the inserted first argument; an unmarked step adds no marker.
Use existing concrete, generic, builtin, and native argument-access checks.
Do not classify a temporary to omit its marker, move the callee, change an
explicit later argument, or propagate an input mode into the next step.
Reject duplicate markers and grouped left-input access annotations. Preserve
callee selection, cleanup, and full-expression lifetimes without introducing
sequential evaluation temporaries. No general call-binder relaxation is needed.

Provide the five identities in `std::sequence`. Add concrete view type facts,
callback checking, descriptor construction, and Read iteration admission. Use
existing owning-copy rules for retained callbacks and fold initialization.

Add the five real method entries on the selected source families. Bind Read
receivers for restartable sources and Take receivers for one-shot sources using
existing instance-call rules; do not change explicit argument checks. Normalize
both entries to shared semantic operations before ownership, effect, stage, and
realization consumers. Add canonical method discovery for unspellable adapter
types and preserve it across publication. No general extension-method mechanism,
free-function fallback for member lookup, or second traversal implementation is
required.

Integrate view operations with the checking and publication mechanisms above.
Extend the structured executor and backend realization for construction,
requests, and consumption. Integrate shared interpreted closure execution and
retain static admission at its checking boundary. Extend
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
| Pipeline grammar | Cover ranges, logical inputs, grouping, type arguments, named callable bindings, optional per-step access, propagation and outer access precedence; reject duplicate step markers, bare/member callees, placeholders and grouped left-input access annotations |
| Pipeline equivalence | Compare ordinary and pipeline calls with argument effects, aliased callable state, nested calls, temporary lifetimes, Write places, Take availability, and failures after source acquisition; include qualified and shadowed names |
| Pipeline access | Require exact Read/Write/Take matching for named and temporary inputs; cover missing and incorrect markers, mixed-mode chains, explicit later arguments, unchanged callee identity and no inherited input mode |
| Method binding | All five methods on slices, ranges, views, Generator and one-shot adapters; consume named/temporary Take receivers, retain Read sources, reject partial/non-owning receivers and borrow conflicts, and keep explicit argument markers mandatory |
| Shared entries | Matched method/free/pipe programs share adapter types, demand, capture work, failures, zero/final take cleanup, and results; verify ordinary receiver/argument evaluation and no duplicate runtime work |
| Method discovery | Inferred/cross-module adapter methods and type aliases work without importing free-function names; local shadowing cannot replace a method; reject arbitrary extension-method fallback and method-value extraction |
| Bounded checking | Diagnose wrong source types, callback arity, access, predicate result, accumulator result, unfinished failure contracts, and unsupported callable forms |
| Demand | Count callback execution for rejected filters, mapped candidates, nested limits, empty sources, `take(0)`, short input, early `break`, reversed ranges, and closed ranges at integer maxima |
| Reuse | Return, copy, forward, and capture views under existing storage rules; consume with both `for` and terminals and repeat traversal with owned and aliased callback state |
| Borrowing and cleanup | Admit caller-backed returns; reject local backing and expiring source or callback holders; check returned and externally written callback borrows and holder cleanup on rejected elements, accumulator replacement, and every loop exit |
| Collection and folding | Verify independent Vector owners, preserved backing for collected views, unit-valued map results, other element restrictions, copied Vector fold initials and empty returns, rejected noncopyable or Take initials, and strict accumulation order |
| View element storage | Collect, insert, copy, select, remove, replace, and clear views, including views held in element products; track owned captures and borrowed sources, reject escapes of local backing, protect borrowed element storage, and release backing loans on cleanup |
| Native and interpreted execution | Compare pure Carven sequence programs using named functions, ordinary closure calls, and closure callbacks; cover owned and Write captures, callable selection before arguments, view copies and Vector storage, repeated traversal, effects, and cleanup; reached native interactions without a provider retain interpreter diagnostics |
| Static admission | Admit construction retaining a runtime function identity; reject unused const consumers invoking it through returned or forwarded views, including fold combiners; admit named const callbacks and reject static closure invocation under the first-version boundary |
| Static execution | Use the same admitted named callbacks and inputs through methods/free calls/pipes at runtime and in static roots; verify the `56` example, `const for` demand and bound-element freezing, discarded elements, budgets, failure diagnostics, and rejected unconsumed view constants, including views inside collections or products |
| Module publication | Return, forward, copy, and consume views across Carven modules; include concrete closure captures and legal private dependencies; verify native definitions and headers use published facts after source-region release |
| Realization | Verify demand, effects, cleanup, retained state, and absence of intermediate result collections independently of the chosen C++ form |
| Effects | Preserve callback and construction work for known results, repeated traversals, aliased captures, and opaque native calls |

Follow existing test responsibilities: parser and semantic diagnostics in
internal tests, observable behavior and static roots in language tests, provided
API visibility in craft tests, and pipeline formatting in formatter fixtures.
During implementation, build with `./xmakew build` before the relevant test
groups. Explicit native view signatures are not part of this validation scope.
