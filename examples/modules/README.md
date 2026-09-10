# Share a shipping calculation

Read `rates.cv` first. Its private `base_cents` constant precedes
`shipping_cents`, which uses it to calculate a price. The constant is local to
that module; the bare function is available within the module domain.

Then read `main.cv`. Its `.rates` import selects `shipping_cents` from the
adjacent logical module, and the console import supplies `print`. `main` calls
the calculation and prints the result.

The build supplies both source files to Carven. Imports resolve within that
explicit batch; they do not discover source files on disk.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-shipping
./xmakew run carven-example-shipping
```

Expected output:

```text
Shipping in cents:
500
```

Change the base price and rebuild to see the caller use the new calculation.
