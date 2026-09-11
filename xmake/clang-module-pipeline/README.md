# Clang module build pipeline

This document describes the repository wrapper's Xmake overlay, module build
rules, incremental checks, and build-state recovery.

## Pipeline

The patched rules use independent BMI and object jobs when
`build.c++.modules.two_phases` is enabled for `clang`, `clang++`, or `clang-cl`.
Both jobs compile the module source after its imported BMIs are ready:

```text
module source -> BMI
module source -> object
```

Clang 23 and newer use `--precompile-reduced-bmi`; earlier versions use
`--precompile`. Object commands use `-x c++` for module-source extensions to
avoid producing an implicit BMI. Ordinary `.cpp` units use their inferred
language.

The leaf pass uses reverse imports from Xmake's P1689 graph. It omits the BMI
for a named, non-interface unit that produces an object and has no importer in
that graph. Interface units, header units, imported units, and BMI-only units
retain their BMIs. Build preparation removes existing BMIs for pruned leaves.
The graph must include all importers for this classification to be valid.

Compilation databases describe separate BMI and object commands for retained
module units and source-to-object commands for pruned leaves. Project generation
uses the same classification without removing existing BMIs.

## Incremental checks

The content-based path applies to `clang` and `clang++` with two-phase module
compilation enabled. The `clang-cl` path uses timestamp-based dependency checks.

For the content-based path, each build batch-scans the target's owned translation
units with `clang-scan-deps`. The scan provides P1689 module dependencies and
textual header dependencies, including system headers. Reused modules retain
their provider's scan ownership and dependency records. The batch shares one
Make dependency output file, with a distinct target key for each translation
unit, because the scanner retains output streams until it exits. Scan records are
published after the complete batch parses successfully. The scanner uses the
configured job count or Xmake's default parallelism.

After prerequisites finish, each BMI and object job compares source, header,
and imported BMI contents, compiler identity, flags, and output contents with
its dependency record. Archive and link jobs compare their recorded inputs,
tool identity, flags, and output contents. Records are saved after successful
execution. Content fingerprints are cached within the current invocation.

These checks detect changes to tracked file contents even when timestamps are
preserved, and rebuild missing or modified outputs. A timestamp change alone
does not require recompilation. BMI and object outputs have separate records.

### Non-cascading changes

Carven enables `build.c++.modules.non_cascading_changes` for its non-Windows
LLVM toolchain when the Xmake runtime contains the content-dependency adapter.
With this policy enabled, the content-based path uses directly imported BMI
contents on Clang 19 and newer. Otherwise, it checks transitive BMI contents.
The full dependency graph and transitive module mappings remain available for
scheduling and compiler lookup.

If `B` imports `A` and `C` imports `B`, a change to `A` may rebuild `B`. When
`B`'s resulting BMI is unchanged, `C` can reuse its outputs if its other inputs
and outputs still match. Each module's object job checks its own inputs
independently. Set the policy to `false` to use transitive BMI checks.

## Usage

Use the repository wrapper for local build commands:

```sh
./xmakew build
./xmakew project -k compile_commands
```

On Windows, use `.\xmakew.ps1` with the same arguments.

For an unexpected compiler, module, or dependency-order failure, clean and
rebuild before diagnosing the implementation:

```sh
./xmakew clean
./xmakew build
```

Clean the build tree before changing the compiler, toolchain, or build pipeline.
When switching from the wrapper to stock Xmake while the wrapper is usable:

```sh
./xmakew clean -a
xmake build
```

If the wrapper cannot apply its patch, use stock Xmake to clean and rebuild:

```sh
xmake clean -a
xmake build
```

## Overlay and compatibility

The patch targets Xmake `v3.1.1+20260827`. The wrapper creates an overlay of the
installed Xmake program directory under the platform temporary directory and
selects it through `XMAKE_PROGRAM_DIR`. Its cache key includes the program
directory, Xmake version, patched Lua files, and patch contents. A patch
application failure stops the wrapper.

The POSIX wrapper attempts an APFS clone on macOS or a reflink on Linux, with a
regular copy as fallback. Windows uses `robocopy`. Each wrapper prepares a
staging directory before publishing the overlay. The POSIX wrapper requires
`patch` and either `shasum` or `sha256sum`; the PowerShell wrapper requires Git
for Windows.

The content-based path requires `clang-scan-deps` from the configured toolchain.
Scan errors stop the build. Generated headers must exist before scanning, for
example through `before_prepare` hooks.

The content-based behavior described here covers local named-module builds.
Header units, PCH, remote execution, and concurrent builds sharing an output
directory are outside this scope.
