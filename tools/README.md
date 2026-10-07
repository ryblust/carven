# Carven tools

- [`carven-format`](formatter/README.md) formats Carven source using the compiler lexer and parser.
- [Workspace analysis](workspace/README.md) provides snapshots and cached source queries.

Build and test the formatter from the repository root:

```shell
./xmakew build carven-format
./xmakew test -g formatter
./xmakew run carven-format input.cv
```

On Windows, use `.\xmakew.ps1` with the same arguments.
