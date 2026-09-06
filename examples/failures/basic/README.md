# Handle a sold-out booking

```sh
./xmakew build example-booking
./xmakew run example-booking
```

Run from the repository root after [building the compiler](../../README.md).

```text
Seats remaining:
3
Not enough seats for:
6
```

`reserve` either returns the remaining seats or produces a `SoldOut` value
containing the requested quantity. Its signature declares that failure.
`report` uses `?` to propagate the pending failure to its `try`, whose handler
reads the payload. Both calls in `main` are independent requests against five
available seats; this example does not retain a booking database.

Inputs here are positive. Try another request larger than the available count.
Carven failures follow [failure contracts](../../../docs/semantics.md#failure-contracts);
they are not native C++ exceptions.

Next: [Order quote](../composition/), which combines failures from multiple
modules. See the [full failure-contract series](../README.md).
