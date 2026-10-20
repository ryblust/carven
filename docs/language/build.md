# Project builds

This page defines build plans, native targets, and the compilation API.

## Scripts and commands

`carven build main.cv` builds `build/main` (`build/main.exe` on Windows).
`carven run main.cv -- arguments...` builds and runs it. These commands form a
single executable target named after the source filename; no script is required.

Without a source argument, `carven build` and `carven run [target]` execute the
current directory's `build.cv`. A script is an ordinary Carven program. It can
also be executed as `carven interpret foo.cv`, regardless of its filename.

A simple project supplies its configuration to the standard build function:

```carven
import std::build using build;

build({
    targets: [{
        name: "hello",
        kind: .Executable,
        sources: ["src/main.cv"],
        ...
    }],
    ...
})?;
```

The trailing `...` explicitly default-initializes omitted record fields. It
applies to every omitted field, independently of declaration order. Target kind
is supplied explicitly because `TargetKind` has no default variant.

## Build data and operations

`build(plan: BuildPlan) -> Vector<BuildOutput> throw BuildError` builds the plan
and returns its output records. Under `carven run`, it also selects and runs an
executable before returning. Under `carven build` or `carven interpret`, it only
builds. Each call finishes before the next program statement; multiple calls
under `carven run` each build and run their own selected executable.

Arguments after `--` pass to the selected program unchanged. A missing target
name selects the only executable output; static libraries do not participate in
selection. Multiple executable outputs require a name. Failed builds never
start a program. A nonzero child exit raises `BuildError` with the exit code in
its message.

| Record | Fields |
| --- | --- |
| `BuildPlan` | `targets: Vector<BuildTarget>`, `shared_sources: Vector<String>`, `output_directory: String` |
| `BuildTarget` | `name: String`, `kind: TargetKind`, `sources: Vector<String>`, `native_sources: Vector<String>`, `native: NativeOptions`, `dependencies: Vector<String>`, `output: String` |
| `NativeOptions` | `include_dirs: Vector<String>`, `defines: Vector<String>`, `compile_flags: Vector<String>`, `link_flags: Vector<String>`, `library_dirs: Vector<String>`, `libraries: Vector<LinkLibrary>` |
| `BuildOutput` | `name: String`, `path: String`, `kind: TargetKind` |

`TargetKind` has `Executable` and `StaticLibrary` variants. A target's
`dependencies` name other targets in the plan. Dependencies must exist and form
an acyclic graph. Executable targets cannot be library dependencies.

`shared_sources` join every target's Carven source roots. Project Craft sources
under `crafts/` also join those roots; their native providers compile alongside
generated implementations. `native_sources` selects additional C++ source files.
Relative input paths resolve from the current directory.

Native include directories, defines, and compile flags apply to the target's
objects; they do not propagate to consumers. Reuse the same `NativeOptions` value
or construct options with ordinary functions when targets share configuration.
Link flags, library directories, and libraries on a static-library dependency
participate in its consumers' executable link. Libraries distinguish linker names
(`LinkLibrary::Name("m")`) from file paths
(`LinkLibrary::File("thirdparty/libsample.a")`). Dependency libraries precede
the external libraries that resolve their references.

An empty `output_directory` selects `build`. An empty target `output` uses the
platform's executable or static-library filename for its target name. An explicit
relative output resolves from the plan's output directory; an absolute
output selects that path directly. Target names contain
ASCII letters, digits, `_`, or `-`, and are distinct ignoring ASCII case.
A plan needs at least one target; each target needs source input or a library
dependency. Target names, dependency references and cycles, source presence, and
duplicate output paths are checked before C++ generation.

## Custom preparation

Plans are ordinary values. Functions, conditions, loops, and containers can
assemble them. Filesystem operations and external tools run through their
standard-library APIs before construction or execution of the plan:

```carven
import std::build using build;
import std::process using run;

let status = run("python3", ["tools/generate.py"])?;
assert(status == 0);

build({
    targets: [{
        name: "app",
        kind: .Executable,
        sources: ["src/main.cv", "build/generated/schema.cv"],
        native: { include_dirs: ["thirdparty/include"], compile_flags: ["-O2"], ... },
        ...
    }],
    ...
})?;
```

Preparation executes in program order on every invocation. The script controls
generator invocation and exit-status handling. Build errors use ordinary failure
handling:

```carven
import std::build using { build, BuildError }

try {
    let outputs = build(plan)?;
    for output in outputs { println(output.path); }
} catch {
    BuildError(error) => println(error.message),
}
```

## Compilation API

`std::compiler.compile(sources: [String], destination: str) -> Compilation throw
CompilerError` analyzes a source program and writes its C++ artifacts. It uses
the host's matching Crafts resources. Each call executes that program's static
roots. An empty source list returns the native resource information without
source analysis or artifact writes. Diagnostics and static output use the host's
standard streams.

`Compilation` owns `cacheable: bool`,
`implementations: Vector<CompiledImplementation>`, `include_dirs: Vector<String>`,
and `native_providers: Vector<String>`. The latter two expose the matching
compiler resources: include roots and candidate native providers. C++ header
dependencies select the providers needed by the program. Each implementation owns its
generated `file: String`, canonical `source: String`, and
`dependencies: Vector<String>` of implementation paths. File and dependency paths
include the destination. `cacheable` reports whether analysis permits reuse;
callers still validate native inputs and outputs. `CompilerError` owns a
diagnostic `message`.

The compiler supplies this operation to static and interpreted execution. A native
program using it must link the implementation declared by
`carven/std/compiler/native.hpp` and provide matching resources in the `crafts/`
directory beside its binary directory.

## Native toolchain and execution

`CXX` selects one native compiler executable; otherwise the host finds `clang++`
on `PATH`. Names and paths containing spaces are supported. Flags belong in
`NativeOptions`, not `CXX`. The compiler, SDK, linker, and archive tool must be
available in the environment. Native arguments follow GCC/Clang conventions with
C++20 as the baseline. `CXXFLAGS` and `LDFLAGS` are not imported.

The builder uses the compiler's matching Crafts resources for runtime headers
and native providers.

The builder analyzes the union of Carven roots once, then selects each target's
implementation closure. Generated implementation edges and native providers
selected from C++ dependency files join the same graph. Type-only dependencies
supply headers without selecting an implementation object. A library dependency
supplies its implementation objects through its archive.

Objects with the same source and native configuration are shared. Distinct
configurations have distinct object identities. Native object records cover the
command, compiler identity, relevant environment, source and header contents, and
the output itself. Adding or removing files in native header search inputs also
changes their reuse identity. Missing or changed inputs cause recompilation. Each validation
batch reads a shared dependency once; fingerprints do not survive the call.
Analysis that observes uncached host effects prevents native object reuse for
that invocation.

Generated C++, objects, and dependency records live below the output directory's
`.carven/` directory. Remove the output directory to rebuild from scratch. Build
calls using the same output directory run serially. Compilation uses
`std::process.available_parallelism` as its process limit. Failed compilation
stops dispatch; in-flight children are waited for before returning. Archives and
executables are produced in dependency order after compilation succeeds. Earlier
successful outputs remain on disk if a later operation fails.

`BuildError` owns a diagnostic `message`. Configuration, filesystem, compilation,
and child-process failures stop the operation. Language and hosted execution
errors also make the command unsuccessful.
