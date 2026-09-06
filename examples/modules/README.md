# Share a shipping calculation

```sh
./xmakew build example-shipping
./xmakew run example-shipping
```

Run from the repository root after [building the compiler](../README.md).

```text
Shipping in cents:
500
```

Start in `main.cv`, then follow `.rates` to `rates.cv`. The relative import
selects `shipping_cents` from the adjacent logical module. The bare function is
available within the compilation's module domain; the `private` base price is
local to `rates.cv`.

The build supplies both source files to Carven. Imports resolve within that
explicit batch; they do not discover source files on disk. Change the base
price and rebuild to see the caller use the new calculation.

Next: [Booking](../failures/basic/).
