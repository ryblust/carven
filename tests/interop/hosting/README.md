# Hosted execution contracts

These C++23 hosts include `carven/hosting/` headers to call interpreted functions
and replace selected entries while retaining native application state. They link
the compiler objects from `carven-modules`. Document and preview
entries also run as C++20 implementations generated from the same Carven sources.
Generated provider contracts use each source module's canonical name in both
execution paths.

Build and run from the repository root so the hosts can read their `.cv` files:

```sh
./xmakew build
./xmakew test 'carven-test-hosting*/*'
```

The three non-default binary targets are `carven-test-hosting`,
`carven-test-hosting-business`, and `carven-test-hosting-preview`.
They check observable values, effects, failure payloads, ownership, and cancellation
through the public hosting API.

## Scalar calls

`main.cpp` loads `program.cv`, selects the bare `calculate` function, and
calls it with different arguments. It checks that argument errors precede source
effects, a declared failure retains its payload, and subsequent calls succeed.
Loading routes static output once; each call has an independent execution domain.

`HostFunction::call` blocks for a synchronous function using the session
execution path. The function handle retains its original program. Source records
cross the boundary with module and type names and owning fields.
Enums carry their selected case and ordered payload. Generic nominal types are
rejected because the boundary does not represent their type arguments.

## Business state and documents

`business.cpp` exposes a score threshold, text decoration, and decision submission.
`business.cv` uses those services to process a record. Each decision retains
the decorated text, original text, and score alongside its label. Reloading
`business_updated.cv` changes the rule without discarding native configuration or
previous submissions. Source failures retain their identity and data after the
program is released.

`enumeration.cpp` uses one source program for interpreted and compiled calls.
It checks numeric cases with explicit codes, enum values nested in records and
vectors, owning payloads, synchronous and asynchronous failures, and native
service failures. Invalid cases and payload shapes fail argument admission.
Reload rejects different numeric case values or underlying integer types.
An object reference nested in an enum payload must remain alive until a cold
native entry starts. Synchronous entries exercise a vector of enums; async Read
uses an enum containing the native reference directly.

The same binary runs `document.cpp` and `document_pair.cpp`. A native `Document`
owns its text and view. `HostObject` registers its stable address;
`ObjectRef<Document>` from that registration is weak. The host invalidates the
registration before moving or destroying the document. Method calls check exact
native type and liveness. Pending source work retains owned inputs, and checks the
document again when it accesses a method after suspension.

`HostSubscription` owns revocable event delivery. Its copyable endpoint posts
owning arguments into the session mailbox without entering source code inline.
Queued events select the current entry when admitted; submitted tasks retain the
implementation selected at submission. Revocation cancels queued events.
Application code cancels already admitted work when its results become obsolete.

`document_pair.cpp` starts from compiled entries generated from
`document_pair.cv`. Reloading `document_pair_updated.cv` replaces `render` while
leaving the compiled `identity` entry in place. Its `selected` entry accepts a
source record containing a vector of native document references. Both documents keep the same
registrations, subscriptions, and call sites. A failed replacement changes no
entry. An admitted call finishes its original implementation.

Carven declarations determine host service contracts, nominal payloads, and failure
types. Hosts include their generated provider headers and bind implementations
using those types. Compiled entries are registered directly from generated API
headers. The small provider `.cpp` files connect generated native entry points to
the session's registered services. Declared failures use ordinary source recovery;
host execution errors stop the current invocation in both modes and return
`HostError`. Stopped calls unwind their local storage and close outstanding
children. Source recovery handles declared failures; it cannot catch execution stops.

## Asynchronous preview

`preview.cpp` starts the generated `preview.cv` implementation, reloads
`preview_updated.cv`, and completes requests from both implementations. The host
owns request completion, cancellation, and the event loop. `poll` advances bounded
execution slices; pending requests create no background interpreter thread.

`DocumentPreview` tracks the selected task separately from all pending tasks.
Only the selected successful result updates the view. Closing it revokes its
subscription, cancels its tasks, and destroys the document. Later provider replies
are rejected after detachment.

The async cases also check declared query failures, cancellation before response
consumption, and a provider response exceeding the configured value limit. An
invalid response stops the whole activation and detaches sibling requests;
cleanup needs no further response from those providers. The task retains the
original execution error. A declared failure instead remains recoverable by the
source body.

## Host lifetime

Session and task controls run on one owning thread. Provider callbacks return
promptly and do not reenter those controls. Request completion and event posting
may occur on other threads. Raw callback contexts outlive session closure;
destroyable native objects use checked registrations rather than raw async context
pointers. Closing a session drains cancellation before releasing its registrations.

Explicitly registered object references remain weak. Native reference carriers
retain object identity when copied, including inside source records, vectors, and enums.
Other native values follow their C++ copy semantics. Hosting text carriers,
including `std::string`, require UTF-8 and obey the call's byte limits. A provider can use
`ObjectRef<T>::own(value)` to retain an instance through shared ownership.
Owning values and completed task results do not retain a session.
