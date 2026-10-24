# Toolchain distribution

Package the compiler, formatter, LSP, analyzer, Crafts, Xmake and CMake integration,
hosting headers and static library, and required runtime libraries as one toolchain.
The VS Code extension is a separate universal VSIX.

These scripts run on the publisher's machine or CI after `xmake install` stages
the files. They assemble the archive and checksum; `install.sh` and `install.ps1`
download and install that finished archive without Node or build tools.

## Build

Use Xmake, LLVM/Clang with libc++, Node.js 24.12 or newer, and pnpm. Linux packaging
also requires `patchelf`. On Windows, use LLVM-MinGW and set `LLVM_MINGW_ROOT` to
its directory; run the following Xmake commands with `.\xmakew.ps1`.

```sh
./xmakew f -y -m release --sanitizers=n
./xmakew build
./xmakew build -g toolchain
./xmakew install -o "$PWD/build/toolchain" -g toolchain
```

## Package

```sh
pnpm --dir editors/vscode install --frozen-lockfile
node tools/release/package.mjs "$PWD/build/toolchain"
pnpm --dir editors/vscode run package
```

`build/packages` contains `carven-<platform>-<arch>.tar.gz`, its
`.sha256` sidecar, and `carven-vscode-<extension-version>.vsix`. Packaging checks the
toolchain files and includes Linux/Windows runtime libraries and license notices.
macOS uses its system C++ runtime.

## Validate

Install CMake 3.21 or newer and Ninja for the external CMake project check.

```sh
pnpm --dir editors/vscode run check:tests
node editors/vscode/tests/installation.ts
```

The package check extracts the archive into a temporary directory and runs a
real build, project query, Xmake and CMake incremental builds, and an LSP session
with its `bin` on `PATH`. Both external projects explicitly load the installed
adapters and check unchanged builds and edits to imported sources.
It also compiles a C++23 host against the installed SDK to check embedding and reload.
Query and LSP startup use only the package and system utilities; native builds
also use the development C++ toolchain.

Run the [editor contracts](../../editors/vscode/development.md#automated-checks) separately
against the built or staged toolchain.

Set `CARVEN_TEST_VSCODE=1` to include the packaged VSIX in a temporary VS Code
profile. On Windows, also set `VSCODE_EXECUTABLE` to the absolute `Code.exe` path.

## Publish

The manually triggered distribution workflow builds and validates native
macOS, Linux and Windows packages, then uploads GitHub Actions artifacts.
Create the GitHub Release for the compiler's `vX.Y.Z` tag manually and attach
the validated platform archives, their checksums, one universal VSIX,
`install.sh` and `install.ps1`. The installers download those release assets;
the workflow does not publish a release or Marketplace extension.
