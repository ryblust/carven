# Hello World

A top-level statement calls the builtin `println` with an ordinary string literal.
It forms the implicit program entry and writes
`Hello World` followed by a newline. No import is needed.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-hello-world
./xmakew run carven-example-hello-world
```

Expected output:

```text
Hello World
```

The examples test group checks this program's actual output.
