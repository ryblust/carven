# Data layout and pointer representation

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Target field order, pointer-plus-state representations, and explicit native-layout boundaries

## Summary

This proposal explores compact representations for object collections and
handles. Field reordering, pointer tags, and explicit interoperation layouts are
independent choices. A representation experiment needs target facts and a workload
before its size reduction can justify adoption.

Structure lowering currently emits fields in declaration order, and target
planning has no layout-input contract. Generated programs use the C++20 baseline.
Layout changes need explicit source-lifecycle and generated-interface guarantees.

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

Explicit C/CPP layout forms are separate possible interfaces. C layout does not
mean no padding or recursively change nested types; CPP does not promise a stable
cross-compiler ABI. Specify each form's target and consumer-interface contract.

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

## Execution and retained data

Symbolic execution locations and target addresses are different facts. Static
results need legal target references, not serialized execution coordinates.
Evaluate logical tags, freeze eligible results, and realize static initialization
as separate checks. Evaluator support does not make arbitrary pointer conversions
valid C++20 constant initialization or permit implicit runtime initialization.

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

## References

- [Read values and aliases](../docs/language/ownership.md#read-values-and-aliases): snapshot policy.
- [Uninitialized storage](uninitialized-storage.md): construction destinations.
- [Execution model](execution-model.md) and [constant storage](constant-storage.md): address observations and retained graphs.
- [Representation candidates](roadmap.md#deferred-representation-candidates): packing and data organization.
