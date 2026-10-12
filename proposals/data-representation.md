# Data representation and layout

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Record, tuple, and enum representation, native-layout boundaries, compact handles, and collection layouts

## Summary

This proposal explores smaller records, enum carriers, and object collections
while preserving Carven's value, access, failure, and lifetime contracts.
Representation selection uses known values, structure, access, and lifetime facts
to choose C++ storage and operations. Explicit native forms would fix the relevant
interoperability contract. Compact collections have separate access and mutation
rules.

All forms and plan names below are candidates. No layout syntax or optimization
is accepted by this document. Select each experiment using target facts and a
workload; a smaller representation alone does not establish a performance gain.

| Slice | Next evidence or decision |
| --- | --- |
| Native layout boundaries and target facts | Define the ABI domain, admitted fields, and source of target layout information |
| Field ordering | Demonstrate preserved construction and observation order for a restricted record family |
| Tuple representation | Measure native compilation and generated size for large products while preserving element lifetimes |
| Enum niches and pointer tags | Specify valid encodings, addressable projections, and C++20 reconstruction |
| Pool-relative handles | Select a node workload with an explicit pool identity and reuse policy |
| Frozen compact collections | Select a builder/reader API with independently checked retention and representation contracts |
| Access-driven layout | Establish a workload where a size-only policy loses and a stable profile or access summary helps |

## Current architecture

`lower_structure` in `src/backend/lowering/decl/nominal.cpp` emits semantic fields
in declaration order. Payload enums use generated case records and `std::variant`;
`runtime::Outcome` also uses `std::variant`. Ordinary classes are encapsulated
nominal values: they introduce no allocation, inheritance, or virtual dispatch.
Field contents determine ownership and native lifecycle behavior.

`TargetPlan` owns names, failure ABI ordering, and artifact plans over a published
immutable `SemIRProgram`. `TargetPlanningRequest` contains test mode, linkage
domain, and an optional `SourceLibrary` describing shared library modules, linkage,
and implementation emission. It supplies no target layout facts. Structure
constants are reconstructed in semantic field order by `backend.lowering.constant`.
These consumers need one representation plan if physical order or encoding changes.

Tuples already use the recursive `runtime::Tuple<Head, Tail...>` aggregate in
`value/tuple.hpp`, with `[[no_unique_address]]` members, direct construction of
immovable elements, and positional access. Flat-product selection remains a
measurement candidate. Existing enum case records and failure carriers supply no
niche or pointer-tag plan, and no source `struct(c)` or `struct(cpp)` layout form
is implemented.

The evaluator already works with logical values and object identities rather than
native record bytes. Keep that separation. Generated programs use C++20; a
representation expressible in LLVM IR is not automatically legal C++20 storage,
aliasing, object lifetime, or constant initialization.

## Lessons from existing systems

The following are precedents, not guarantees inherited by Carven. References
identify the implementations and research behind each observation.

| Source | Relevant mechanism | Candidate transfer and limit |
| --- | --- | --- |
| Rust reference and rustc layout engine | Default representation freedom, explicit native representations, field-to-memory mappings, niche-aware enum selection | Separate source guarantees from a deterministic implementation policy; evaluate enclosing enum cost as well as record size |
| Swift ABI and library evolution | Extra inhabitants and spare bits; explicit exposure of layout across binary boundaries | Model valid representation states and ABI exposure separately; do not introduce runtime resilience metadata without a binary-library consumer |
| LLVM `PointerIntPair` | Pointer traits supply a count of usable low bits and recover the pointer | Require an explicit target/provider capability, including a plain representation fallback |
| V8 pointer compression | Offsets within a controlled allocation region | Compress a pool's links only with its owner and extent contract; arbitrary external pointers have no such domain |
| Goose | Inline variable-size records, relative links, and fixed/variable ADT modes | Explore collection-specific layouts whose APIs expose indexing and mutation restrictions; keep ordinary Carven resource lifetimes |
| ribbit, ICFP 2023 | Separate source ADTs and bit-level representations; derive construction and matching from a representation description | Use one checked plan for encoding, discrimination, and projection; its immutable ADT model does not prove Carven's mutable aliases safe |
| MLKit, ICFP 2024 | Double-ended bit-stealing in a complete ML compiler | Retain as a target-specific experiment, not evidence that high address bits are universally free |
| Marmoset, ECOOP 2024 | Field-access analysis and a constraint/cost model for packed recursive ADTs | Compare access-aware ordering with size-only ordering before considering a solver; its abstract optimum is not a hardware performance guarantee |
| packed-data, ECOOP 2025 | Typed builders and readers for packed values implemented as a library | Try an explicit craft/container boundary before changing the representation of every source record |
| Floorplan, GPCE 2019 | Declarative layout descriptions generate address calculations | Keep layout arithmetic and its verification in one owner instead of duplicating offsets across consumers |

## Representation contracts

### Ordinary and native records

The proposed default for ordinary `struct` and `class` preserves logical field
identity and behavior without promising physical declaration order. A source
nominal type need not map to a single C++ class or struct. Construction, projection,
copying, transfer, and cleanup must agree on the selected representation throughout
its native interface domain. Privacy is independent of representation: a class
method may still expose a field address,
retain a borrow, or call native code. Class encapsulation alone grants no right
to eliminate that field's storage.

Possible source forms, pending grammar and ABI decisions:

| Form | Proposed contract |
| --- | --- |
| Ordinary `struct` / `class` | Carven selects an eligible representation; source field order governs language observations |
| `struct(c)` | Target C ABI record with an explicitly admitted set of C-compatible fields and a C-consumable declaration |
| `struct(cpp)` | Ordinary generated C++ record with source member order and native member lifecycle; no Carven-specific field reordering or tag compression |

`struct(c)` does not mean packed. Its field types, alignment, padding, enum and
Boolean representations, and by-value calling convention need a specified target
contract. Source unit fields, empty records, Carven strings, operations, closures,
and payload enums need an explicit mapping or rejection; source familiarity does
not establish C compatibility. The current C++ export mechanism does not itself
provide C headers or C linkage.

`struct(cpp)` does not identify an existing C++ type or promise cross-compiler ABI
stability. Existing types remain imported native types. Native layout retains the
C++ compiler's ordinary padding and optimization under the selected ABI.

An outer layout marker never recursively changes a field's representation.
By-value nested types must satisfy the selected boundary; opaque pointers have
their own pointee and lifetime contracts. A native outer record containing an
ordinary optimized inner record cannot promise a stable recursive layout unless
that dependency is explicitly pinned. Specify admissibility or reject the field;
do not silently reinterpret it as another type.

Publishing an ordinary type through a generated C++ interface need not disable
optimization. It exposes that compilation's exact generated representation and
requires matching headers, runtime, and layout configuration. Durable native ABI
or independently evolving binary libraries need a separate explicit contract.
Changing an export annotation must not silently change a nominal type's layout.

### Addressable fields and encoded projections

Field reordering can retain one aligned C++ object per field. Bit packing, niche
encoding, and relative handles may instead represent a logical field through an
access operation. The plan must distinguish these cases.

For example, merging `pointer: ptr<Node>` and `state: u8` into one word removes
the independent pointer slot. `addressof(&value.pointer)` and a native Write
reference to that slot cannot be implemented as a temporary decode/writeback
without changing aliasing, reentry, and observation order. Two nonexclusive Write
arguments make this particularly visible. A field loan retained across an await
has the same problem.

Ordinary source places keep their promised address and access behavior. A
candidate either provides real stable storage, proves the transformation
unobservable within a closed scope, or uses an explicitly restricted handle/view
API. It must not reject otherwise valid ordinary source merely because an
optional optimization was selected. Opaque native calls and separate consumers
preclude assuming that an address never escapes.

## Field ordering candidate

The candidate aims to reduce padding and array stride while preserving semantic field
identity, source evaluation order, ownership, and borrowing. For illustration,
if u64 has size and alignment eight and u8 has size and alignment one, ordinary
member alignment can make `u8, u64, u8` occupy 24 bytes and `u64, u8, u8` occupy 16.
Actual sizes must be measured on the selected target.

One implementation stores `physical_order[physical index] = semantic field index`
and its inverse in the target plan. Unchanged records use the identity mapping;
all artifacts consume the same plan rather than copying semantic field tables.

| Consumer | Required correspondence |
| --- | --- |
| Member declarations | Emit physical order with semantic field names |
| Aggregate initialization | Arrange designated initialization in generated member order |
| Operand sequencing | Preserve source order exactly once, retaining earlier values when needed |
| Frozen-value reconstruction | Use the same positional or designated order |
| Field access, ownership, evaluation, and display | Continue using semantic field identity and order |

A restricted experiment can admit builtin scalar/pointer fields with known target
layout and unobservable member lifecycles. Unknown native layouts retain source
order. Nested records and arrays need eligibility proofs. Effectful initializers
require sequencing that preserves their source observations.

Specify supported targets, layout sources, eligibility, and a deterministic
policy, such as stable alignment-first ordering. Use size and alignment facts
from the selected target, and state the heuristic's limits.

Start with a natural-alignment policy rather than packed accesses. Stable ties
retain source order. Measure enclosing records and enum carriers too: a field
order with the same size can expose a more useful niche, while a smaller isolated
field may not reduce final stride. Do not reuse padding as persistent tag storage;
ordinary C++ copies and member operations need not preserve those bytes.

Extending empty-member overlap to ordinary records and adding Boolean bit packing
are separate candidates. Empty objects may have observable addresses, and C++
object-identity rules still apply.
Bit packing removes independent Boolean places and can couple otherwise separate
writes to one physical read-modify-write operation.

## Lifecycle and native interface

C++ member order affects construction, implicit copy/assignment, and destruction.
Temporaries can add observable operations or destroy direct construction of an
immovable component. Custom lifecycle code can change triviality and consequently
the Read snapshot/alias policy. Reordering native fields needs a proof covering
those behaviors.

Keep member declarations, final aggregates, and frozen reconstruction consistent.
Recheck snapshot observation points, reservation order, and backing construction
before borrowing results. Preserve each native lifecycle operation's effects.

Handwritten consumers may depend on positional/designated initialization,
offsets, or persisted bytes. Define supported construction interfaces, possibly
using semantic-order factories,
and require consistent configuration across headers, implementations, backing,
and callers. Construction destinations retain their initialization, lifetime,
and cleanup guarantees.

Do not make eligibility depend on which construction call sites happened to be
seen first. One nominal instance has one representation throughout its ABI domain.
Either every admitted construction has a legal realization or the type keeps its
ordinary representation. Failure before full construction cleans only completed
members under the existing semantic order.

## Tuple representation

A measured large-product cost may justify flat aggregates selected by canonical
product identity. Compare generated size and native compile time with recursive
aggregates before selecting another form. Shared definitions require consistent
cross-module placement and dependencies.

Preserve nested shapes, unit slots, ordered construction, stable element storage,
immovable native results, partial construction, retained loans, and cleanup.
Representation changes do not expand the source tuple API.

## Enum representation candidates

A niche is a representation state unavailable to a valid payload, reserved to
encode another alternative. It differs from padding and from a spare bit whose
value is ignored or fixed by an explicitly supported encoding. Model the valid
payload set, available states, and occupied states rather than recording only a
count of apparently unused bits.

Begin with single-payload enums and empty alternatives where all operations can
be realized without changing field addresses or native lifecycle behavior. A
non-null pointer carrier could admit a null niche; current nullable `ptr<T>` does
not supply that contract. A flow-local non-null fact is insufficient to change a
type's representation. Nested enums must account for states already used by an
inner enum. Construction and matching must agree on every remaining state.

Logical invalid states are not permission to create invalid C++ objects. A compact
Boolean-plus-empty enum may use integer storage with checked decoding; it must not
store an invalid bit pattern in a live native `bool`. Unknown native classes have
no inferred niches merely because a few examples leave bytes unused. SIMD masks
likewise have a logical validity contract distinct from their backend registers.

The plan supplies construction, discrimination, payload projection, replacement,
copy, Take, and cleanup. Project a native payload only when its lifetime is active.
Replacing `std::variant` can affect triviality, callable passing, exception-related
native traits, and constant reconstruction. Initial experiments should retain
trivial payloads and use the existing representation as the fallback.

Typed failure and async completion carriers are later consumers of this mechanism.
They must preserve success, each nominal failure, cancellation, and any internal
state that is actually observable by the runtime. Compression never merges those
states. `FailureABI` member order stays independent of physical discriminant
encoding; import/export providers require a matching carrier contract.

## Pointer-plus-state candidate

Identify a real pointer-plus-small-state value: a node kind, an ownership-defined
handle, or a restricted pointer payload enum. Identify the actual runtime state:
an inline value class may have none, while a callable's invocation thunk is a
function pointer that cannot generally be replaced by a Boolean tag.

Nullable pointers matter: hypothetical `Option<ptr<T>>` has distinct `None` and
`Some(nullptr)`, so one null niche cannot represent both. Access modes erased at
compile time provide no runtime bits to compress.

Each encoding candidate must establish:

- Alignment from valid target and allocation contracts, including low-alignment
  subobjects and external pointers. Count any additional alignment memory cost.
- Supported pointer encoding and recovery. High bits, function pointers, and
  arbitrary integer masks are not universally available. A tag does not prove
  provenance, access permission, liveness, or unique destruction.
- Correct restored pointers at native calls, dereferences, and release. Ordinary
  `T*` and `T*&` interfaces must not silently change representation.
- A separate pointer-plus-tag fallback, with consistent capability selection
  across an ABI domain. Version bits alone do not solve ABA or reclamation.

Keep the logical pointer and tag independent of the compression policy. Verify
toolchain interfaces and usable bits for each target while retaining the C++20
baseline.

Model a pointer-encoding capability with the address space, source/allocation
guarantee, available low bits, permitted round trip, null encoding, and unsupported
instrumentation modes. `alignof(T)` alone does not establish that an arbitrary
native address conforms to the required source contract. Do not infer usable bits
from a compiler-host pointer or from the largest observed allocation.

Start with controlled low-bit encodings. High bits may belong to memory tagging,
pointer authentication, or other target state. LLVM non-integral pointers can
carry information that an integer conversion cannot restore. Native function
pointers, capability pointers, and unsupported targets keep the plain carrier.
A target-specific C++ helper must establish legality independently of LLVM's
ability to represent the operation.

## Pool-relative handles

A relative handle is a logical reference into a named owning region, represented
by an offset or element index. It is distinct from a tagged machine address.
Begin with a fixed-size node pool and one chosen offset width; state whether the
range is measured in bytes or elements and how null is represented. Bounds,
alignment, element-start validity, target type, and checked arithmetic are part
of resolution. Count pool metadata and any per-handle base in the space result.

Pool identity must survive copies, aggregates, and calls. A global pool parameter,
type-level region evidence, or an explicit owner argument are alternatives; the
syntax and proof mechanism remain open. Handles cannot be resolved against an
unrelated same-shaped pool. A growing or relocating pool may preserve handles
without preserving raw pointers or borrowed views already obtained from them.
Define those invalidation rules separately.

Start without slot reuse. If reuse is added, choose explicitly between stale
logical references and generation-checked handles; generation width, wraparound,
and failed resolution need contracts. Version bits do not establish reclamation,
ABA freedom, or thread safety. Overflow must fail before publishing an invalid
handle, or select a separately identified wider representation; it cannot silently
change a live pool's ABI.

Self-relative links need additional copy rules. Moving a complete region can
preserve internal offsets; copying one node to another location generally cannot.
Define the relocated closure and treatment of outbound links before permitting
bytewise region relocation. Restoring bytes is not enough to create native objects
or to validate hostile input.

## Frozen compact collections

Explore an explicit builder and immutable reader for variable-size records or
recursive values. Keep ordinary structs and classes fixed-size native values in
the initial scope. A working model is:

```text
checked builder -> completed logical collection -> verified compact storage
                                                 -> borrowed reader/projections
```

Candidate storage includes inline strings and sequences, compact discriminants,
relative links, and optional offset tables. A variable-size element collection
supports sequential traversal; random access either requires an index or an
explicit scan. Immutable construction avoids resizing one record through the
middle of its neighbors. Reconstruction, field updates, and conversion back to
ordinary values are explicit operations with measured costs.

Readers returning decoded values or cursors do not pretend to provide native
`T&` places. Aligned real payload storage may support a checked borrow when its
lifetime is established; unaligned bytes need legal loads/decoding. A byte buffer
does not by itself create an array of nontrivial C++ objects. Initial eligibility
should exclude native resources and externally owned pointers. Region release
may skip per-element cleanup only for values whose contract permits it.

Build-in-destination and failure cleanup belong to the uninitialized-storage
proposal. A failed builder must not expose a partial value or leave valid handles
to reclaimed construction storage. Allocation failure and offset overflow need
an explicit failure policy rather than an invented source failure type.

Runtime compact storage, compile-time retained data, and a persistent wire format
are separate contracts. A file format additionally fixes versioning, endianness,
widths, padding policy, and input validation; optimized internal layout is not a
serialization promise.

For compile-time construction, begin with acyclic scalar/text records and an
explicit immutable reader. Execute logical construction, failures, and cleanup;
retain completed reachable values and backing before assigning physical offsets.
A conversion to a different reader type remains explicit. Pending operations,
scheduler state, native register bytes, and external resources are not logical
collection contents.

Offsets derive from final target storage, not host addresses or execution
coordinates. Native realization must produce legal C++20 static data with the
same reader contract as runtime construction. An encoding cannot silently add
dynamic initialization or change the ABI of a constant instance.

Compare static and runtime builders with the same reader and expected values.
Cover empty data, construction failure, escaping backing, offset overflow, and
cross-module storage; measure static bytes, native compilation, and access costs.
Shared or cyclic collections additionally need the retained-reference-graph
contract defined in semantic execution.

## Target representation planning

### Target inputs and ABI consistency

Extend the planning input with a validated target description before selecting
layout. Required facts include scalar size/alignment, pointer model and address
space, native ABI, relevant runtime/SIMD representation, and supported encoding
capabilities. The build driver owns acquisition and compatibility with the native
compiler. Cross compilation must not use host `sizeof` or execute target probes.

Two sources merit a bounded experiment: a deliberately small table of supported
target contracts verified by generated assertions, or native-toolchain layout
queries compiled for the selected target. LLVM `DataLayout` alone does not answer
C++ class lifecycle or complete native ABI questions. Unknown native types retain
ordinary delegated representation rather than blocking unrelated scalar records.

Give the layout policy and ABI-relevant configuration a reproducible identity.
Headers, implementations, constant backing, imports/exports, and separately built
consumers must agree. Define compile/link-time mismatch detection where practical;
do not rely on silently compatible-looking sizes or a cache key alone. Adding a
call site or changing an optimization level cannot privately alter one copy of a
shared type. Cross-version binary compatibility remains an explicit promise.

### One plan and its consumers

A proposed `TargetRepresentationPlan`, owned by `TargetPlan`, records:

- The semantic type and selected native form, target assumptions, and fallback.
- Physical field order and inverse mapping; known offsets, size, alignment, and
  stride with their target evidence, rather than guessed values for unknown types.
- Real addressable fields versus encoded projections, including backing identity.
- Enum/tag encoding, valid states, discriminant tests, and payload access recipes.
- Construction, transfer, replacement, cleanup, and static reconstruction needs.

Keep source access, validity, and lifetime facts in SemIR. Planning selects a
realization from those facts; it must not rewrite semantic declaration order or
re-run source checking. Preparation consumes the selected projection/storage
requirements; realization preserves sequencing and cleanup; emission prints
verified target syntax. Type-trait-sensitive Read policies must see the actual
chosen representation without allowing an optimization to change observable
snapshot/alias behavior.

Ordinary declaration/constant lowering, pattern matching, display, native
interfaces, and failure carriers must query this plan rather than independently
reconstructing offsets or tags. A plan verifier checks total and inverse field
mappings, encoding disjointness, alignment, occupied bits, and supported projection
recipes. Generated C++ checks verify target facts that the planner assumes.

Do not build a general representation DSL before a second encoding needs it.
Begin with identity and reordered-record plans; add enum recipes only with an
actual carrier experiment. The research on dual representations motivates the
boundary, not an obligation to reproduce its compiler.

## Execution and retained data

Symbolic execution locations and target addresses are different facts. Static
results need legal target references, not serialized execution coordinates.
Evaluate logical tags, freeze eligible results, and realize static initialization
as separate checks. Evaluator support does not make arbitrary pointer conversions
valid C++20 constant initialization or permit implicit runtime initialization.

The evaluator need not emulate target bytes for ordinary field access. Future
target-dependent layout queries need explicit target inputs and admission rules;
they cannot accidentally observe the evaluator host. Logical constants retain
their type and semantic field order; physical reconstruction queries the plan.

Pointer/integer tagging can obstruct C++20 constant initialization even when a
runtime helper is legal. Either choose a representation usable by every promised
construction path, explicitly model distinct storage forms with conversions, or
leave that type uncompressed. Do not pick a different ABI for a constant instance
of the same nominal type. Relative-offset constant data is a separate candidate
whose offsets are derived from final target storage, not execution coordinates.

SIMD layout work distinguishes compact scalar storage from computation layout.
Natural alignment, vector loads, access stride, and conversion cost can outweigh
padding savings. AoSoA or SoA should start as explicit collection representations
with view contracts, not as invisible rewriting of an addressable array of structs.

Async lowering may reuse the plan for Carven-generated closure or completion
records. It does not directly control the C++ compiler's coroutine frame layout.
Evaluate layout after existing fusion and tail-await transformations remove
unneeded carriers; do not optimize storage that disappears. Surviving loans,
suspension, cancellation, failure, and cleanup retain their existing semantics.

## Open decisions and bounded experiments

### OPEN-01 — Target contract and native forms

- **Status:** Active
- **Question:** Which target facts are provided to planning, and which nested
  types are admitted by the provisional `struct(c)` and `struct(cpp)` forms?
- **Closure condition:** One specified C ABI and C++ toolchain configuration,
  cross-compilation behavior, native consumer examples, and mismatch diagnostics.
  Source syntax and the field whitelist must be decided before implementation.

### OPEN-02 — Record ordering and observability

- **Status:** Active; selection consumes the target facts from OPEN-01.
- **Question:** Which record families preserve source construction, address,
  cleanup, and Read policy under a deterministic reorder?
- **Closure condition:** Identity/reordered plans for scalar records and nested
  eligible products, with native assertions, sequencing/failure counterexamples,
  and an array-traversal workload. Retain source order when eligibility is unknown.

### OPEN-03 — First compact carrier

- **Status:** Active; encoding selection consumes the target capability contract.
- **Question:** Is a scalar single-payload enum or an explicit pointer-plus-state
  handle the first worthwhile consumer?
- **Closure condition:** A complete valid-state table, projection and address
  policy, native lifecycle/constant realization, fallback, and measured carrier
  cost. Broad custom `std::variant` replacement is not required for this slice.

### OPEN-04 — Relative links and frozen collection API

- **Status:** Active
- **Question:** Which node/record workload justifies a fixed-width pool handle or
  a builder/reader collection, and which operations remain addressable?
- **Closure condition:** One owner and invalidation model, overflow behavior,
  construction-failure example, traversal/indexing costs, and explicit copy rules.
  Static promotion also needs eligible contents and legal static reconstruction;
  acyclic collections do not require a retained-cycle design.

## Deferred directions

### DEFER-01 — Access-driven ordering and data organization

- **Reason deferred:** No access profile currently justifies policy complexity
  beyond the restricted natural-alignment experiment.
- **Reactivation condition:** A repeatable workload where field locality, hot/cold
  separation, or AoS/SoA/AoSoA materially changes end-to-end cost.

Use semantic field-access summaries or explicit collection policies first. Profile
input must have stable provenance and invalidation. Hard constraints preserve
legality; size, stride, cache locality, indirections, and conversion cost are
preferences. Bound search cost and compare with simple heuristics before adding
an ILP solver. One ABI domain chooses one layout; per-function layouts require
explicitly distinct representations and conversion costs. Packed recursive layout
research does not establish legality for arbitrary mutable native-containing
records. Concurrency-sensitive packing additionally needs the concurrency
proposal's memory-location and synchronization contracts.

### DEFER-02 — High-bit tags and multi-payload bit encodings

- **Reason deferred:** Target pointer-state interactions, projection rules, and
  C++20 static realization need more evidence than the low-bit/carrier experiment.
- **Reactivation condition:** A named target and carrier whose measured savings
  survive decoding, instrumentation, and native-boundary costs.

Preserve capability/authentication/tagging state, recursive niche accounting, and
the fallback ABI. Do not infer support from a nominal 64-bit pointer width.

### DEFER-03 — General packed records and binary images

- **Reason deferred:** Unaligned addressable fields, mutation, native object
  lifetime, and schema evolution exceed the frozen collection experiment.
- **Reactivation condition:** A concrete memory-mapped or interchange consumer
  specifies its format, access API, verification, and target support.

Boolean bit packing, empty-member overlap, varint fields, and bytewise comparison
each need an observable contract. Equality must preserve field semantics, including
floating values and ignored padding, rather than assuming byte equality.

## Validation and stopping conditions

Field-order checks cover named, positional, default, and constant construction;
source order differing from physical order; Read followed by Write; early failure;
display; cross-module backing; and unknown or immovable native components.
Size assertions are conditional on the target, not portable language guarantees.

Tag checks cover null and every tag, range bounds, low/over-alignment, copy and
Take, native round trips, exactly-once destruction, fallback, and static storage.
Sanitizers do not independently establish ABI or provenance validity.

Compare current Carven, the candidate, and equivalent handwritten C++ on sizes,
stride, allocation, workload throughput/latency, generated bytes, native build
time, and binary size using representative record or node workloads. Reconsider
a design whose lifecycle and interface costs outweigh the measured gain.

| Experiment | Required comparisons and failure cases |
| --- | --- |
| Reordered records | Scalar and nested arrays; construction with side effects and early failure; field addresses; unknown and immovable native members; separate consumers |
| Compact enums/handles | Every tag and null combination; nested encodings; invalid ingress; copy/Take/replacement; address escape; exactly-once cleanup; forced fallback |
| Relative node pool | Raw-pointer and indexed-arena baselines; overflow, wrong pool, growth and stale views; full-region versus single-node copying; reuse only if admitted |
| Frozen records | Build, freeze, traversal, random lookup, conversion, and teardown; offset-index overhead; malformed lengths/tags/links; unsupported native resources |
| SIMD and async consumers | Aligned scalar versus compact/SoA traversal; cold construction and suspension; failures/cancellation; fusion enabled; surviving carrier allocation and stack use |

Test ordinary execution, admitted static execution, and native reconstruction
against the same logical results. Compile generated data using the C++20 baseline,
including static backing in multiple modules and C/C++ consumer translation units.
Inspect generated types and lifecycle traits as well as results. Where offsets
are checked, use facilities valid for the selected native type; `offsetof` is not
a universal query for arbitrary C++ classes.

A test-only layout permutation mode can expose accidental dependence on physical
field order. Keep one seed/configuration per ABI domain and test multiple legal
permutations against identity layout. Passing these tests is not a proof of
representation independence. A proposed layout report should explain semantic
versus physical fields, padding, encoding, target assumptions, and fallback reasons.

Separate payload bytes, allocator overhead, committed/resident memory, and virtual
address reservation. Include indexing tables, generation counters, pool slack,
cleanup, and conversion in end-to-end measurements. Report losses and workload
distribution as well as geometric means; results from another language's benchmark
suite do not predict Carven's gains.

## References

- [Read values and aliases](../docs/language/ownership.md#read-values-and-aliases): snapshot policy.
- [Uninitialized storage](uninitialized-storage.md): construction destinations.
- [Semantic execution](semantic-execution.md): address observations and retained graphs.
- [C++ representation](../docs/compiler/backend/representation.md) and [compiler architecture](../docs/compiler/README.md): current ownership and publication boundaries.
- [Async](../docs/language/async.md) and [tuples](../docs/language/aggregates.md#tuples): implemented completion and product contracts.
- [Concurrency](concurrency.md): future shared-memory obligations.
- [Planning request](../src/backend/generation/request/request.cppm),
  [nominal lowering](../src/backend/lowering/decl/nominal.cpp), and
  [constant reconstruction](../src/backend/lowering/constant/constant.cpp): current plan inputs and physical order.
- [Tuple runtime](../crafts/carven/runtime/value/tuple.hpp): current product representation.

### Compiler and runtime precedents

- [Rust type layout](https://doc.rust-lang.org/reference/type-layout.html), [Option representation](https://doc.rust-lang.org/std/option/#representation), and [rustc layout implementation](https://github.com/rust-lang/rust/blob/master/compiler/rustc_abi/src/layout.rs): representation guarantees, niches, and field mappings.
- [Rust layout randomization](https://doc.rust-lang.org/unstable-book/compiler-flags/randomize-layout.html): testing accidental layout assumptions.
- [Swift type layout](https://github.com/swiftlang/swift/blob/main/docs/ABI/TypeLayout.rst) and [library evolution](https://www.swift.org/blog/library-evolution/): extra inhabitants, spare bits, and ABI exposure.
- [LLVM PointerIntPair](https://llvm.org/doxygen/PointerIntPair_8h_source.html): explicit low-bit capability and recovery.
- [LLVM non-integral pointers](https://llvm.org/docs/LangRef.html#non-integral-pointer-type) and [Clang HWASan](https://clang.llvm.org/docs/HardwareAssistedAddressSanitizerDesign.html): pointer-state constraints.
- [V8 pointer compression](https://v8.dev/blog/pointer-compression): region-relative encoding and decompression tradeoffs.
- [Goose specification](https://github.com/aardappel/goose/blob/master/docs/goose_spec.md) and [benchmark summary](https://github.com/aardappel/goose/blob/master/bench/summary.md): layout/access tradeoffs and workload-dependent results.
- [C++ member initialization](https://eel.is/c++draft/class.base.init) and [pointer reinterpretation](https://eel.is/c++draft/expr.reinterpret.cast): native realization constraints; proposed implementations must also validate the C++20 baseline.

### Research

- Baudon, Radanne, and Gonnord, [Bit-Stealing Made Legal](https://perso.ens-lyon.fr/thais.baudon/icfp2023.pdf), ICFP 2023: dual source/representation descriptions and construction/matching validity for immutable ADTs.
- Elsman, [Double-Ended Bit-Stealing for Algebraic Data Types](https://doi.org/10.1145/3674628), ICFP 2024: representation inference and implementation in MLKit under target pointer assumptions.
- Singhal et al., [Optimizing Layout of Recursive Datatypes with Marmoset](https://arxiv.org/html/2405.17590v1), ECOOP 2024: access analysis, layout constraints, and packed recursive data.
- Jamet and Vollmer, [Type-Safe and Portable Support for Packed Data](https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2025.38), ECOOP 2025: library-based typed construction and traversal.
- Cronburg and Guyer, [Floorplan: Spatial Layout in Memory Management Systems](https://arxiv.org/abs/1909.05339), GPCE 2019: declarative layout and generated address calculations.
