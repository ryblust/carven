# VS Code development

Run commands from the repository root. Build the compiler first, then the tools:

```sh
./xmakew build
./xmakew build -g toolchain
```

## Try the checkout

In an existing VS Code project window, set `carven.serverPath` in workspace
settings to the built LSP. For a macOS ARM64 Release build:

```json
{
    "carven.serverPath": "/absolute/carven/build/macosx/arm64/release/carven-lsp"
}
```

Saving restarts the server. It uses the sibling compiler and analyzer and this
checkout's Crafts. After rebuilding, run **Carven: Restart Language Server**.
Remove the setting to use `carven-lsp` on `PATH` again. Trying a checkout LSP
requires no Node installation or extension rebuild.

On Windows, use `.\xmakew.ps1` and select `carven-lsp.exe`.

## Develop the extension

Use Node.js 24.12 or newer and pnpm. Install dependencies once, then build:

```sh
pnpm --dir editors/vscode install --frozen-lockfile
pnpm --dir editors/vscode run build
code --extensionDevelopmentPath="$PWD/editors/vscode" /path/to/project
```

Set `carven.serverPath` in that project to select the checkout LSP. After changing
extension code, rebuild it and run **Developer: Reload Window**.

## Automated checks

Use the existing compiler and toolchain build. For a macOS ARM64 Release build:

```sh
export CARVEN_LSP="$PWD/build/macosx/arm64/release/carven-lsp"
export CARVEN_TEST_COMPILER="$(dirname "$CARVEN_LSP")/carven"
export CARVEN_TEST_CRAFTS="$PWD/crafts"
pnpm --dir editors/vscode test
pnpm --dir editors/vscode run test:vscode
```

The first command checks LSP protocol and project behavior; the second checks
VS Code integration in a temporary profile. Failed fixtures retain their files
and logs. On Windows, select the `.exe` paths and set `VSCODE_EXECUTABLE` to
`Code.exe`. Set `CARVEN_XMAKE` to select a specific Xmake executable.

These checks run separately from `./xmakew test`. CI builds and tests the editor
toolchain in its own job; compiler jobs need no Node or pnpm.

```sh
pnpm --dir editors/vscode run check:tests
pnpm --dir editors/vscode run format
```

Prettier preserves blank lines between logical groups.

Distribution acceptance runs `tests/installation.ts` against the packaged
toolchain, external build adapters, and optional VSIX.

## Package

```sh
pnpm --dir editors/vscode run package
```

The VSIX is written to `build/packages/carven-vscode-<version>.vsix`. It contains the
editor integration and language client. To run VS Code checks against a package,
set `CARVEN_VSIX` to that file and `CARVEN_LSP` to the installed server, with the
installed toolchain on `PATH`.
