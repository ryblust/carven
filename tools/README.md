# Carven tools

| Directory | Responsibility | Xmake targets |
| --- | --- | --- |
| `format/` | Source formatting and the `carven-format` command | `carven-format`, `carven-format-modules` |
| `workspace/` | Document snapshots, cached analysis, and navigation queries | `carven-workspace-modules` |

The module targets provide reusable components and depend on `carven-modules`.
Command entry points consume these components. Workspace APIs accept document
updates and return versioned query results; callers own transport and scheduling.

Build `carven-format` and test the tools from the repository root:

```shell
./xmakew build carven-format
./xmakew test -g format
./xmakew test -g workspace
./xmakew run carven-format input.cv
```

On Windows, use `.\xmakew.ps1` with the same arguments.
