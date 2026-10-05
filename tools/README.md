# Carven tools

- [`carven-format`](formatter/README.md) formats Carven source using the compiler lexer and parser.
- [Workspace analysis](workspace/README.md) provides snapshots and cached source queries.
- [`carven-analyzer`](analyzer/README.md) retains document inputs and serves queries in a resident process.
- `lsp/` contains the `carven-lsp` placeholder and is not integrated into the build.

Build and test the formatter from the repository root:

```shell
./xmakew build carven-format
./xmakew test -g formatter
./xmakew run carven-format input.cv
```

On Windows, use `.\xmakew.ps1` with the same arguments.
