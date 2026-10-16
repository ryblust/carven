# Dynamic ownership and allocation

- **Status:** Draft
- **Scope:** Dynamic and native resource owners, allocation policy, construction in owned storage, and operation-specific container capabilities

## Summary

| Slice | Design status | Remaining decision |
| --- | --- | --- |
| Unique owner | Draft | Select a consumer and the source ownership and lifecycle surface |
| Source-owned TCP (`NET-02`) | Draft | Define native resource capabilities, retained backing, and close access |
| Shared owner | Exploration | Select a multi-owner consumer and its access contract |
| Allocation policy | Exploration | Establish the allocator state and failure behavior required by a consumer |
| Move-only Vector elements (`VEC-01`) | Deferred | Select a consumer for operation-specific capability and lifetime checking |

## Context

String and Vector own dynamic storage. Async operations have completion and
cancellation owners. General `Unique<T>` and `Shared<T>` types, source-defined
lifecycle operations, and allocator parameters remain unimplemented. Source
`ptr<T>` is a nullable nonowning handle. The owner contracts below are proposed.

## Design

### Construction and lifetime

The proposed owners manage a separate object's allocation and lifetime through
ordinary associated factories such as `Unique<T>::new(...)`. The factory name
remains an ordinary function name; the owning type supplies lifetime behavior.
Current value classes derive copying and cleanup from their fields. Implementing
dynamic owners requires storage/lifecycle primitives and derived capabilities.

### Owning types

Use the provisional standard-library types `Unique<T>` for one owner of a dynamic
object and `Shared<T>` for shared ownership. Keep owning handles distinct from
nullable, nonowning `ptr<T>`.

An initial Unique owner is non-null, noncopyable, and transferable. Destruction
destroys its live payload exactly once and releases the matching allocation.
Moving a handle does not relocate its payload. Checked borrows cannot outlive
the responsible owner. Source Take rules still determine whether moving an owner
with outstanding borrows is permitted; the payload's stable address does not end
those borrows. Optional ownership needs a separately defined optional-value
contract.

A Shared copy refers to the same object and retains its lifetime. It does not
deep-copy the payload. The last owning handle destroys the payload and releases
its storage. Require a checked uniqueness or synchronization mechanism for shared
mutation. Carven's Write access permits other handles to observe the same object.
Cross-thread use, atomic reference counting, weak references, and cycles need
their own contracts before that surface is exposed.

### Allocation policy

The proposed default policy uses the platform allocation mechanism. Stateful
allocator support must specify propagation through copying and Take, the lifetime of
allocator state, allocation/deallocation pairing, alignment, and ownership across
native boundaries. A stateless allocator requires no per-owner field. Use
compressed member storage only when an empty policy requires no distinct address.

Existing builtin allocation failure terminates without a nominal throw effect.
Recoverable allocation, if provided, uses an explicit fallible operation and
defines whether arguments have been consumed when allocation or construction
fails. A successful allocation alone does not establish a live T object.

### Construction destinations

Constructing directly in owned storage is a general mechanism consumed by
factories, containers, and native adapters. It must preserve source evaluation
order, initialized-prefix cleanup, and failure propagation. The mechanism applies
to all factories independently of their names.

An owner factory needs empty/live/consumed destination states, partial
initialization, and native object lifetime. It consumes those construction
contracts to release storage after failure and destroy completed payloads on every
applicable exit. Supporting immovable native payloads requires
delivery directly to their final location, rather than assuming a temporary can
always be moved after a factory returns.

### NET-02 — Source-owned TCP resources

- **Implementation:** Not started

The selected consumer is an outbound TCP client connecting to a literal IPv4
address and port, transferring bytes, and closing. The implemented `std::net`
borrows host-owned, address-stable native connections. Source-owned connect and
Stream remain unimplemented. Listen/accept, DNS, TLS, and cross-thread transfer
require their own consumers and contracts.

The source owner must be noncopyable, support whole-owner transfer only when no
borrow survives, and propagate that capability through containing values. Its
view must retain a checked relationship to that owner. A hidden native pointer
inside an ordinary source struct does not establish that relationship.

| State or transition | Required source contract | Existing support and gap |
| --- | --- | --- |
| Creating → open or failed | A cold connect operation publishes one owner only after success; failure/cancellation closes the pending socket | Runtime cancellation and native RAII exist; source construction into noncopyable owner storage needs explicit capability facts |
| Open → transferred | Whole-owner Take invalidates the old binding; any retained view or cold operation prevents transfer | Take and retained-holder checks exist; native resource capability and backing relationships must propagate through source values |
| Open → cold read/write | Creating an Operation retains the view's owner; dropping an unstarted operation releases its borrow without I/O | Ordinary Operation holder checks exist; the native pointer's pointee is currently a host obligation |
| Open → active | One read and one write can coexist; another same-direction operation fails with Conflict | Implemented by the native adapter; a source owner must remain stable while providers can reach it |
| Active → cancelling → open | Cancellation completes only after provider registration and buffer access end | Implemented by structured close and the adapter; releasing the owner early must be rejected in source |
| Open → closed | Explicit close is idempotent; pending operations must first be cancelled and drained | Native close reports Conflict while pending; source close access and the observation rules for surviving views remain open |
| Closed → read/write | Return Closed without accessing a released socket | Implemented for host views |
| Open/closed → destroyed | Exactly one socket release; no reachable provider survives destruction | Native RAII enforces the host contract; source destruction and borrow checks need the owner facts above |

Native wrappers currently rely on C++ copyability and host lifetime contracts.
A wrapper that copies native `Connection` passes Carven analysis; C++ rejects
the deleted copy constructor. The shared `ConnectionView` nominal supports
existing cold-operation holder checks, while the pointed-to socket's lifetime
remains the host's responsibility.

Implement owner capability facts and explicit backing relations before exposing
source-owned connect. Select the finite native-resource declaration mechanism,
the access required by close, and the behavior of any surviving views. Keep this
delivery independent of a general Unique API, user destructor hooks, and public
concepts. Validate rejection of all owner copies and of transfers or scope exits
with outstanding cold/active borrows. Verify failed construction, unstarted drops,
cancellation, repeated close, and exactly-once native release.

## Deferred work

### VEC-01 — Move-only Vector elements

- **Reason deferred:** Current consumers use copyable elements and fixed async
  composition. Internal capability checks can evolve independently of public concepts.
- **Reactivation condition:** A concrete move-only container consumer, such as
  a runtime-sized finite batch of operations with retained input borrows.

Check copying, transfer, relocation, destruction, and retained borrowing at each
Vector operation. Runtime growth and push already transfer elements; copying,
assignment, and `from_slice` require copyable elements. Whole-owner transfer does
not copy its elements. Operation elements additionally retain parameter borrows;
admission needs source capability propagation, extraction/access checks, cleanup,
and static-execution rules. The container does not start or cancel operations.

Check the lifetime requirements of each admitted native, borrowed, or callable
element family. Validate accepted transfers, rejected copies, growth, destruction,
and retained borrows through the selected consumer.

## Validation

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
