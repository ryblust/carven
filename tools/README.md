# Carven tools

This directory contains placeholder executables for two tools:

- `graver/`: source formatter.
- `carvend/`: language server.

Each executable prints an unimplemented message to stderr and exits with status
`1`. Both depend on `carven-modules` and are excluded from default builds.

Build and run explicitly from the repository root:

```shell
./xmakew build graver
./xmakew run graver

./xmakew build carvend
./xmakew run carvend
```

On Windows, use `.\xmakew.ps1` with the same arguments.
