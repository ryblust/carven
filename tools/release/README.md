# Toolchain distribution

Package the compiler, formatter, LSP, analyzer, Crafts, Xmake and CMake integration,
hosting headers and static library, and required runtime libraries as one toolchain.
The VS Code extension is a separate universal VSIX.

CI builds, packages, checks, and uploads the Release toolchains. Distribution
collects those artifacts from a successful CI run for the selected commit.
`install.sh` and `install.ps1` install the published archive without Node or build
tools. The commands below also support local packaging.

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
`.sha256` sidecar, and `carven-vscode-<extension-version>.vsix`. Packaging includes
Linux/Windows runtime libraries and license notices.
macOS uses its system C++ runtime.

## Validate

Install CMake 3.21 or newer and Ninja for the external CMake project check.

```sh
node tools/release/check.mjs
```

The package check extracts the archive to `build/installed`, builds and runs one
program through Carven, Xmake, and CMake, and links a C++23 consumer against the
installed SDK. Formatting uses only the package and system utilities on `PATH`;
native builds also use the development C++ toolchain.

CI runs the editor protocol tests once against the extracted package. To run them
locally, follow the [editor test instructions](../../editors/vscode/development.md#automated-checks)
with `CARVEN_LSP`, `CARVEN_TEST_COMPILER`, and `CARVEN_TEST_CRAFTS` set to the
corresponding paths under `build/installed`.

To check the VSIX in a temporary VS Code profile, set `CARVEN_VSIX` to the
package, select the installed LSP with `CARVEN_LSP`, and run
`pnpm --dir editors/vscode run test:vscode`. On Windows, also set
`VSCODE_EXECUTABLE` to the absolute `Code.exe` path.

## Publish

CI uploads one toolchain artifact per Release platform and one artifact containing
the universal VSIX and installers. Source checks and editor tests run in CI;
Distribution does not rebuild or rerun them.

Trigger Distribution on the branch or tag to publish. It selects the latest
successful CI run for that exact commit, downloads its `carven-*` artifacts, and
uploads them together as `carven-distribution`. A missing successful run or
expired artifacts require a new CI run.

Create the GitHub Release for the compiler's `vX.Y.Z` tag manually and attach
the platform archives, their checksums, the universal VSIX, `install.sh`, and
`install.ps1` from that collection. The workflow does not create a Release or
publish to the VS Code Marketplace.
