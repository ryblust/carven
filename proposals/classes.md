# Classes and dynamic abstraction

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** Transparent data, ordinary classes, class forms, receivers, and dynamic abstraction
- **Depends on:** None for ordinary classes; dynamic generic operations depend
  on [Generics](generics.md), and cross-boundary calls depend on explicit
  [C++ interoperation](../docs/semantics.md#c-interoperation) contracts

## Summary

This proposal separates transparent records, encapsulated value classes, and
runtime-erased values. Ordinary class construction and receiver access, the
static/dynamic boundary, and the `class(form)` declaration axis are accepted
and unimplemented.

Operation visibility and consuming decomposition remain open for ordinary
classes (`OPEN-01`, `OPEN-02`). Dynamic ownership, conformance, and type-use forms
follow in `OPEN-03` through `OPEN-05`; generic dynamic operations are a separate
candidate in `OPEN-06`. Deferred extensions retain their own activation conditions.

## Context

Carven needs source forms for published record fields, representation protected
by operations, and values whose concrete type is unknown to a caller. Static
generics can express direct calls for known types; heterogeneous collections,
callbacks, services, and plugins can also require runtime erasure.

Ordinary argument access already distinguishes Read, Write, and Take. Receivers
reuse these ownership and availability rules. Class semantics must define
observable construction, access, and lifetime before selecting C++ mechanisms.
All examples below describe proposed behavior, with unsettled spellings marked.

## Goals and non-goals

The initial scope is transparent records, encapsulated value classes, explicit
receiver access, and a declaration axis for compiler-defined class forms.
Dynamic forms require their own ownership and conformance contracts.

Inheritance, protected representation, implicit allocation or managed lifetime,
automatic concept/dynamic conversion, user-defined class transformations,
runtime reflection, and stable plugin ABI are outside the initial scope.
Transparent records carry no POD, trivial-layout, standard-layout, or C ABI promise.

## Design

### Transparent `struct` and ordinary `class`

**Maturity:** Accepted semantics.

A `struct` is a transparent nominal product whose published fields follow the
declaration's audience. Construction, field access, and destruction use aggregate
rules. It has no inherent methods, private state, inheritance, or class-form
transformation. An external `impl Concept for Struct` does not add members.

An ordinary `class Name { ... }` combines hidden representation with instance
and associated operations that protect its invariants. It is a static,
non-inheriting value type. Declaring one implies no heap allocation, reference
identity, virtual dispatch, or nullable state.

Both forms compose copyability, destruction, ownership, and resource behavior
recursively from their fields. The initial class model has no custom copy/move
hooks or separate lifetime system. Field semantics determine C++ representation
requirements, including any nontrivial resource behavior.

### Ordinary class encapsulation and construction

**Maturity:** Accepted semantics.

Class fields participate in lookup only within the class body. Other declarations
in the module have no privileged access. External callers use operations and
observable type properties.

Inside the body, `ClassName { field: value }` constructs the representation.
External aggregate construction is invalid, and no public all-fields constructor
is generated. Receiverless functions provide named factories:

```carven
class Money {
    cents: i64,

    fn from_cents(cents: i64) -> Money {
        return Money { cents: cents };
    }
}

let price = Money::from_cents(100);
```

Associated construction is an ordinary call with the usual constraints, failure,
evaluation, and diagnostics. There is no reserved `init` or `constructor` family.
`Money()` is invalid because postfix call syntax requires a callable value.

```text
T { ... }          direct value construction
value(...)         callable invocation
Type::name(...)    associated operation selection and invocation
```

Lowering may use C++ constructors, factories, or equivalent forms that preserve
these source rules.

### Receiver access

**Maturity:** Accepted Read/Write/Take and dot-call behavior; operation visibility
and consuming decomposition remain open.

An instance operation declares a receiver slot:

```carven
fn value(self) -> i64;             // Read receiver
fn increment(&self);               // Write receiver
fn into_value(&&self) -> i64;      // Take receiver
```

Dot calls supply the receiver without repeating its access marker:

```carven
counter.value();
counter.increment();
let value = counter.into_value();
```

After member resolution, Read grants read-only access; Write requires an
updatable place; Take requires a complete owner or permitted temporary and makes
the original binding unavailable. Static and dynamic operations use the declared
contract; access is not inferred from the body.

Consuming operations support builders and resource owners:

```carven
class RequestBuilder {
    fn build(&&self) -> Request;
}

var builder = RequestBuilder::default();
let request = builder.build();
builder.set_url(url); // error: builder was Taken
```

`OPEN-02` must define controlled whole-representation decomposition for access
to stored fields. An ordinary partial member Take cannot leave a usable class
object. A consumed binding has no callable moved-from state.

Marker omission applies only to the dot-call receiver. Other arguments, free
functions, and associated functions retain explicit matching markers:

```carven
fn merge(&self, &&other: Counter);
left.merge(&&right);

fn reset(&value: Counter);
reset(&counter);

Counter::combine(&left, &&right);
```

### Static and dynamic abstraction

**Maturity:** Accepted boundary.

| Property | `concept` | Dynamic interface value |
| --- | --- | --- |
| Subject | Generic type parameter | Runtime value |
| Call | Resolved at definition site; direct after instantiation | Erased dispatch |
| Ordinary type | No | Yes, through an explicit holding form |
| Runtime state | None | Data handle and dispatch information |
| Ownership and allocation | No additional behavior | Defined by the holding form |
| Composition | Static conjunction | Separate dynamic-contract rules |

Concept evidence and dynamic conformance are separate facts. A concept in value
position does not become an interface type, and failed static resolution does
not fall back to dynamic dispatch. Any future conversion between the models must
make its representation and cost explicit.

### Minimum dynamic contract

**Maturity:** Accepted constraints; concrete forms remain open.

A dynamic contract has nominal identity from its canonical module and declaration.
It lists the operations available through an erased value. Compile-time
conformance checks every required signature; unrelated concrete members remain
inaccessible through that value.

Calls evaluate operands exactly once and use the contract's failure signature.
Allocation, ownership, mutability, nullability, and lifetime must be explicit.
After these rules are fixed, lowering can choose operation tables, proxy storage,
or another equivalent representation.

### The `class(form)` declaration axis

**Maturity:** Accepted declaration axis; the first concrete form and erased
type-use spelling remain open.

A bare class declaration selects an ordinary class. A parenthesized,
compiler-defined form selects a special declaration contract:

```carven
class Money {
    cents: i64,
}

class(interface) Printer {
    fn print(text: str) -> void;
}
```

`interface` is illustrative and has not been accepted as a builtin form.
`class(form)` is declaration syntax; it leaves declaration identity independent
of borrowing, ownership, and erasure at type-use sites.

Initial form names are compiler-defined. `class()` is invalid. Parentheses for
forms and `<...>` for generic arguments are separate. The axis does not select
`dyn`; this possible use-site syntax is also illustrative:

```carven
fn render(printer: dyn Printer, text: str) {
    printer.print(text);
}
```

`OPEN-03` through `OPEN-05` decide dynamic holding, conformance, and type use.

### Dynamic generic operations

**Maturity:** Exploration. `CLS-07` preserves this design option;
`OPEN-06` decides whether any such source capability is admitted.

Generic dynamic operations could use representations other than C++ virtual
members. This example is a candidate, not accepted syntax:

```carven
class(interface) Visitor {
    fn visit<T: Node>(value: T) -> void;
}
```

A closed compilation could generate specialized thunks for observed argument
and receiver types, per-argument dispatch slots, a finite matrix for multiple
erased axes, or proxy handles and operation tables. Static receivers could use
direct calls while erased receivers use indirect calls. These options require
neither global registries nor unused instances.

Any selected design must define signatures, evaluation, failures, lifetime, and
diagnostics independently of representation. Every generic dynamic call must
enter the analyzed graph. C++ entry points need finite, explicit
`import(cpp)`/`export(cpp)` contracts; incidental generated names cannot add
untracked calls. Open-world registration and ABI remain deferred.

## Decision record

| ID | Decision and reason |
| --- | --- |
| `CLS-01` | Structs publish fields; classes encapsulate representation and behavior. Both compose value and resource semantics from fields. |
| `CLS-02` | Class bodies construct representation directly; associated functions provide factories using ordinary call rules. Type names are not callable. |
| `CLS-03` | Receivers declare Read/Write/Take; dot calls supply the receiver without repeating the marker. |
| `CLS-04` | Static concepts and dynamic values have separate evidence and representation; conversion must be explicit. |
| `CLS-05` | Dynamic contracts are nominal and explicitly define allocation, ownership, nullability, and lifetime. |
| `CLS-06` | `class(form)` selects a declaration contract independently of type-use ownership and erasure. |
| `CLS-07` | Generic dynamic operations remain a source-design option even though C++ virtual members cannot be templates. Feasibility alone does not accept the feature. |

## Open decisions

**Next discussion:** `OPEN-01`

### OPEN-01 — What is the final operation visibility and helper surface?

- **Status:** Active
- **Depends on:** `CLS-01`, `CLS-02`, `CLS-03`
- **Question:** Distinguish public instance operations, associated operations,
  and class-private helpers.
- **Constraints:** Fields stay class-private, module peers have no privilege,
  and receiver access is declared explicitly.
- **Options:** Use invariant-preserving classes to identify the minimum forms.
- **Closure condition:** Compare construction, query, mutation, and private-helper
  APIs and select the smallest adequate syntax.

### OPEN-02 — How does a Take receiver decompose its whole representation?

- **Status:** Active
- **Depends on:** `CLS-03`
- **Question:** Give consuming builders and resource owners controlled access
  to stored fields.
- **Constraints:** `self` becomes unavailable; no usable partially moved object
  remains; every field has deterministic ownership and destruction.
- **Options:** Evaluate whole-representation patterns using `build`, `finish`,
  and `into_*` operations.
- **Closure condition:** Select a pattern and define availability, destruction,
  and diagnostics for every field path.

### OPEN-03 — Which dynamic ownership and value forms exist?

- **Status:** Active
- **Depends on:** `CLS-04`, `CLS-05`
- **Question:** Define storage and lifetime for dynamic interface values.
- **Constraints:** Allocation and sharing are explicit; forms compose with
  Read/Write/Take and imply no nullable state.
- **Options:** Borrowed and owned forms have candidate uses. Shared, nullable,
  mutable, and inline-storage forms require further evidence.
- **Closure condition:** Use callbacks, heterogeneous containers, and service
  APIs to select the minimum holding forms and lifetime rules.

### OPEN-04 — What is the first concrete class form and conformance syntax?

- **Status:** Blocked
- **Depends on:** `CLS-06`, `OPEN-03`
- **Activation condition:** Dynamic ownership and lifetime are defined.
- **Question:** Select a concrete form and how a type conforms to it.
- **Constraints:** Initial form names are compiler-defined; conformance is
  distinct from concept evidence and implies no allocation or inheritance.
- **Options:** `interface` is the leading form candidate; conformance spelling
  is unresolved.
- **Closure condition:** Define declaration, conformance, missing/extra/signature
  diagnostics, and one static-to-erased construction.

### OPEN-05 — How does a type use request an erased dynamic value?

- **Status:** Blocked
- **Depends on:** `OPEN-03`, `OPEN-04`
- **Activation condition:** Holding forms and the first dynamic contract are defined.
- **Question:** Make erasure, borrowing, and ownership visible at the use site.
- **Constraints:** Concepts remain separate from ordinary types; declaration
  form and use-site ownership remain distinct.
- **Options:** `dyn Contract`, ownership-derived forms, or another explicit form
  validated with real APIs.
- **Closure condition:** Select an unambiguous model for borrowed and owned
  parameters, results, locals, fields, and container elements.

### OPEN-06 — Which generic dynamic-operation surface enters the first slice?

- **Status:** Blocked
- **Depends on:** `CLS-07`, `OPEN-04`, `OPEN-05`
- **Activation condition:** Dynamic contracts and erased type use are defined.
- **Question:** Decide whether generic dynamic operations meet a concrete need.
- **Constraints:** Bodies are checked at definition site; every erased call is
  in the analyzed graph; behavior is independent of dispatch representation.
- **Options:** Exclude generic dynamic operations; support static generic
  arguments with an erased receiver; or add multiple erased axes if a use case
  requires them.
- **Closure condition:** Define a complete generic contract operation,
  conformance, call, finite instance set, and diagnostic matrix.

## Deferred work

Each direction below retains its own reactivation condition.

| ID | Direction and reason deferred | Dependencies | Reactivation condition |
| --- | --- | --- | --- |
| `DEFER-01` | Interface composition needs identity and conflict rules. | `OPEN-03` through `OPEN-05` | An API needs multiple contracts on one erased value and defines nominal identity and operation conflicts. |
| `DEFER-02` | Default dynamic implementations need settled conformance and override rules. | `OPEN-04` | Repeated conformance behavior can be shared with explicit representation, failures, and dispatch cost. |
| `DEFER-03` | Downcasting adds runtime identity, failure, ownership, and lifetime behavior beyond dispatch. | `OPEN-03` through `OPEN-05` | An API needs concrete-type recovery and defines its checked failure path. |
| `DEFER-04` | Static/dynamic bridging needs an explicit conversion and cost model. | Implemented concepts and dynamic contracts | Repeated APIs need both forms of one contract. |
| `DEFER-05` | Open-world generic dispatch and plugins need erased ABI, registration, and instance-extension rules. | `OPEN-06` and an artifact/plugin proposal | Supported separate distribution needs external generic instances. |
| `DEFER-06` | User-defined class forms need an established extension model. | Multiple successful compiler-defined forms | Repeated contracts require a bounded declarative form. |
| `DEFER-07` | Metaclass generation needs bounded transformation and execution/diagnostic rules. | Implemented forms and a static-meta proposal | A transformation exceeds ordinary forms and has bounded inputs and outputs. |
| `DEFER-08` | Inheritance and protected representation add layout and lifetime rules. | Stable ordinary and dynamic classes | A use case cannot be expressed by composition, static capabilities, or dynamic contracts. |
| `DEFER-09` | Stable C++ ABI needs layout, calling, ownership, and compatibility contracts. | Dynamic value implementation and an ABI/interoperation proposal | A supported external consumer needs stable layout, calls, lifetime responsibility, and compatibility. |
| `DEFER-10` | Runtime reflection adds metadata, discovery, and reflective operations beyond dispatch. | Implemented class/dynamic semantics and a reflection proposal | A runtime consumer justifies explicit metadata, ownership, lookup, and cost. |

### DEFER-11 — Managed object class form

- **Reason deferred:** Reference identity and automatic reclamation require rules
  for cycles, nullability, reclamation timing, finalization, weak references,
  relocation, pinning, and C++ boundaries. `managed` and `gc` are possible names,
  not defined contracts. Tracing metadata alone does not provide user-visible
  runtime reflection.
- **Depends on:** `CLS-06`, settled class ownership and lifetime, and the memory
  model if references may cross threads.
- **Reactivation condition:** An API needs opt-in reference identity and automatic
  lifetime beyond ordinary values or explicit owner/shared forms, and specifies
  observable guarantees, runtime participation, and interoperation cost.

## Implementation

After `OPEN-01` and `OPEN-02`, ordinary classes can be delivered through syntax,
field visibility, associated and instance lookup, receiver access, availability,
semantic IR, diagnostics, lowering, and documentation.

Dynamic implementation follows `OPEN-03` through `OPEN-05`: contract and
conformance, explicit erased construction, calls, and lifetime. Only the optional
generic part in `OPEN-06` depends on generics. C++ boundary slices additionally
require explicit `import(cpp)`/`export(cpp)` carrier contracts.

Choose C++ values, factories, tables, thunks, proxy storage, and direct or indirect
calls from the resolved semantic facts.

## Validation

Ordinary-class validation covers:

- transparent struct construction and field access, and class representation privacy;
- in-body construction, external factories, and rejection of `Type()` construction;
- receiver access, exactly-once evaluation, use after Take, and whole-representation decomposition;
- operation and helper visibility across class and module boundaries;
- C++20/C++23 compilation, linking, and execution without fixing constructor or layout strategy.

Dynamic validation additionally covers nominal conformance and its diagnostics,
explicit allocation and ownership, referent lifetime, copying, mutation,
nullability, exactly-once calls, typed failures, direct/erased equivalence, and
finite generic dispatch. Reject C++ definitions and provider entry points that
introduce untracked calls.
