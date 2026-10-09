# Design directions

This page records candidates without a developed standalone proposal. Each entry
states the problem, remaining questions, and evidence needed to select a scope.

## Library consumers

`Option<T>` is a possible generic-enum consumer. If public Option/Result APIs are
selected, define their value and ownership contracts. Result also needs explicit
failure capture and a defined relationship to typed control effects.

Iterator protocols need a concrete source API and access contract. Type
computation, structural queries, and declaration generation each need a defined
source contract.

## Vector extensions

Additional element types, views, and operations require a concrete consumer and
the contracts below.

| Direction | Activation and required contract |
| --- | --- |
| Borrowed elements, such as `Vector<str>` | A parser or collection consumer needs them; track backing through insertion, replacement, extraction, copies, and escaping owners without extending referent lifetimes |
| Move-only elements, including async or Unique owners | A concrete owner needs dynamic storage; derive conditional copying, relocation, extraction, and cancellation/disposal without weakening Take rules |
| External C++ elements | A native consumer needs them; establish construction, relocation, exception termination, cleanup, and borrowing through native capability checks |
| Mutable slices | An algorithm needs a writable view; define permissions, aliasing, owner mutation, and lifetime consistently with nonexclusive Write |
| Bulk append, insertion, and pop | A consumer needs the operation; specify aliasing, partial effects, copying versus consumption, order, and empty-result behavior |

Allocator and recoverable-allocation contracts belong to dynamic ownership.

## Multi-field consuming decomposition

A nominal-record consumer needing independent field owners must establish
whole-representation extraction, with the original owner unavailable, exactly-once
field evaluation, and deterministic disposition of every field on success,
failure, and rejected patterns. No usable partially moved object remains. A concrete `build`, `finish`,
or `into_*` operation can select syntax. Validation covers all field dispositions
and exits, including unused fields and borrowed backing.

## Async extensions

Additional async scopes need a consumer and their own ownership and completion
contract.

| Candidate | Reactivation evidence and remaining contract |
| --- | --- |
| Repeated selection and runtime-sized groups | A workload needs to consume a changing collection of completions; define remaining-owner availability, result typing, startup and selection order, retained backing, cancellation, and closure on every exit |
| Streams and same-thread channels | A producer/consumer workload needs repeated delivery; define capacity, backpressure, value transfer, end-of-stream, failure, cancellation, and endpoint lifetime |
| Shared tasks and supervisors | A workload needs multiple observers or ownership beyond one lexical scope; define the accountable owner, result access, shutdown, and backing lifetime |
| Async resource disposal | A resource needs suspending cleanup; define exit selection, cleanup failure, cancellation acceptance, and the lifetime of cleanup dependencies |
| Provider adapters | A concrete I/O or callback API needs source-level access; define partial results, resource ownership, registration lifetime, cancellation commitment, and callback quiescence |

A runtime index alone cannot establish that a lexical child remains available.
Repeated selection needs a checked remaining-owner abstraction; it must not
enumerate all remaining subsets or completion permutations. Dispatch may grow
linearly with the number of concrete slots. A selected result remains alive while
losers close and release borrowed backing. Transferring an immovable completed
value into a product additionally needs a construction-destination or owned-backing
contract; moving a completion handle does not move its payload.

Measure group state, allocation, dispatch, and generated-code growth alongside
behavior. Custom frame allocation needs a measured surviving frame cost after
existing fusion and tail-await lowering. Cross-thread execution additionally needs
the concurrency model; same-thread composition does not establish it.

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
