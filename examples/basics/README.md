# Calculate a receipt

Read `main.cv` in source order. The console import supplies `print`. `Item`
groups price and quantity, and `subtotal` reads an item without transferring
ownership. Amounts use integer cents.

In `main`, an array owns two items. `let` makes that binding immutable, while
`var total` permits accumulation. The loop sums each subtotal before printing
the result.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-receipt
./xmakew run carven-example-receipt
```

Expected output:

```text
Total in cents:
860
```

Try adding a third item to the array. Its extent is inferred from the literal;
the loop still visits each item. This example uses bounded positive inputs and
does not implement input validation or arbitrary-size financial arithmetic.
