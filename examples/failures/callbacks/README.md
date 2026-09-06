# Carry failure contracts through callbacks

An admission pipeline accepts a caller-selected policy. One policy checks that
an amount is positive; another also imposes a captured limit. Both fit the same
callable interface without erasing their possible failures into a string.

```sh
./xmakew build example-policies
./xmakew run example-policies
```

Run from the repository root after [building the compiler](../../README.md).

## Read main.cv

`positive` declares only `InvalidAmount`. `process` accepts a callable view
whose signature allows `InvalidAmount + LimitExceeded`, and propagates the
selected policy's failure with `?`. A smaller failure set fits that wider view;
no conversion between the failure payload types is needed.

The `bounded` closure captures `limit`, calls `positive`, and can additionally
throw `LimitExceeded`. Its failure set is inferred from its body. Creating the
closure does not run its body or produce either failure. The typed `policy`
view borrows that named closure, which stays alive for all its uses.

`report` handles both types admitted by the view. That contract applies even
when the particular callback is `positive` and can produce only one of them.
Failure widening admits more possibilities; it does not claim the callback
will produce all of them.

## Output

```text
Basic policy
Accepted:
8
Rejected by basic policy
Amount must be positive:
0
Captured policy
Accepted:
4
Rejected by captured policy
Requested:
8
Policy limit:
5
Invalid input through captured policy
Amount must be positive:
-2
```

## Explore the contract

Change the captured limit and rebuild to change the policy. Then try narrowing
the local `policy` view to `throw InvalidAmount` while keeping the closure body.
The closure's possible `LimitExceeded` cannot fit that view. Restore the wider
contract before running the output checks.

This is a synchronous borrowed callback. The program does not store it in a
service, return it, or promise asynchronous lifetime management. Those concerns
follow the [callable-view contract](../../../docs/semantics.md#signatures-and-views).

Return to the [failure-contract series](../README.md), or examine the
[native C++ adapter](../../interop/importing/) to see the separate native
exception boundary.
