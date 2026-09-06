# Restock and dispatch inventory

```sh
./xmakew build example-inventory
./xmakew run example-inventory
```

Run from the repository root after [building the compiler](../README.md).

```text
Snapshot:
7
Dispatched:
9
Replacement stock:
1
```

Follow the same `stock` binding through `main.cv`:

- `restock(&stock, ...)` grants Write access to the existing stock.
- `let snapshot = stock` creates a separate record value.
- `dispatch(&&stock)` transfers ownership to the function.
- A complete assignment initializes `stock` again after the transfer.

The snapshot stays at 7 after the original reaches 9. This record contains only
an integer; copying an external C++ handle can instead preserve an alias to
external storage. See [ownership semantics](../../docs/semantics.md#value-ownership-and-lifetime).

Try reading `stock.units` immediately after dispatch and before reassignment;
Carven rejects use of the transferred owner. Restore the program to run it.

Next: [Shipping](../modules/).
