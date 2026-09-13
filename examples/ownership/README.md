# Restock and dispatch inventory

Read `main.cv` in source order: the `Stock` record,
`restock` with a Write parameter, and `dispatch` with a Take parameter. Then
follow the `stock` binding through `main`:

- `restock(&stock, ...)` changes the existing stock from 4 to 7.
- `let snapshot = stock` creates a separate record value.
- The second restock changes the original to 9; the snapshot remains 7.
- `dispatch(&&stock)` transfers ownership to the function.
- A complete assignment restores `stock` with one unit.

This record contains only an integer. Copies of values containing non-owning
handles can retain aliases to external storage.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-inventory
./xmakew run carven-example-inventory
```

Expected output:

```text
Snapshot: 7
Dispatched: 9
Replacement stock: 1
```

Try reading `stock.units` immediately after dispatch and before reassignment;
Carven rejects use of the transferred owner. Restore the program to run it.
