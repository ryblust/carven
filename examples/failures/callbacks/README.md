# Carry failure contracts through callbacks

An admission pipeline accepts a caller-selected policy. One policy checks that
an amount is positive; another also imposes a captured limit. Both fit the same
callable interface, which lists their possible failure types.

## Read main.cv

The console import comes first, followed by the `InvalidAmount` and
`LimitExceeded` records. `positive` declares only `InvalidAmount`.
`process` accepts a callable view whose signature allows `InvalidAmount + LimitExceeded`, and propagates the
selected policy's failure with `?`. A smaller failure set fits that wider view;
no conversion between the failure payload types is needed.

`report` handles both types admitted by the view. That contract applies even
when the particular callback is `positive` and can produce only one of them.
Failure widening admits more possibilities; it does not claim the callback
will produce all of them.

In `main`, calls using `positive` precede the captured policy.
The `bounded` closure captures `limit`, calls `positive`, and can additionally
throw `LimitExceeded`. Its failure set is inferred from its body. Creating the
closure does not run its body or produce either failure. The typed `policy`
view borrows that named closure, which stays alive for all its uses.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-policies
./xmakew run carven-example-policies
```

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

The program invokes the borrowed callback synchronously while its closure
owner remains in scope.
