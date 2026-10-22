# Synchronous generators

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** `std::sequence::Generator`, synchronous production with `yield`, ownership, advancement failures, early destruction, static execution, and shared sequence methods, free functions, and explicit-access pipes

## Summary

Add cold, single-pass synchronous generators that retain ordinary source control
flow between delivered elements. Select a joint delivery with the existing five
sequence operations through methods, free functions, and explicit-access pipes
sharing semantics while preserving the restartable Read-view contracts.
The rules below are proposed contracts, not implemented language behavior.

## Context

Carven currently supports cold async operations and fixed iterable families.
Neither supplies a synchronous producer that preserves its execution position
between consumer requests. The accepted sequence proposal supplies restartable
Read views over slices and integer ranges, but no user-written suspended producer.

A concrete consumer is nested-array event parsing: deliver begin, number, and
end events from recursive source control flow, and stop when a consumer has enough
numbers. A cursor expresses this with explicit parser phases; a callback retains
natural control flow but propagates a stop result through recursive calls. A
generator combines natural producer control flow with a stored pull interface.
Tokenization across input blocks remains a separate responsibility in all forms.

Generated programs and Runtime Craft currently have a C++20 baseline. Selecting
generator semantics does not select the C++23 `std::generator` implementation.

## Design

### Public surface and first delivery

Public type names and free operations are imported through `std::sequence`:

```carven
import std::sequence using { Generator, SequenceStep, next, filter, map, take, collect, fold }
```

`Generator` is not a default binding. Its name follows ordinary lookup, aliases,
and shadowing; recognition uses the canonical selected type, not its spelling.
`SequenceStep<T>` is a standard-source result enum with `Item(T)` and `End` cases.
`next`, like the sequence adapters, is a direct-call builtin following ordinary
lookup. It accepts a mutable one-shot source, not arbitrary user iterators.
Its method entry is `.next()` with a Write receiver. Method-only calls discover
operations from their receiver type and do not require imports of free-operation
names. Generator type annotations still require its ordinary import.

| Surface | Contract |
| --- | --- |
| `Generator<T>` | Single-pass owner delivering `T`, with no outward advancement failures |
| `Generator<T, throw E>` | Same owner with the closed, copyable nominal advancement failure set `E` |
| `yield expression;` | Deliver one `T` and suspend the directly enclosing producer |
| `next(&source)` | Advance a generator or one-shot adapter; return `SequenceStep<T>` and possibly fail with `E` |
| `source.next()` | Same advancement operation through an existing `&self` receiver contract |
| `for item in &&source` | Consume an infallible one-shot source; expose each delivered item under ordinary Read binding rules |
| Five sequence operations, as methods and free functions | Shared source-order/demand contract; Read-view or one-shot ownership follows the source family and existing receiver/argument rules |

`Generator<T, throw ()>` and `Generator<T>` have one canonical type. Different
producers with equal element and solved failure types have the same Generator
type. Producer identity, retained backing, and static-execution capability remain
semantic facts; a type annotation does not prove them.

The first delivery includes named source functions, recursion, associated
functions without instance receivers, owned element delivery, explicit advancement,
consuming infallible loops, the five sequence methods and `.next()` sharing free
operation semantics, native and interpreted execution, and `const` execution.
Generator lambdas, yield-containing source producers with instance receivers,
frame-borrowed elements, async element production, native generator ABI exposure,
and generator-containing Vector elements are outside this delivery.

### Grammar and function classification

Reserve `yield` lexically, like `return` and `await`:

```ebnf
yield-statement = "yield", expression, ";";
```

The spelling `yield_once` stays an identifier. Keyword-shaped qualified and
member names retain the existing grammar. Bare declarations or imports cannot
bind a local name spelled `yield`.

Parse every function body without consulting its return type. A `yield` belonging
to that function, including one on an unreachable path, classifies it as a
generator body. Nested function bodies do not classify the outer body. The body
requires an explicit return annotation selecting the canonical `Generator<T, E>`.
Type aliases are admitted; there is no generator function modifier.

```carven
fn numbers(limit: i32) -> Generator<i32> {
    for value in 0..limit {
        yield value;
    }
}

fn forward(limit: i32) -> Generator<i32> {
    return numbers(limit);
}
```

`forward` is an ordinary function returning an owner. A body with no own `yield`
does not acquire generator semantics from its return type. An empty traversal
can use `numbers(0)`; no element need actually be yielded at execution.

Within a generator body, fallthrough and `return;` finish the sequence. A value
return is invalid. `yield ();` produces one unit element for `Generator<void>`;
bare `yield;` is invalid. Yield cannot cross a value-expression control boundary,
such as an enclosing value-form `if`, `match`, `try`, or `const` block. Existing
return and loop-transfer boundary rules supply the corresponding precedent.

### Cold construction and advancement

Calling a generator body evaluates runtime arguments once, left to right, and
establishes captures under their declared access contracts. It does not execute
any statement in the body. Static parameters specialize normally and need no
runtime capture. Argument evaluation and capture initialization retain their
ordinary effects, failure handling, and partial-construction cleanup.

Read pure-value parameters snapshot their values; retained storage parameters
keep their holders and backing alive. Write inputs retain their actual writable
places. Take inputs become owned captures. None of these rules snapshots borrowed
collection contents or grants exclusive Write access.

An advancement runs from initial entry or the preceding suspension until one
element is delivered, the body ends, or a failure escapes. It never schedules
another task. A yield evaluates its operand and establishes independent element
storage before suspending; construction failure produces no item.

| Logical state | `next` behavior | Destruction |
| --- | --- | --- |
| Cold | Start body | Release captures without executing body |
| Suspended | Continue after previous yield | Destroy initialized locals and captures without continuation |
| Running | Reentry is invalid | Destruction of this same active owner is invalid |
| Ended | Return `End` | No live producer body remains |
| Failed and delivered | Return `End` | Failure has already left through its normal result carrier |

Native reentry that cannot be rejected statically terminates deterministically
as a runtime contract violation. It is not a new member of `E`. Advancing or
destroying a different nested generator remains valid.

The terminal transition releases producer locals and captures once. The owner
can retain an empty terminal representation. A failure is delivered once; a
later request does not rethrow it or resume the failed body.

### Ownership, storage, and yielded values

A generator is move-only, has no default, and has no implicit clone or restart.
Initializing or returning from an existing named owner requires `&&`. A fresh
factory result uses ordinary value delivery. Owner transfer moves the right to
advance and destroy; it must preserve addresses of live frame storage.
This does not waive an explicit Take argument's marker: passing a factory result
as an explicit argument still requires `&&`, just like passing an existing owner.
Implicit method receivers use existing `&&self` binding, not that explicit
argument syntax.

The first delivery admits generator owners in local owning bindings, parameters,
results, record and tuple slots, fixed arrays, and one-shot adapter state.
Contained ownership and backing propagate through those positions. Taking a
field still requires taking its complete owner under ordinary rules. Ordinary
closures cannot acquire a generator through an owning-copy capture; adding a
Take capture mechanism is not a prerequisite for this delivery. Vector storage
of generators or one-shot adapters requires separate move-only element support.

An owner retains all captured backing until its active state is destroyed or
reaches a terminal state. Transfers, returned wrappers, and adapters preserve
these relationships. Dropping an intermediate carrier must not release backing
still held by its successor. Borrowing caller storage can be valid; returning
a producer borrowing callee-local storage is invalid.

First-delivery yielded types are independently backed Carven data: scalar and
unit values, String, admitted owning Vectors, and arrays, tuples, records, or
enums recursively containing such values. This element admission excludes `str`,
slices, pointers, callable state, native objects, Operation, Generator, and
sequence descriptors, including nested occurrences. They require separate
backing, execution-owner, or native validity contracts. The restriction applies
to yielded elements, not to otherwise admitted borrowed producer inputs.

`yield value;` uses ordinary owning-copy delivery when `value` selects an existing
owner. `yield &&value;` transfers a complete owner and makes it unavailable until
reinitialized. Fresh results can be delivered directly. The backend must not add
default construction or assignment requirements just to represent an item slot.

Yielded items cannot borrow producer-local storage. A consumer can retain an
item independently of advancing or destroying the producer. Values produced by
mapping have the same independence requirement on the one-shot path. Ordinary
restartable views keep their existing, wider checked borrowed-result contracts.

Loan checking must cover delayed producer writes as well as captured inputs.
Construction establishes captures; advancement applies producer effects through
all possible suspension paths and loop joins. Writes into an external holder
must not leave it borrowing a producer local that later dies. Cancellation,
owner transfer, and native calls do not create lifetime extensions.

Loans established inside a producer can also survive suspension: for example,
a local slice of caller storage may remain live after a scalar item is yielded.
Advancement publishes the corresponding suspended-state loans back onto the
source owner. Interleaved caller writes must respect the conservative union of
possible live suspension states. Captured-input loans alone are not a sufficient
model. Terminal transitions can release active relationships where the existing
flow proves termination; otherwise lifetime checking remains conservative.

### Failure contracts

Advancement failures belong in `Generator<T, throw E>`. The generator body checks
all propagated and directly thrown failures against `E`; omission in
`Generator<T>` means the empty set, not inference of an unannounced public set.
Failure-set parameters use the existing normalized union and Copy rules.
For a published yield-containing declaration, the explicit Generator failure
argument supplies the producer's public failure contract; it is not a missing
trailing function failure declaration. Ordinary non-yielding wrappers retain
the existing published function-failure rules.

```carven
fn parsed_values(input: [u8]) -> Generator<i32, throw ParseError> {
    var parser = make_parser(input);
    while {
        match parser.next()? {
            .Item(value) => { yield value; },
            .End => { return; },
        }
    }
}
```

The parser operations above illustrate a producer contract, not existing APIs.
Creating `parsed_values(input)` does not observe ParseError. Advancing it does:

```carven
var values = parsed_values(input);
while {
    match next(&values)? {
        .Item(value) => { process(value); },
        .End => { break; },
    }
}
```

A yield-containing function has no independently failing body-construction
phase: its trailing function `throw` clause must be absent or explicitly empty.
Ordinary wrapper functions may validate eagerly and have a construction-time
failure contract in addition to the advancement failures of their returned
Generator. Argument and capture-expression failures still occur in the caller.
Allocation termination remains under the existing allocation contract.

On an escaping advancement failure, destroy the active producer state before
delivering the failure. Validate failure-payload backing against that cleanup;
a payload must not refer to destroyed frame storage. Previously delivered items
and completed effects remain. Catching the failure does not restart the producer.

Manual `next`, `collect`, and `fold`, through either method or free entries, use
ordinary call propagation and catches.
Initial `for` support requires an empty source failure set. Reject a fallible
source even when a surrounding function declares the same failures: the loop
does not introduce implicit propagation. No special interpretation of
`for item in source?` is introduced. Fallible per-request loop syntax is a
separate deferred question; the explicit `next(&source)?` loop covers this use.

### Explicit advancement and consuming loops

`next(&source)` and `source.next()` require a mutable available place. They borrow
the source only for the call and return an owning `SequenceStep<T>`. A caller may
pause between calls or alternate independent sources. The returned item follows ordinary enum
pattern Read rules; matching it does not add a Take pattern mechanism.

`for item in &&source` transfers an infallible generator or one-shot adapter into
loop-owned storage. A temporary source also requires explicit transfer, as in
`for item in &&numbers(4)`. An unmarked one-shot source is rejected rather than
silently copied, mutated through Read, or consumed. The source expression is
evaluated once. Read element bindings, recursive tuple targets, and their
iteration-local lifetime rules remain the existing ones; Take element bindings
and mutable element traversal are outside this delivery.

At each backedge, release the previous item before asking for another. `continue`
asks for the next item. `break`, return, failure, and ordinary scope exit destroy
the loop-owned source after releasing its current item. They do not leave the
source available for a later loop. A caller needing partial resumption uses a
stored `var` owner and manual `next` instead.

Breaking out of a manual next loop does not itself destroy its surrounding
named owner. Use a nested lexical scope when that caller needs resources released
at the stop point; otherwise cleanup occurs when the owner is replaced or its
scope ends. No implicit close is attached to an ordinary while-loop break.

### Early destruction and completion

Dropping a suspended producer never resumes it to run statements after `yield`.
Only cleanup of already initialized storage executes, in ordinary scope order;
unreached initializations and unobserved failures do not occur. Cleanup does not
yield, await, emit new declared failures, or initiate generator-body work.
Native destructors keep their existing boundary obligations.

If application logic requires a trailer, validation, flush, or another fallible
final action, it must consume to normal completion or invoke an explicit owning
API for that action. A statement after the last emitted item is not a destructor.
Taking a prefix is intentionally not validation of the rest of an input.

Adapters own their upstream one-shot source. Destroying an adapter recursively
destroys upstream state without further requests. A `take` adapter releases
upstream state when it delivers its final permitted item; `take(0)` releases it
when adapter construction completes and never starts it. Normal exhaustion and
failure also release upstream and callback state once. Retained-backing analysis
may conservatively keep a named adapter's loans until its lexical lifetime ends;
terminal cleanup alone does not promise that the checker proves it terminal.

For example, if item four would throw, consuming `take(source, 3)` never observes
that failure. If code immediately after item three has an observable effect, it
does not run merely because the limit was reached. This also applies when three
was the producer's last possible item: proving exhaustion would require another
request, which the prefix consumer must not make.

### Composition with the five sequence operations

Keep two source modes under the same public operation names:

| Source mode | Argument access | Lazy result | Reuse |
| --- | --- | --- | --- |
| Slice, integer Range, existing restartable view | Read | Restartable Read view | Fresh traversal progress each time |
| Generator or one-shot adapter | Take | Move-only one-shot adapter owning its upstream | One traversal only |

Concrete builtin checking selects the parameter contract from the source family.
Every explicit Take argument requires `&&`, including a fresh factory or adapter
result, as in every ordinary declared call. Methods bind their implicit receiver
with the existing receiver contract. The source family and the selected
argument/receiver access must agree; this is bounded builtin checking, not a
general overload or iterator protocol. The adapter's concrete type has no source
spelling. It retains its element type, failure set, producer provenance, callbacks,
ownership, and stage facts across inferred returns and ordinary forwarding.
There is no implicit conversion from a one-shot adapter or restartable view to
the canonical Generator type. A function packaging adapters uses its inferred
concrete result type. An explicitly Generator-returning producer can instead
forward elements through its own yield statements.

| Operation on a one-shot source | Construction | Request/consumption behavior | Failure contract |
| --- | --- | --- | --- |
| `filter(&&source, predicate)` | Own source and concrete callback | Request until one predicate accepts or source ends | Retain source `E` |
| `map(&&source, transform)` | Own source and concrete callback | Transform each requested item once | Retain source `E` |
| `take(&&source, count)` | Own source and count; zero closes immediately | Deliver at most count; never probe past limit | Retain source `E` |
| `collect(&&source)` | Take source before traversal | Deliver independent item owners into Vector in order | Call fails with source `E` |
| `fold(&&source, initial, combine)` | Take source; establish independent accumulator from Read initial | Replace accumulator in source order | Call fails with source `E` |

All callbacks keep the original first-delivery empty outward failure contract.
`filter` and `map` have Read element parameters, and `combine` reads accumulator
and element. Named functions and concrete closures are admitted under the same
completion and lifetime checks as restartable views. Captures are constructed
once and reused through advancement. No callback work runs during lazy adapter
construction. `take(0)` is the specified exception for upstream destruction,
not for advancement or callback invocation.

Source `E` propagates through lazy adapters without being observed at construction.
An empty limit does not erase `E` from the type or waive the ordinary failure
handling required by a terminal call. Fallible callbacks and fallible restartable
views are separate extensions; domain failure can currently be written in a
generator body before yielding its mapped value.

`filter` and `take` deliver the accepted independent item without a source-level
copy requirement. A rejected item is destroyed before the next request. `map`
keeps its input alive through the transform, then releases it; its output cannot
borrow that input. `collect` transfers delivered owners into final element
storage when admitted by Vector. `fold` keeps its existing copyable Read initial
and Read accumulator contract; this does not add Take accumulators.

If upstream advancement fails, or a consuming loop body fails, destroy current
items, active accumulator/results, callbacks, and upstream owners in the order
required by their scopes. A failed collect returns no partial Vector. Completed
external effects remain. Native reentry and invalidation are checked at the
same source/runtime boundaries as direct advancement.

The joint delivery uses explicit input access on every pipeline step. The marker
has the same meaning for generators and ordinary user functions' explicit
arguments; existing implicit instance-receiver rules are unchanged:

```carven
let result = parsed_values(input)
    |> &&filter(positive)
    |> &&map(square)
    |> &&take(3)
    |> &&collect()?;

// Equivalent ordinary call:
let result = collect(&&take(&&map(&&filter(
    &&parsed_values(input), positive), square), 3))?;
```

The paired result declarations above are alternative spellings, not successive
executions. A named source uses the same per-step access rule:

```carven
let source = parsed_values(input);
let result = source
    |> &&filter(positive)
    |> &&map(square)
    |> &&take(3)
    |> &&collect()?;
```

This expands to `collect(&&take(&&map(&&filter(&&source, positive), square), 3))?`.
At every step, no marker means Read, `&` means Write, and `&&` means Take of the
inserted first argument. The marker does not transfer the callee or call result
and does not persist to later steps. Later explicit arguments keep their own
markers. Missing Take markers are rejected even on independent temporary results.

Ordinary outer access-expression precedence remains unchanged:
`use(&&source |> operation())` still means `use(&&operation(source))`, not
`use(operation(&&source))`. Grouped left-side access is not an alternate pipe
annotation. No temporary omission or transparent-group argument-marker rule is
introduced. Expansion preserves nested-call callee/argument order, temporary
lifetimes, failure handling, and cleanup without statement boundaries. The
sequence proposal owns this general syntax and expansion; ordinary argument
checking remains responsible for exact access matching.

Nested producers use ordinary consuming loops and explicit re-yielding. No
`yield from`, ambient emitter, or implicit delegation is introduced:

```carven
fn nested_values(limit: i32) -> Generator<i32> {
    let child = numbers(limit);
    for value in &&child {
        yield value;
    }
}
```

This forwarding copies existing values as ordinary Read delivery requires.
An implicit delegation facility or Take binding needs its own ownership and
stack-cost contract. Recursive generators are supported, but this design makes
no bounded-native-stack guarantee for deeply nested synchronous advancement.

### Method entries and shared semantics

Include the five sequence methods on Generator and one-shot adapters in this
delivery. Each has a Take receiver (`&&self`); the corresponding restartable
slice/Range/view methods use Read receivers (`self`). The sequence proposal owns
[method lookup and the shared operation contracts](sequence-pipelines.md).
These are genuine methods on known types, not a rewrite through same-named free
functions or an extension-method mechanism.

```carven
let source = parsed_values(input);
let result = source
    .filter(positive)
    .map(square)
    .take(3)
    .collect()?;
// source was consumed by filter and cannot be used again.
```

The existing receiver rule consumes both named and temporary owners. A caller
need not write `(&&source)` before a consuming method. The same declaration's
`&&self` contract supplies receiver access for named and temporary owners alike;
this is not an optional-marker rule based on temporary status. A method on a non-owning
borrow, partial owner, or owner with conflicting loans is rejected under the
same existing checks as other Take-receiver methods.

All explicit arguments retain exact marker matching. A method's Take receiver
does not make explicit Take parameters optional, infer Write access, permit
copies of Generator, or relax the owned-item/backing restrictions. Generator-
producing source functions with their own yield and an instance receiver remain
outside the first delivery; this does not exclude ordinary builtin sequence
methods that construct adapters or consume a producer.

Add `source.next()` on a mutable Generator or one-shot adapter with `&self`.
It is the method entry of `next(&source)`, with the same SequenceStep result,
advancement failure set, terminal behavior, reentry checks, and short-lived Write
access. Pausing between calls leaves the owner available; a later consuming
method can take its remaining sequence under ordinary checks.

Bind both method and free-function entries to the same normalized operation
contract, using the same source, callbacks, element type, failure set, adapter
identity, and capture/provenance facts. Share demand, holder lifetimes, delayed
producer/callback effects, failure delivery, early destruction, static admission,
executor operations, and native realization. A method adds no separate carrier,
intermediate collection, generator frame, or callback copy solely for its entry.
Methods retain receiver-before-explicit-argument evaluation; ordinary calls and
expanded pipes retain their existing evaluation. Fixed builtin entry identities
must not be rebound through local free-function shadowing.

Thus `.take(0)` closes without advancement exactly like `take(&&source, 0)`;
`.take(n)` does not probe the next item after its limit; `.collect()` and `.fold()`
observe `E` at the consuming call and clean partial results identically. Lazy
methods preserve `E` without observing it. Method syntax does not add fallible
callbacks or weaken const producer/callback proof or result freezing.

Methods are available from canonical receiver type facts even when Generator or
an adapter is inferred from another module and no same-named free function is
imported locally. Use ordinary imports when naming Generator, SequenceStep, or
calling a free operation. A method cannot be extracted as a callable value;
arbitrary types do not gain these methods from an imported function name.

Use method chains as the usual sequence-focused form. Keep the free functions
and explicit-access pipe for generic function composition and code that wants
written access at each inserted argument. They are alternate entries to one
semantic capability, not independently evolving APIs.

### Static execution and result freezing

`const fn` on a generator body states that its suspended body can execute in the
static stage. It still runs normally at runtime when consumed there. Evaluate
the same prefix, effects, failures, and cleanup in both stages.

Cold construction, advancement, and publication are separate checks. Static
construction may retain a non-const producer identity without executing it,
provided the capture construction is admitted. It does not make that producer
statically advanceable. An ordinary non-generator wrapper still requires its
normal `const fn` call capability.

At a reachable `next`, loop, collect, or fold in a const function, the definition
validator must prove that every possible producer and invoked callback is
statically admitted. The erased Generator type alone is insufficient. Preserve
producer provenance through storage, returns, composition, and joins; an opaque
or unproven producer is rejected rather than deferred to a successful execution
sample. This may conservatively reject a helper with an opaque Generator
parameter. No public const-qualified Generator type is introduced in this scope.

Static callbacks retain the sequence proposal's named `const fn` boundary;
runtime native and interpreted consumption support its concrete closures.
All nested generators and callbacks share the static root's step, memory, and
depth budgets. Unbounded production is permitted at runtime; static traversal
must finish within the existing budgets. `take` does not bypass admission checks
or prove callback purity.

```carven
const fn static_numbers(limit: i32) -> Generator<i32> {
    for value in 0..limit {
        yield value;
    }
}

const fn total() -> i32 {
    var result = 0;
    let source = static_numbers(4);
    for value in &&source {
        result += value;
    }
    return result;
}

const answer = total(); // 6
```

Active and terminal generator owners and one-shot adapters are execution-local
values, not frozen constants, including inside products or collections. Consume
and destroy them within the static root; publish only admitted completed values.
`const pending = static_numbers(4);` is invalid. A completed collected Vector
can freeze under ordinary element rules. `const for` consumes an infallible
one-shot source under the same rules and additionally freezes each bound item
required by expansion. Fallible static traversal can use explicit `next(...)?`
with normal static failure handling.
Execution failure or a budget diagnostic must also release the executor's active
frames and host resources without resuming producer bodies for finalization.

### Async interaction

`yield_once()` remains a cold scheduling operation in `std::async`. Its execution
queues the current task at the FIFO tail and does not deliver a sequence item or
implicitly accept cancellation. Generator yield has no scheduling behavior.

An async function can produce a synchronous generator as its final result:
calling it constructs `Operation<Generator<T, throw E>, throw C>`, and await
observes construction/acquisition failures `C`. Later synchronous advancement
observes `E`. The async result-admission rules must explicitly admit an
independently backed generator; a frame borrowing the completed async activation
is rejected. The result does not gain this admission merely because it is an
owning handle. First-delivery admission requires independently owned captures
and complete frame-backing facts for every possible producer.

An async function cannot contain an own `yield`; it returns one Generator as its
single successful completion. A synchronous generator body cannot await, start
lexical children, or suspend on the async driver. An async consumer may own a
synchronous generator across an await, retaining ordinary backing and scope
cleanup obligations. Async-generator types and per-element awaiting are deferred.

### Compiler and runtime responsibilities

Standard Craft owns module declarations and the ordinary `SequenceStep` result
enum. Builtins own Generator identity, yield and next semantics, the concrete
one-shot adapter identities, access selection, and their failure contracts.
Runtime Craft owns native frame, advancement, destruction, and adapter support.

Analysis publishes producer classification, element and failure contracts,
capture layout and retained relationships, suspension live storage, item
independence, callback identities, stage capability, and cleanup destinations.
Suspension is a control-flow boundary for availability and lifetime analysis;
cross-yield state and subsequent writes must participate in the fixed point.
Module publication retains every fact needed after source regions are released.

The executor represents logical captures, instruction/continuation positions,
scope cleanup, terminal states, and independently delivered items. It cannot
depend on compiling or running generated C++ for static execution. Runtime
interpretation and static traversal share these logical operations and their
normal provider/admission boundaries.

Native realization may use C++20 coroutines or an equivalent explicit frame.
Canonical producer-erased Generator values need a stable owning representation
and producer-specific advancement/destruction behavior. Allocation and dispatch
are evaluated against handwritten C++ with that same stored-owner contract;
direct sequence loops have their own loop baseline. Do not promise allocation
elision or fuse away observable capture, address, failure, or cleanup effects.
Reusable views must not be lowered through allocated generator frames solely to
share an implementation. Local specialization is optional and preserves behavior
when disabled. No C++23 library dependency is introduced.

### Selected joint delivery

| Capability | This delivery |
| --- | --- |
| `yield`, Generator type, next and SequenceStep | Include synchronous owned-item production, recursive source functions, manual advancement and deterministic close |
| `filter`, `map`, `take` | Include methods and free functions for restartable views and owning one-shot adapters, sharing empty outward callback failure contracts |
| `collect`, `fold` | Include both entries and source modes; share Vector admission and Read accumulator rules |
| Existing `for` | Include restartable views and explicit Take of every infallible one-shot source, including temporaries |
| `const` and interpretation | Include named const callbacks, producer admission, prefix demand, failure/cleanup, and result freezing checks |
| Pipeline `\|>` | General per-step Read/Write/Take first-argument access; require explicit Take even for temporaries and preserve outer-marker precedence |
| Storage and publication | Include local/saved/returned views and generators, concrete runtime closures, cross-module results, and contained backing checks |
| Views stored in Vector | Preserve the accepted design, but defer this implementation slice and its contained-capture/unknown-index analysis |
| Generator stored in Vector | Defer move-only element admission and structural mutation support |

This selects all five operations from the accepted sequence proposal, not every
accepted storage position. Completing this slice is partial implementation of
that proposal. No source-defined iterable protocol, higher-order generic
machinery, flat_map, or general callable capability mechanism is a prerequisite.

## Decision record

| ID | Status | Decision or draft selection |
| --- | --- | --- |
| GEN-01 | Agreed | Put Generator in `std::sequence`, with explicit import |
| GEN-02 | Agreed | Use a Generator return type and body-local reserved `yield`; add no function modifier |
| GEN-03 | Draft | Cold, move-only, single-pass owners; independent owned elements and explicit Take consumption |
| GEN-04 | Draft | Advancement failures live in Generator's failure set; manual next and terminals use ordinary explicit propagation |
| GEN-05 | Draft | Destruction never resumes a body; terminal transitions release active state once |
| GEN-06 | Draft | Support static execution with producer/callback admission; never freeze a generator owner |
| GEN-07 | Draft | Share five operation names across restartable Read views and owning one-shot adapters |
| GEN-08 | Agreed | Keep exact explicit Take for explicit call arguments, including temporaries; reuse existing implicit receiver binding, general per-step pipe markers, and empty outward callback failures |
| GEN-09 | Draft | Permit independently backed async completion with a synchronous Generator; keep async element production separate |
| GEN-10 | Draft | Select all five sequence operations, local/returned storage, and const; defer collection-held view and generator storage |
| GEN-11 | Agreed | Include all five sequence methods and next alongside free functions and explicit-access pipes, with shared semantic operations and existing receiver rules; activates and resolves GEN-DEFER-06 |

## Deferred work

### GEN-DEFER-01 — Borrowed and additional yielded values

- **Reason deferred:** Independently backed items avoid loans into a suspended
  producer, advance-invalidated elements, and unchecked native representations.
- **Reactivation condition:** A consumer needs zero-copy borrowed items, native
  values, callable/sequence descriptors, or nested execution owners as elements.

Define item lifetime, advancement blocking, owner movement, cleanup, native
validity, and const capability at the boundary consuming each added family.

### GEN-DEFER-02 — Fallible callbacks and loop propagation syntax

- **Reason deferred:** Producer failures are expressible with explicit next or
  terminal calls; existing Read views and callbacks retain their accepted rules.
- **Reactivation condition:** Concrete adapters need fallible callbacks or a
  common fallible per-element loop makes the explicit next form burdensome.

Define source/callback failure unions, restartable failure behavior, and visible
per-request handling. Do not reinterpret a source expression's ordinary `?` as
an ambiguous mixture of construction and traversal propagation.

### GEN-DEFER-03 — Owning element bindings and delegation

- **Reason deferred:** Current loop bindings already express Read consumption;
  pipelines use the same exact per-argument access rules as ordinary calls.
- **Reactivation condition:** Source uses require noncopyable element extraction
  or delegation with a distinct ownership or stack-cost contract.

An owning element binding must distinguish generated independent items from
elements borrowed from a restartable source. Delegation must define item
transfer, early close, failure delivery, and native stack requirements.

### GEN-DEFER-04 — Async generation and additional producer forms

- **Reason deferred:** The selected consumer needs synchronous pull production.
- **Reactivation condition:** A consumer needs await during element production,
  generator lambdas, instance-receiver producers, or a native generator boundary.

Each extension needs capture/receiver lifetime, failure, cancellation or reentry,
static capability, and destruction contracts for its actual consumer. They are
not implied by returning a synchronous Generator from an async function.

### GEN-DEFER-05 — Collection-held owners and deep delegation

- **Reason deferred:** Local/returned composition supplies the selected consumers;
  move-only Vector storage and bounded-stack delegation add independent contracts.
- **Reactivation condition:** A consumer stores runtime-sized producer sets or
  needs deep recursive forwarding with a demonstrated stack constraint.

Use the owning collections proposal for move-only element operations. Preserve
the sequence proposal's accepted collection-held reusable views as a separate
implementation slice. Do not claim bounded recursive stack use without an
explicit advancement/delegation design and evidence.

## Implementation

Implement the joint delivery in these dependency-ordered steps. This document
selects a design and validation scope; it does not claim implementation progress.

1. Add yield token/AST/formatting and body classification, canonical Generator
   contracts, SequenceStep and next declarations, and definition diagnostics.
2. Publish suspension and captured-backing facts; validate item independence,
   ownership transfer, body/failure separation, cross-module results, and terminal
   cleanup. Add the bounded async-completion admission explicitly.
3. Implement logical cold/advance/yield/end/failure/destroy operations in the
   shared executor and C++20 Runtime Craft realization. Add consuming infallible
   loops and explicit next before adapters.
4. Implement the five sequence operations and next with method/free entries
   normalized to shared contracts, source/receiver admission, demand, and cleanup,
   while retaining distinct restartable/one-shot ownership. Publish method lookup
   for canonical and inferred adapter types. Include saved and returned views,
   named functions and concrete runtime closure callbacks; do not require Vector
   elements holding views for this step.
5. Implement optional per-step input access through ordinary marked-argument
   pipe expansion, static producer/callback admission, static traversal, freezing,
   module serialization and naming, and required diagnostics. Maintain the language
   references only with implemented contracts.

## Validation

| Contract | Required evidence |
| --- | --- |
| Parsing and classification | Keyword/name positions, nested yields, alias return types, unreachable yields, invalid result types, value returns, empty production, and value-expression transfer boundaries |
| Cold behavior | Construction evaluates arguments/captures once but no producer statements; destroying an unstarted owner only releases captures |
| Ownership | Named owners require Take; copies/defaults are rejected; transferred frames preserve backing and addresses; unavailable owners cannot advance |
| Suspension | Initialized locals survive yield; subsequent writes and reinitialization preserve ownership; moved yield operands cannot be reused without reinitialization |
| Element independence | Retain items after advance/drop; reject all excluded nested types, local backing escapes, and delayed writes that leak frame borrows |
| Failure | Construction-wrapper versus advancement failures, failure-set parameters, explicit propagation/catches, one-time delivery, next-after-failure End, and payload cleanup safety |
| Destruction | Cold/suspended/ended/failed owners, nested sources, partial initialization, early break/return/body failure, current-item cleanup, and exact-once native resource release |
| Demand | take(0), filters rejecting items, one transform per candidate, no request/effect/error after the limit, immediate upstream close at limit, and unbounded sources with finite prefixes |
| Composition | All five operations on both modes, empty inputs, independent collected owners, Read initial copies, rejected callback failures, and iterator/adapter storage transfers |
| Method entries | All five methods plus next use existing Read/Take/Write receiver binding; reject reuse of consumed named owners, non-owning/partial Take receivers and loan conflicts; explicit Take arguments still require markers even for temporaries |
| Shared semantics | Matched method/free/pipe entries produce the same concrete adapter types, effects, requested prefix, errors, exact-once cleanup and const results; preserve each form's ordinary receiver/callee/argument evaluation |
| Method publication | Inferred cross-module adapters and aliases retain methods without free-name imports; local free-function shadowing cannot change methods; no extension-method fallback or method values |
| Pipelines | All five operations with explicit Take at each consuming step, ordinary user Write/Take calls, mixed-mode chains, reject missing markers even on temporary results, preserve outer precedence and nested-call evaluation/failure/cleanup |
| Reuse | Repeated Read-view traversals restart; one-shot owners cannot silently repeat; manual next alternates independent producers and resumes after a pause |
| Async separation | Existing yield_once behavior; reject own yields in async bodies and await in synchronous producers; accept only independently backed async generator results |
| Const | Definition-site producer/callback proof through aliases/returns/joins, reject opaque targets, same prefix effects/failures/cleanup, budgets, const-for expansion, and rejection of retained generator owners |
| Publication and execution | Cross-module saved/returned producers and views; native/interpreted/static agreement within admitted providers and callbacks; facts survive source release |
| Realization | C++20-only consumers; inspect and compare allocations, dispatch, stack depth, capture copies, and final item construction against matching handwritten contracts |

Use normal internal/parser/semantic, language, crafts, interop, and formatter
test responsibilities. Build with `./xmakew build` before relevant test groups.
Documentation-only proposal editing does not require compiler builds or tests.
