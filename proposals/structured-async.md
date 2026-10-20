# Finite dynamic async composition

- **Status:** Deferred
- **Scope:** Same-thread composition of a runtime-sized, finite operation sequence

## Summary

Compose a runtime-sized finite batch of operations with shared result and failure
types, and deliver results in input order.

## Design

### Ownership and activation scope

The bounded batch operation belongs to `std::async`. A builtin can derive its
result and failure types; compiler knowledge checks operation transfer, retained
borrows, result backing, and structured close. Runtime Craft supplies shared
child/completion storage, dispatch, and cancellation mechanisms. Standard Craft
owns public data declarations and source-level batching, retry, or application
policy that composes this operation.

Activate `ASYNC-01` with a finite same-thread batch consumer. Select checked
storage for that consumer: Vector needs operation-specific element admission;
a dedicated batch owner needs equivalent transfer, access, and cleanup facts.
The choice supplies stable child and result lifetimes. Public parameter packs,
higher-order generics, and a general effect system are independent extensions.

## Deferred work

### ASYNC-01 — Dynamic when_all

- **Implementation:** Not started
- **Reason deferred:** Current applications use sequential awaits or fixed
  `when_all` batches without a demonstrated dynamic-composition cost.
- **Reactivation condition:** A consumer needs a runtime-sized finite batch.
- **Depends on:** Checked operation storage and retained borrowing, including
  move-only element capabilities if Vector supplies that storage.

Allocate stable child slots before starting work. Define empty input, startup
failure cleanup, cancellation followed by provider completion, and borrowing of
the input and result backing. Completed results and their backing remain alive
while other children cancel and finish. Immovable results require final-location
construction or an owning backing-storage delivery contract; moving a completion
handle does not move its payload.

Represent remaining work with per-child runtime slot state and dispatch. Keep
generated control structure independent of completion permutations and
remaining-input subsets. Keep batch size fixed after startup and release all
child and result storage through structured cleanup.

Reuse the existing invocation-stop contract: a stopped child stops the whole
invocation and closes its siblings. It is neither a declared failure nor a
cancelled result eligible for recovery or selection.

## Validation

Cover completion order, ordered delivery, startup failure, child failure,
cancellation, retained borrows, and exactly-once cleanup. Compare sequential
awaits, fixed batches, and ordinary async wrappers. Measure stack, state
allocation, dispatch, and generated-code size independently.
