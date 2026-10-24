# Carven for VS Code

Language support for Carven: diagnostics, completion, hover, navigation, rename,
semantic highlighting, formatting and quick fixes.

## Get started

1. [Install the Carven toolchain](https://github.com/ryblust/carven#install)
   and add its commands to `PATH` before starting VS Code.
2. Install `carven-vscode-0.1.0.vsix` with **Extensions: Install from VSIX**.
3. Open a trusted project folder and a `.cv` file.

Requires VS Code 1.142 or newer. The extension starts `carven-lsp` from `PATH`;
VS Code provides its JavaScript runtime, so no separate Node installation is needed.
For remote workspaces, install the toolchain on the remote host.

## Use

Use the usual VS Code actions: **Go to Definition**, **Find All References**,
**Rename Symbol**, **Format Document**, **Quick Fix** and **Organize Imports**.
The Carven status item shows the active project and any configuration errors.

Projects use `build.cv` or `xmake.lua`; Xmake projects also require `xmake` on `PATH`.
Set `carven.target` to select a target, or `carven.xmakePath` to select Xmake.
To try a local build, set `carven.serverPath` to the absolute path of `carven-lsp`.

Run **Carven: Reload Project** to refresh project configuration, or
**Carven: Restart Language Server** after updating the toolchain.
