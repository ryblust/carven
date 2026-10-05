# Carven tools

- [Graver](graver/README.md) formats Carven source using the compiler lexer and parser.
- [Editor analysis](editor/README.md) provides snapshots and cached source queries.
- [Analyzer](analyzer/README.md) retains document inputs and serves queries in a resident process.
- `carvend/` contains a language-server placeholder and is not integrated into the build.

Build and test Graver from the repository root:

```shell
./xmakew build graver
./xmakew test -g graver
./xmakew run graver input.cv
```

On Windows, use `.\xmakew.ps1` with the same arguments.
