# Carven tools

| Directory | Responsibility | Xmake targets |
| --- | --- | --- |
| `hosting/` | C++23 embedding, sessions, events, and source replacement | `carven-hosting` |
| `format/` | Source formatting and the `carven-format` command | `carven-format`, `carven-format-modules` |
| `workspace/` | Document snapshots, cached analysis, and navigation queries | `carven-workspace-modules` |
| `analyzer/` | Workspace queries over a process protocol | `carven-analyzer` |
| `lsp/` | Language Server Protocol transport and editor operations | `carven-lsp` |
| `release/` | Package an installed toolchain for distribution | — |

The hosting library and tool modules reuse the `carven-modules` compiler core.
CLI, workspace, and analyzer targets do not depend on the Hosting SDK.
Build `carven-hosting` explicitly after the default compiler build when embedding is
needed. Workspace APIs accept document updates and return versioned query results;
callers own transport and scheduling.

Build `carven-format` and test the tools from the repository root:

```shell
./xmakew build carven-format
./xmakew test -g format
./xmakew test -g workspace
./xmakew run carven-format input.cv
```

On Windows, use `.\xmakew.ps1` with the same arguments.
