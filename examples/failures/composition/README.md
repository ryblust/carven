# A quote with precise failure contracts

An order quote combines independent catalog and delivery rules. A caller needs
the amount when it succeeds, enough data to explain rejection, and a selective
recovery policy when the primary carrier is unavailable.

## Read the program

1. [catalog.cv](catalog.cv) validates quantity and stock. Its contract combines
   the `QuantityError` enum with the `OutOfStock` record. The stock failure
   retains requested and available counts.
2. [delivery.cv](delivery.cv) defines a separate failure enum with unknown and
   unavailable destination cases. The small fixed zone table makes each path
   reproducible.
3. [orders.cv](orders.cv) composes the calls. `primary_quote` is private and
   infers their combined failure set. One `?` encloses the sum, so the successful
   path reads as an ordinary calculation. Evaluation is left to right: a catalog
   failure prevents the delivery call on that path.
4. The shared `quote` function states the three-type contract. Its guarded
   handler selects an alternate carrier only for unavailable zone 2. Other
   failures use `rethrow`; they retain their type and payload. A failure from
   the alternate calculation propagates outward.
5. [main.cv](main.cv) handles every declared failure. Enum alternatives share a
   payload binding where appropriate, and the stock handler explains the
   shortage without decoding an integer status or parsing an error string.

The providers do not share a base error type. Composition adds their distinct
failure types to a set. Changing the spelling order of the set does not change
its meaning; changing handler order can change which recovery is selected.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-order-quote
./xmakew run carven-example-order-quote
```

## Output

```text
Standard delivery
Quote in cents:
800
Alternate carrier
Quote in cents:
1200
Stock shortage
Requested:
6
Available:
5
Invalid quantity
Quantity must be between 1 and 100:
0
Oversized order
Quantity must be between 1 and 100:
101
Unknown destination
Unknown zone:
9
Unavailable destination
No carrier for zone:
3
```

Quantities are checked before multiplication and capped at 100. The example
quotes a fixed product at 250 cents per unit, with fixed delivery prices. It
reads stock availability; it does not reserve stock, charge money, or promise
transaction rollback.

## Let the compiler review a change

Make one edit at a time and rebuild; restore it before continuing:

- Remove the `throw` clause from the shared `quote` function. Its nonempty
  failure contract must be published explicitly; the private helper can infer.
- Remove `DeliveryError` from that clause while keeping the body. The body's
  outward failures must fit the declared contract.
- Remove the `DeliveryError(.UnknownZone(zone))` handler from `report`.
  The remaining arms do not cover the declared failure cases.
- Remove `?` from the sum in `primary_quote`. The pending failure cannot be
  returned as an ordinary completed i32.

These checks connect source changes to their callers. Catching a wildcard would
be valid but would deliberately accept a more general recovery policy; the
example lists cases to keep that responsibility visible.
