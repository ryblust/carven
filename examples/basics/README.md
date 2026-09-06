# Calculate a receipt

Read `main.cv`, then run from the repository root:

```sh
./xmakew build example-receipt
./xmakew run example-receipt
```

Build the compiler first as described in [the examples entry](../README.md).

```text
Total in cents:
860
```

`Item` groups price and quantity. The array owns two items, `let` makes the
binding immutable, and `var total` permits accumulation. `subtotal` reads an
item without transferring its ownership. Amounts use integer cents.

Try adding a third item to the array. Its extent is inferred from the literal;
the loop still visits each item. This example uses bounded positive inputs and
does not implement input validation or arbitrary-size financial arithmetic.

Next: [Inventory](../ownership/).
