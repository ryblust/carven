# Calculate a receipt

This single-file program introduces records, enums, functions, arrays, and loops
through a receipt with per-line discounts. Read `main.cv` in source order.

## Read the program

`Discount` represents either no discount or a percentage. `Item` groups a name,
unit price, quantity, and discount. Names are string literals; amounts use integer
cents.

`subtotal` calculates the undiscounted line amount. `savings` uses `match` to
handle both discount cases and bind the percentage payload. Both functions read
an item without transferring ownership and return ordinary integer results.

The top-level statements prepare a fixed array and print each line before the
receipt totals.
`let` keeps the items binding immutable; `var` permits the two accumulators to
change. The array length is inferred from its literal, and the loop visits every
item. The two lines exercise both discount cases.

## Run

From the repository root:

```sh
./xmakew build
./xmakew run carven examples/basics/main.cv
```

In PowerShell, use `.\xmakew.ps1`. Expected output:

```text
Notebook x 2
  Subtotal in cents: 500
  Discount in cents: 0
  Line total in cents: 500
Pencil x 3
  Subtotal in cents: 360
  Discount in cents: 36
  Line total in cents: 324
Subtotal in cents: 860
Savings in cents: 36
Total in cents: 824
```

## Try a change

- Add a third item. The array length and loop adapt to the new entry.
- Change the pencil discount to `.None`. Savings become zero and the
  receipt total becomes 860 cents.
- Change it to `.Percent(25)`. Savings become 90 cents and the total
  becomes 770 cents.
- Remove a `match` arm to see the compiler report incomplete enum coverage.

Restore the original inputs before running `./xmakew test -g examples`.
The example assumes small positive prices and quantities and percentages in
0..100. Integer division truncates fractional-cent discounts toward zero.
Input validation, taxes, and general-purpose financial arithmetic are outside
this program's scope.
