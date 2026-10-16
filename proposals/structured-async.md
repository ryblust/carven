# Finite dynamic async composition

- **Status:** Deferred
- **Scope:** Same-thread composition of a runtime-sized, finite operation sequence

## Summary

Compose a runtime-sized finite batch of operations with shared result and failure
types, and deliver results in input order.

## Deferred work

### ASYNC-01 — Dynamic when_all

- **Implementation:** Not started
- **Reason deferred:** Current applications use sequential awaits or fixed
  `when_all` batches without a demonstrated dynamic-composition cost.
- **Reactivation condition:** A consumer needs a runtime-sized finite batch.
- **Depends on:** Checked operation storage and retained borrowing, including
  move-only element capabilities if Vector supplies that storage.

A checked builtin may establish these types and failures without public parameter
packs, higher-order callbacks, or concepts.

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

## Validation

Cover completion order, ordered delivery, startup failure, child failure,
cancellation, retained borrows, and exactly-once cleanup. Compare sequential
awaits, fixed batches, and ordinary async wrappers. Measure stack, state
allocation, dispatch, and generated-code size independently.
