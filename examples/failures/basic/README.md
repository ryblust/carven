# Handle a sold-out booking

Read the failure type and helper functions in `main.cv`. `reserve` either returns the remaining seats or produces a `SoldOut`
value containing the requested quantity. Its signature declares that failure.

`report` uses `?` to propagate the pending failure to its `try`, whose handler
reads the payload. `main` makes two independent requests against five available
seats. This example does not retain a booking database.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-booking
./xmakew run carven-example-booking
```

Expected output:

```text
Seats remaining: 3
Not enough seats for: 6
```

Inputs here are positive. Try another request larger than the available count.
