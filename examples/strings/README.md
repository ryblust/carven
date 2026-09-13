# Owned text

Read the `greeting` function in `main.cv`, then `main`.
`greeting` returns an owning `String` from `f"Hello, {name}!"`.

`main` first constructs the message and saves an independent copy. The annotation
`let view: str = message` borrows a view inside a scope for output. That view ends before `clear`
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

Change the greeting text and observe the byte count. Builtin `println` accepts
both `str` and `String`, including interpolation results.
