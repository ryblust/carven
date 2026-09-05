# Clang module build pipeline

This wrapper runs Carven with a versioned Xmake overlay and selects the pipeline
for the configured Clang driver and version. `clang`, `clang++`, and `clang-cl`
use the same internal pipeline; only the repository-wrapper command differs
between POSIX shells and PowerShell. The complete pipeline combines three
module-build capabilities:

- Reduced BMI generation when the driver supports `--precompile-reduced-bmi`
  (LLVM 23 and newer).
- Independent BMI and object jobs compiled from the same module source.
- Leaf BMI elimination for object-producing leaf units.

## Pipeline

After a unit's imported BMIs are ready, Xmake schedules its BMI and object in
the same jobgraph stage:

```text
module source -> Reduced BMI  (--precompile-reduced-bmi, when supported)
              -> Full BMI     (--precompile, earlier supported Clang)
module source -> object       (-x c++ for module-source extensions)
```

The object job reads the module source rather than the unit's own BMI. The two
jobs can therefore run concurrently while retaining the dependency edges from
imported BMIs to their consumers.

An object-producing unit with no importer uses the shorter leaf pipeline:

```text
module source -> object
```

## Usage

Use the repository wrapper for normal project commands. Pipeline selection
happens inside the patched Xmake rules:

```sh
./xmakew build
./xmakew test
./xmakew project -k compile_commands
```

```powershell
# Native Windows PowerShell
.\xmakew.ps1 build
.\xmakew.ps1 test
.\xmakew.ps1 project -k compile_commands
```

Project generators use the same module-unit classification as the build
pipeline. Compilation databases therefore contain separate Reduced BMI and
object commands for retained module units, while object-only leaf units emit
only their source-to-object command. Module-source extensions such as `.cppm`
use `-x c++` for the object command so Clang does not produce a second implicit
BMI; ordinary `.cpp` units rely on their inferred language and remain
deduplicable by Xmake's compilation-database generator. Project generation only
describes the pipeline and does not remove existing BMI artifacts.

If an unexpected compiler, module, BMI, dependency-order, or apparently
impossible type error occurs, clean and rebuild with the repository wrapper:

```sh
./xmakew clean
./xmakew build
```

If the wrapper reports that its versioned patch does not match the installed
Xmake, run the same command with stock Xmake, for example `xmake build`. Before
switching pipelines in an existing build tree, clean through the pipeline that
produced its current artifacts.

The commands build `carven-modules` as a `moduleonly` target. The same clean
rule applies before changing the configured compiler or toolchain.

## Carven leaf model

The pass computes reverse imports from Xmake's scanned P1689 graph. It keeps
interface units, imported units, and BMI-only units. Carven's source convention
defines the object-only leaves:

- `.cppm` contract partitions own declarations, templates, and other state
  shared through imports.
- `.cpp` internal module partitions named `carven:*.impl` contain out-of-line
  definitions and translation-unit-local helpers. They produce objects and are
  not imported.

A pruned leaf retains its object and every dependency on the BMIs that it
imports. The same pruning model applies to projects with an equivalent
object-only leaf invariant and a P1689 graph containing all permitted importers.

Each pruned leaf removes one PCM while retaining its object. The saving depends
on the number and size of leaf BMIs. Carven has an internal module partition for
each out-of-line implementation slice, so these BMIs account for a substantial
part of its build tree, especially in debug builds.

## Overlay and compatibility

The wrapper creates a content-addressed Xmake program-directory overlay under
the platform temporary directory and selects it through `XMAKE_PROGRAM_DIR`.
The cache key includes the Xmake program directory, Xmake version, patched
Xmake Lua files, and this patch.

On macOS, the wrapper first attempts an APFS copy-on-write clone. On Linux it
requests a reflink when the local `cp` supports one. Both platforms fall back to
a regular recursive copy. Windows uses `robocopy`, which attempts block cloning
when the source and cache are on a supported ReFS volume and otherwise performs
a regular copy. A copy-on-write clone creates the complete logical directory
tree while initially sharing file data with the installed Xmake; only files
changed by the patch allocate new data blocks. Publishing the completed overlay
is safe when multiple wrapper processes start concurrently.

The PowerShell wrapper requires Git for Windows to apply the patch. The POSIX
wrapper requires `patch` and either `shasum` or `sha256sum`.

The patch targets Xmake `v3.1.1+20260827`. If the installed Xmake files no
longer match, the wrapper stops when applying the patch. LLVM/Clang 23 and newer
use the Reduced BMI branch; earlier supported Clang versions use Full BMIs while
keeping the independent object pipeline and leaf BMI elimination.
