# Design directions

This page records candidates without a developed standalone proposal. Each entry
states the problem, remaining questions, and evidence needed to select a scope.

## Library consumers

`Option<T>` is a possible generic-enum consumer. If public Option/Result APIs are
selected, define their value and ownership contracts. Result also needs explicit
failure capture and a defined relationship to typed control effects.

The constant-storage proposal owns growable containers. Iterator and operator
protocols depend on the selected API. Type computation, structural queries, and
declaration generation each need a defined source contract.

## Multi-field consuming decomposition

A consumer needing two independent field owners must establish whole-representation
extraction, with the original owner unavailable, exactly-once field evaluation,
and deterministic disposition of every field on success, failure, and rejected
patterns. No usable partially moved object remains. A concrete `build`, `finish`,
or `into_*` operation can select syntax. Validation covers all field dispositions
and exits, including unused fields and borrowed backing.

The tuple proposal owns builtin product decomposition. This candidate concerns
nominal records and their field-disposition rules.

## Failure extension edges

### Richer failure payloads

- **Status:** Deferred
- **Implementation:** Not started
- **Reactivation condition:** A concrete API needs a payload outside the copyable
  nominal category and its underlying ownership form has a defined contract.

Move-only values, owning references, and managed references are independent
candidates. The open decisions are:

1. **OPEN-01 — Select a payload category.** Describe an end-to-end
   `throw` → `?` → `catch` → `rethrow` example with explicit copying, consumption,
   borrowing, sharing, or identity.
2. **OPEN-02 — Define handler ownership.** After `OPEN-01`, define pattern-binding
   lifetime, continued matching after a rejected guard, handler-produced failure,
   preservation by `rethrow`, and destruction on every exit.

Preserve closed typed failure sets and establish source ownership before changing
private carriers. Validate construction, propagation, matching, false guards,
handler failures, rethrow, and cleanup, with diagnostics for invalid transfers and
expired borrows. Public C++ failure mapping requires its own carrier and lifetime
contract. Async owns suspension and cancellation transport.

## C++ interoperation candidates

Broader return-borrow contracts need a concrete native interface and explicit
backing guarantees. Generic providers and construction destinations are covered
by the generics and uninitialized-storage proposals.

Stable binary interfaces, precompiled distribution, plugin loading, and open-world
discovery each require a consumer and an ABI, ownership, lifetime, and failure
contract for the selected boundary.

## Deferred infrastructure

A consumer or measured cost problem supplies the evidence for selecting each
candidate.

| Direction | Reactivation evidence and retained questions |
| --- | --- |
| Query system or concurrent queries | Repeated analysis or independent demand justifies it; define unique computation, pending/running/completed/failed states, wait relationships and cycle handling, deterministic identity commit, and fixed-point convergence |
| Incremental analysis, persistent IDs, or syntax caches | An interactive or measured reuse workload needs them; define content/version/options keys, source identity rebinding, invalidation, and the import-directory contract |
| Independent execution IR | Dispatch cost, repeated execution, debugger stepping, or pause/resume justifies a separate representation; preserve demand-driven completion, cleanup, failure, and budgets without rejecting unexecuted native branches during preparation |
| Generated C++ module interfaces | A supported consumer needs them; preserve artifact identity and dependencies while BMI orchestration remains a build-system responsibility |
| Reflection and declaration generation | A consumer identifies the required query surface and bounded generation model |
| Runtime reflection | A dynamic API justifies explicit metadata, ownership, and runtime cost |

If queries may wait on one another, define cross-query wait relationships and
cycle detection or prevention; per-query completion state alone is insufficient.
Distinguish semantic dependency cycles from scheduling waits and preserve
diagnostics and events. Test caches across source, option, and dependency changes.
For an execution IR, measure preparation cost against reuse in one-shot roots and
repeatedly executed bodies.

## Deferred representation candidates

Packed records, Boolean bit packing, AoS-to-SoA conversion, hot/cold splitting,
and bytewise comparison are separate possible designs. Select an object workload
and state addressability, alignment, aliasing, copying, comparison, and native
interface obligations before choosing a representation. Verify relevant effects
and measure target-program memory and access costs. The layout proposal covers
ordinary field ordering and pointer tags.

## References

- [Proposal index](README.md)
- [Generics](generics.md#open-decisions): parametric declarations and C++ boundaries.
- [Tuples](tuples.md): builtin product decomposition.
- [Constant storage](constant-storage.md#growable-library-containers): growable containers.
- [Uninitialized storage](uninitialized-storage.md): construction destinations.
- [Layout](layout.md): field ordering and pointer representation.
