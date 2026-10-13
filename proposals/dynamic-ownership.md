# Dynamic ownership and allocation

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** Dynamic object owners, allocation policy, and construction in owned storage

## Current boundary

String and Vector already own dynamic storage, and async operations already have
accountable completion and cancellation owners. Those builtin contracts do not
provide a general `Unique<T>` or `Shared<T>`, custom source-defined lifecycle
operations, or allocator parameters. Source `ptr<T>` remains a nullable nonowning
handle. The owner contracts below are still proposed.

## Construction and lifetime

The proposed owners manage a separate object's allocation and lifetime through
ordinary associated factories such as `Unique<T>::new(...)`. The factory name
remains an ordinary function name; the owning type supplies lifetime behavior.
Current value classes derive copying and cleanup from their fields. Implementing
dynamic owners requires storage/lifecycle primitives and derived capabilities.

## Owning types

The proposed library surface uses `Unique<T>` for one accountable owner of a
dynamic object and `Shared<T>` for shared ownership of one object. These names
are provisional standard-library types, not new keywords or global builtin
reservations. Owning handles remain separate from nullable, nonowning `ptr<T>`.

An initial Unique owner is non-null, noncopyable, and transferable. Destruction
destroys its live payload exactly once and releases the matching allocation.
Moving a handle does not relocate its payload. Checked borrows cannot outlive
the responsible owner. Source Take rules still determine whether moving an owner
with outstanding borrows is permitted; the payload's stable address does not end
those borrows. Optional ownership needs a separately defined optional-value
contract.

A Shared copy refers to the same object and retains its lifetime. It does not
deep-copy the payload. The last owning handle destroys the payload and releases
its storage. Shared ownership grants no general mutable access: Carven's current
nonexclusive Write access is not proof that no other handle observes the object.
Mutation requires a separately checked uniqueness or synchronization mechanism.
Cross-thread use, atomic reference counting, weak references, and cycles need
their own contracts before that surface is exposed.

## Allocation policy

The proposed default policy uses the platform allocation mechanism. Stateful
allocator support must specify propagation through copying and Take, the lifetime of
allocator state, allocation/deallocation pairing, alignment, and ownership across
native boundaries. A stateless allocator requires no per-owner field. A future
allocator parameter can use compressed member storage where an empty policy
requires no distinct address.

Existing builtin allocation failure terminates without a nominal throw effect.
Recoverable allocation, if provided, uses an explicit fallible operation and
defines whether arguments have been consumed when allocation or construction
fails. A successful allocation alone does not establish a live T object.

## Construction destinations

Constructing directly in owned storage is a general mechanism consumed by
factories, containers, and native adapters. It must preserve source evaluation
order, initialized-prefix cleanup, and failure propagation. The mechanism applies
to all factories independently of their names.

[Uninitialized storage](uninitialized-storage.md) defines the proposed
empty/live/consumed destination states, partial initialization, and native object
lifetime. An owner factory consumes
those contracts to release storage after failed construction and destroy completed
payloads on every applicable exit. Supporting immovable native payloads requires
delivery directly to their final location, rather than assuming a temporary can
always be moved after a factory returns.

## Delivery evidence

Select an end-to-end Unique consumer such as a recursive node or an independently
allocated native resource. Establish source capability checks, whole-owner
transfer, retained-borrow rejection, construction failure, and exactly-once
cleanup through ordinary and failure exits. Native representation must preserve
the C++20 baseline and the allocator's matching release requirements.

Shared ownership is activated by a concrete multi-owner consumer. Its evidence
must cover handle copies, final release, target access, and retained borrows;
cross-thread tests belong only to a selected concurrency contract. Vector element
admission for either owner additionally needs conditional copying and relocation
of move-only elements.

Owner-bearing failure types retain the current Copy admission requirement.
For admitted payloads containing Shared owners, validate retained backing and final
release through throwing, propagation, matching, rejected guards, handler failure,
and rethrow. Catch bindings must preserve the original failure while further
matching or rethrow can still use it.

## References

- [Owning vectors](../docs/language/aggregates.md#owning-vectors) and
  [String](../docs/language/text.md): implemented builtin storage owners.
- [Async](../docs/language/async.md): implemented operation ownership and cancellation.
- [Pointers](../docs/language/pointers.md): current nonowning handles.
- [Failures](../docs/language/failures.md): Copy admission and caught-payload lifetime.
- [Uninitialized storage](uninitialized-storage.md): proposed construction destinations and cleanup handoff.
