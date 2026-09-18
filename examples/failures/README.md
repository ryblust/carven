# Failure contracts

This program combines typed failures with ordinary expressions, module interfaces,
recovery policies, callable views, and owning or borrowed payloads. `main.cv` runs
five parts with fixed inputs; each part handles its outward failures before returning.

## Reading order

| Part | Files | What to follow |
| --- | --- | --- |
| Introduction | `intro.cv` | Declare a failure, propagate it, and read its payload |
| Composition | `orders.cv`, `composition.cv` | Combine failure sets, publish a contract, recover selectively, and observe expression evaluation |
| Recovery | `settings.cv`, `recovery.cv` | Try a backup, translate failures, and choose a default |
| Callbacks | `callbacks.cv` | Adapt functions, stored views, and captured closures to one failure contract |
| Payloads | `payloads.cv` | Carry owned and borrowed text through guards, rethrow, and recovery |

## Introduction

Start here for a minimal `throw` → `?` → `catch` example. If these forms are
already familiar, continue with composition and evaluation.

`reserve` returns the remaining seats or throws `SoldOut` with the requested
quantity. Its signature declares that failure. `report` uses `?` to propagate
it to the enclosing `try`, then handles its payload. The two requests are
independent and use positive inputs; no booking state is retained.

Try a different request above the available count. Remove `?` to see the
compiler reject consumption of a result with pending failures.

## Composition and evaluation

`orders.cv` contains independent quantity, stock, and delivery rules. Quantity
failures use an enum; stock failures retain requested and available counts in a
record. These types need no shared base type. Their failure sets combine when
private `primary_quote` calls both providers:

```carven
private fn primary_quote(quantity: i32, available: i32, zone: i32) -> i32 =>
    (line_total(quantity, available) + delivery_fee(zone))?;
```

One `?` propagates the composite expression's failures. `primary_quote` infers
its failure set. The shared `quote` interface explicitly declares the permitted
outward types, including failures from its recovery policy. The declaration is
an upper bound; a particular call need not produce every listed failure.
Member order does not change a failure set's meaning.

`quote` uses a guarded handler to select an alternate carrier. Zone 2 has an
alternate, zone 3 does not, and zone 4 makes the lookup itself fail. That guard
failure goes to the caller; it does not enter the remaining arms of the same
`try`. Other failures use `rethrow`, preserving the selected type and payload.
A failure in the alternate calculation also goes outward.

`composition.cv` handles the published contract. Quantity alternatives share a
payload binding, delivery cases have separate handlers, and a carrier lookup
failure has its own type. Handler order determines which matching policy runs.

The final calls make evaluation observable:

- `report_sequence` records stock evaluation as 1 and delivery as 2. Success
  produces trace 12. Stock failure produces trace 1: delivery is skipped, and
  the earlier trace mutation remains. The local handler substitutes -1 for
  the failed total solely to print the outcome.
- `report_short_circuit` puts a fallible call on the right of `&&`. When delivery
  is not needed, the call is skipped and the trace stays 0. Otherwise it becomes
  2. One `?` applies to the whole boolean expression.

The quote uses a fixed price of 250 cents, quantities in 1..100, and fixed
shipping fees. Quantity checks precede multiplication. It reads availability;
no stock reservation, payment, or rollback is performed.

### Explore the contract

Make one edit at a time, rebuild, and restore it before continuing:

- Remove the `throw` clause from shared `quote`. A published nonempty contract
  must be explicit, while its private helper can infer.
- Remove `CarrierLookupError` from that clause. Failures from the guard must
  also fit the interface. Removing `DeliveryError` likewise excludes a failure
  that the body can propagate.
- Remove the unknown-zone handler in `composition.cv`. The remaining arms do
  not cover every declared failure case. A guard that can reject also needs
  remaining coverage.
- Remove `?` from the sum in `primary_quote`. Its pending failures cannot be
  consumed as an ordinary completed i32.
- Change `report_sequence(6)` to `report_sequence(5)`. Both calls now execute,
  producing trace 12. Change the delivery-needed input to observe short-circuiting.

A wildcard handler is valid when the application intends one policy for all
remaining failures. Explicit cases make each decision visible here.

## Recovery across an interface

`settings.cv` parses decimal ASCII ports in 1..65535. Missing text is a separate
failure type from invalid text. Bad digits carry a zero-based byte position;
out-of-range values use a nullary enum case. The parser checks before multiplying
to avoid overflow, accepts leading zeros, and rejects whitespace and signs.

Follow the value through three functions:

1. Private `select_port` infers its failures. Missing primary text selects the
   backup; invalid primary text is rethrown. A backup failure originates inside
   a handler and propagates outward without retrying that handler.
2. Shared `configured_port` translates both parser failures into `ConfigError`.
   `Invalid` retains the original `InvalidPort` payload. Callers depend on this
   declared domain contract.
3. `port_or_default` uses value-form `try` to recover missing configuration as
   8080, while preserving invalid configuration through `rethrow`.

`recovery.cv` handles the result with nested payload patterns. The interface
still declares the type `ConfigError`, so the caller covers its complete enum,
including `Missing`. Recovering one enum case does not narrow the published
contract to a subset of that enum's cases.

### Explore the policy

Use `"9x"` as backup with an empty primary: the outer handler receives byte
position 1. Use two empty inputs to select the default. Invalid nonempty primary
text does not select a valid backup. Inputs are passed directly; no environment
variables or files are read.

Change the default handler to `ConfigError(_) => 8080`. This is statically valid,
but now hides invalid configuration too. The compiler checks coverage and
propagation; the application chooses the recovery policy.

## Callbacks and contract widening

`process` accepts a callable view with `InvalidAmount + LimitExceeded` and
propagates its failures. `positive` declares only `InvalidAmount`, so it fits
this interface. `run_callbacks` also widens a stored narrow view into a wider
view; the original view remains alive throughout its use.

The captured `bounded` closure calls `positive` and may additionally throw
`LimitExceeded`. Its failure set is inferred. Constructing the closure does not
execute its body or produce those failures. The typed `policy` view borrows the
named closure, which remains alive for all three synchronous calls.

`report` handles both types in the view's contract even when the selected
function can produce only one. Widening changes the admitted failure set while
preserving payload types.

Change the limit to observe a different policy. Then narrow the local `policy`
view to `throw InvalidAmount` while retaining the closure body: its possible
`LimitExceeded` no longer fits. Restore the wider contract before running checks.

## Payload ownership and borrowing

`payloads.cv` uses nominal records with `String` and `str` fields:

- `owned_failure` throws an owning record by ordinary copy. A guard rejects it,
  `rethrow` passes the original failure outward, and the outer handler recovers
  its text. The payload remains valid after the throwing function returns.
- `taken_failure` uses `throw &&error` to request explicit Take of the owning
  record. The caller recovers its message.
- `borrowed_failure` carries a view into the caller's String. `forward_failure`
  rejects the empty-message guard and rethrows the borrowed payload. The outer
  handler creates an owning copy. Once handling finishes, the caller clears its
  source; the saved message remains unchanged.

The original failure payload retains its backing during catch selection,
guards, and rethrow. Pattern bindings do not end that obligation. These payloads
are copyable nominal values; this example does not require move-only failure types.

### Explore the lifetime checks

In the borrowed handler, try `source.clear()` before copying `error.message`.
The original failure still borrows the source, so the mutation conflicts with
that borrow. Alternatively, change `borrowed_failure` to throw a view into a
String created locally in that function: the view cannot outlive its backing.
Restore the valid program before running output checks.

## Run

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-failures
./xmakew run carven-example-failures
./xmakew test -g examples
```

In PowerShell, use `.\xmakew.ps1`. Expected output:

```text
Basic failures
Seats remaining: 3
Not enough seats for: 6
Failure composition
Standard delivery
Quote in cents: 800
Alternate carrier
Quote in cents: 1200
Stock shortage
Requested: 6
Available: 5
Invalid quantity
Quantity must be between 1 and 100: 0
Oversized order
Quantity must be between 1 and 100: 101
Unknown destination
Unknown zone: 9
Unavailable destination
No carrier for zone: 3
Recovery lookup fails
Carrier lookup failed for zone: 4
Expression total: 800
Evaluation trace: 12
Expression total: -1
Evaluation trace: 1
Delivery affordable: 0
Delivery trace: 0
Delivery affordable: 1
Delivery trace: 2
Recovery
Primary setting
Port: 443
Backup setting
Port: 9000
Built-in default
Port: 8080
Invalid primary is not hidden
Bad digit at byte: 1
Invalid backup propagates
Port must be between 1 and 65535
Zero is not a port
Port must be between 1 and 65535
Callbacks
Basic policy
Accepted: 8
Rejected by basic policy
Amount must be positive: 0
Widened stored view
Accepted: 3
Captured policy
Accepted: 4
Rejected by captured policy
Requested: 8
Policy limit: 5
Invalid input through captured policy
Amount must be positive: -2
Failure payloads
Owned detail: request rejected
Taken detail: taken detail
Saved detail: borrowed detail
Source bytes after clear: 0
```

Restore documented inputs and contracts after each experiment. The output check
covers these concrete runs; compiler rejection cases remain in the test suites.
All failure handling here is implemented in Carven. Native exceptions require
handling at the C++ boundary, as shown by the `call_cpp` example.
