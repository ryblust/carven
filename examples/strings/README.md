# Owned text

Read `main.cv` from its console import to the `greeting` function, then `main`.
`greeting` returns an owning `String` from `f"Hello, {name}!"`.

`main` first constructs the message and saves an independent copy. It borrows
`.as_str()` inside a scope for console output. That view ends before `clear`
modifies the owner. `append` adds the replacement text; the saved copy retains
the greeting. The final interpolation uses `04x` to format a hexadecimal ID.

String needs no import. Length counts UTF-8 bytes; `.chars` iterates scalars
and `.bytes` iterates bytes.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-strings
./xmakew run carven-example-strings
```

Expected output:

```text
Hello, 世界!
Goodbye!
Hello, 世界!
Bytes: 14, ID: 002a
```

Change the greeting text and observe the byte count. The console helper takes
`str`, so the example explicitly borrows each String with `.as_str()`.
