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

Each build checks the target's owned translation units against Xmake dependency
records using `depend.load` and `depend.is_changed`. The adapter stores content
fingerprints in the records' `values`. Scan inputs include source and recorded
textual headers, compiler and scanner binaries, scan flags, working directory,
output path and include environment variables. Missing or unreadable records,
changed scan values and rebuild mode require rescanning.

Units requiring a scan enter one `clang-scan-deps` batch per target. When every
owned unit has a reusable record, no scanner process is launched; input checks
still run. The scanner produces P1689 module facts and textual header
dependencies, including system headers. It retains dependency output streams
until exit, so the batch shares one stream with a distinct target key per unit.
The batch runs with the configured job count or Xmake's default parallelism.
Updated records are published after the entire batch parses successfully, using
stable serialization and writes on content changes. They are merged with cached
records for unchanged units. The in-memory record set contains the current owned
units; consumers read reused units from their provider.

Depfiles list headers read during preprocessing; failed include lookups are not
recorded. Include lookup checks fingerprint recursive file-name listings for
source directories, explicit include/framework search roots, include environment
paths and project-local textual-header directories. Listings are shared within each
invocation. This detects file-name changes that can affect header shadowing or
`__has_include` results. Adding, removing or renaming files invalidates units
using those roots. A project-root search directory also includes build and cache
files, whose creation can trigger another scan. The scan record is a compilation
input, so changed lookup results also invalidate compilation.

File-name changes in implicit SDK/toolchain search trees or other locations
outside the tracked roots require a clean build. Contents of recorded headers in
those locations are checked. Response files, VFS overlays and indirect
include-prefix configurations force scanning and compilation on each invocation;
their records include an invocation token. Changes to these configurations can
affect compilation while leaving the reported file list and module facts
unchanged.

Each target parses its complete current record set in one task, after its own
scan and the scans of reused providers. Decoded P1689 facts are shared in memory;
target-specific module paths are derived separately. The module DAG and build
jobs are reconstructed on each invocation, with a module-name index for edge
lookup. Provider graph publication precedes consumer graph construction. Disk
records also serve project generation.

After prerequisites finish, each BMI and object job compares source, header,
scan-record and imported BMI contents, compiler identity, flags, and output
contents with its dependency record. Archive and link jobs compare their recorded inputs,
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
