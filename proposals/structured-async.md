# Finite dynamic async composition

- **Status:** Deferred
- **Scope:** Same-thread composition of a runtime-sized, finite operation sequence

## ASYNC-01 — Dynamic when_all

- **Implementation:** Not started
- **Reason deferred:** Current applications use sequential awaits or fixed
  `when_all` batches without a demonstrated dynamic-composition cost.
- **Reactivation condition:** A consumer needs a runtime-sized finite batch.
- **Depends on:** Checked operation storage and retained borrowing, including
  move-only element capabilities if Vector supplies that storage.

Consume operations sharing result and failure types and deliver results in input
order. A checked builtin may establish these types and failures without public
parameter packs, higher-order callbacks, or concepts.

Allocate stable child slots before starting work. Define empty input, startup
failure cleanup, cancellation followed by provider completion, and borrowing of
the input and result backing. Completed results and their backing remain alive
while other children cancel and finish. Immovable results require final-location
construction or an owning backing-storage delivery contract; moving a completion
handle does not move its payload.

Use runtime slot state and dispatch, without enumerating completion permutations
or remaining-input subsets. A batch has a fixed size after startup; adding and
removing running tasks belongs to a separate consumer-driven design.

## Validation

Cover completion order, ordered delivery, startup failure, child failure,
cancellation, retained borrows, and exactly-once cleanup. Compare sequential
awaits, fixed batches, and ordinary async wrappers. Measure stack, state
allocation, dispatch, and generated-code size independently.

## References

- [Async](../docs/language/async.md): current ownership and provider contracts.
- [Dynamic ownership](dynamic-ownership.md#vec-01--move-only-vector-elements):
  operation-specific Vector capabilities.
