# Project builds

This page defines build plans, native targets, and the compilation API.

## Scripts and commands

`carven run main.cv helper.cv -- arguments...` builds and runs one executable from
the supplied sources. One or more `.cv` files are accepted. The first filename
names the output: `build/main` (`build/main.exe` on Windows). Program entry follows
the ordinary language rules; file order does not select an entry. No script is required.

`carven build` takes no arguments and interprets the current directory's `build.cv`.
`carven run [target]`, `carven test [target]`, and `carven query` also execute that
file. The filename is a command convention. A script is an ordinary
Carven program and can be executed as `carven interpret foo.cv`; its library calls
provide the build behavior.

A simple project supplies its configuration to one project entry:

```carven
import std::build using project;

project({
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

`build(plan: BuildPlan) -> Vector<BuildOutput> throw BuildError` always builds
the plan and returns its output records. It never selects or runs an executable.
Each call finishes before the next program statement, so scripts can inspect
outputs and compose explicit build actions.

`project(plan: BuildPlan) throw BuildError` resolves the same declarations and
selects the command's action:

| Command | Action |
| --- | --- |
| `carven query` | Emit one JSON project description |
| `carven build` or ordinary interpretation | Build the resolved targets |
| `carven run [target]` | Build, then select and run an executable |
| `carven test [target]` | Build and run the selected targets' test blocks |

For `carven run`, arguments after `--` pass to the selected program unchanged. A missing target
name selects the only executable output; static libraries do not participate in
selection. Multiple executable outputs require a name. Failed builds never
start a program. A nonzero child exit raises `BuildError` with the exit code in
its message.

| Record | Fields |
| --- | --- |
| `BuildPlan` | `targets: Vector<BuildTarget>`, `shared_sources: Vector<String>`, `output_directory: String` |
| `BuildTarget` | `name: String`, `kind: TargetKind`, `sources: Vector<String>`, `headers: Vector<String>`, `native: NativeOptions`, `dependencies: Vector<String>`, `output: String` |
| `NativeOptions` | `include_dirs: Vector<String>`, `defines: Vector<String>`, `compile_flags: Vector<String>`, `link_flags: Vector<String>`, `library_dirs: Vector<String>`, `libraries: Vector<LinkLibrary>`, `syslinks: Vector<SystemLibraries>` |
| `BuildOutput` | `name: String`, `path: String`, `kind: TargetKind` |

`TargetKind` has `Executable`, `StaticLibrary`, and `SourceLibrary` variants. A target's
`dependencies` name other targets in the plan. Dependencies must exist and form
an acyclic graph. Executable targets cannot be library dependencies.

A `SourceLibrary` describes sources without producing an archive or object files.
Its sources are compiled by each consuming product with that product's native
configuration. A source library may depend only on other source libraries;
executables and static libraries may consume either library kind. `build` returns
outputs only for executables and static libraries. A plan containing only source
libraries returns an empty output vector without invoking a compiler or creating
build directories. Unconsumed source libraries are not semantic input roots.

Source libraries provide `sources`, `headers`, `dependencies`, and conditional
`native.syslinks`. Other `NativeOptions` fields and `output` are rejected on a
source library. Native compilation settings belong to the consuming product or
the external build system, such as Xmake or CMake.

`sources` accepts both Carven and native C++ source files. `shared_sources` joins
every target's source roots. Project Craft sources under `crafts/` are available
for imports; only the imported closure joins analysis. A Craft's `build.cv`
declares its targets through the same `project(plan)` API. C++ header dependencies
select their implementation targets; other `.cpp` files are not added automatically.
List native C++ inputs alongside `.cv` inputs in `sources`.
Relative input paths resolve from the current directory. Carven sources must
remain inside the project root; native source paths may refer outside it.

A product's native include directories, defines, and compile flags apply to its
objects; compile flags also apply when linking that target's executable. They do
not propagate to consumers. Reuse the same `NativeOptions` value
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
A plan needs at least one target; each product needs source input or a library
dependency. A source library may instead provide only headers. Target names, dependency references and cycles, source presence, and
duplicate output paths are checked before C++ generation.

## Project queries

`carven query` supplies editors and other tools with target names, exact source
membership, and paths to watch. The LSP uses this information to select the same
inputs as the build; projects need no separate editor source list.

`project(plan)` resolves the same declarations for queries and builds, including
dependencies and source/output paths. Its query action does not analyze application
source, generate C++, create build directories, or invoke a native compiler or
linker. Application syntax and type errors therefore do not block discovery.
The build script must still be valid, and invalid plans or missing inputs fail
the query.

`carven query` returns a JSON object with `project` and `scripts` fields.
`project` contains `root`, `targets`, and `roots`; each target includes `name`,
`kind`, `default`, `sources`, `headers`, `syslinks`, and `dependencies`.

`kind` is `source-library`, `static-library`, or `executable`. Paths are absolute.
Each target's `sources` contains its own Carven and native
inputs, including `shared_sources`. `dependencies` names other targets; consumers
follow those edges when selecting a target. `headers` and `syslinks` belong to
that target. All plan targets are default targets.

The LSP filters the selected closure to Carven roots and uses the compiler's
ordinary Craft discovery. Standard sources come from the matching toolchain.
`project.root` defines project-relative module paths. `project.roots` supplies
watch directories; `scripts` lists the loaded source files, including indirect
imports of the build script. Consumers use these inputs to invalidate retained
script analysis. Each query executes the script again to observe external files
and environment values.

A queryable project has one `project(plan)` call and reserves stdout for its JSON
result. Filesystem, process, and explicit `build` calls around `project` execute
in program order, including during queries. Put preparation actions in a separate
script when discovery should not execute them.

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
CompilerError` invokes `carven compile` using the `carven` executable on `PATH`
and reads its `carven/manifest.json` below the destination. The selected compiler
supplies its matching Crafts resources. Source inputs execute their static roots;
checked artifact inputs reuse retained semantics. Diagnostics and static output
inherit the caller's standard streams.
An empty source list writes a resource manifest without analyzing sources or
generating C++, supporting targets that contain only native sources.

`Compilation` owns `cacheable: bool`, `sources: Vector<CompilationSource>`,
`crafts: Vector<CompilationCraft>`, and `include_dirs: Vector<String>`. These
records report compiler facts; build tools decide target selection and execution.

Each source records its canonical `source: String`,
`implementation: Lookup<String>`, and generated `dependencies: Vector<String>`
expressed as source paths. A generated implementation path includes the
destination; a native source implements itself. `Missing` means no implementation
was generated for that source.

Each Craft description has `root: String` and `targets: Vector<CompilationTarget>`.
A target retains its `name`, `kind`, `sources`, `headers`, `syslinks`, and named
`dependencies` from the query. Sharing a source does not merge distinct targets.
The builder resolves these facts into its execution graph. If it selects a
Carven source absent from the generated output, import that source or list it
explicitly in the application's inputs.

A Craft can provide a native interface with an ordinary `build.cv`:

```carven
import std::build using project;

project({
    targets: [{
        name: "network",
        kind: .SourceLibrary,
        sources: ["network.cpp"],
        headers: ["network.hpp"],
        native: {
            syslinks: [{ platform: "windows", libraries: ["ws2_32"] }],
            ...
        },
        ...
    }],
    ...
})?;
```

Craft consumers use source-library declarations and their source-library
dependencies. A real static library or executable is a product of an explicit
build action; the source adapter does not reinterpret it as a source group.
Header matches select source libraries and their dependency closure. A header may
select several source libraries. An explicit source root can select its unique Craft owner; a
source shared by several Craft targets requires an unambiguous header or target
dependency. Generated dependencies reaching another Carven source likewise select
its unique owner. A target's own source edges keep that target's context, so they
do not activate other targets sharing those sources. Only selected targets contribute system libraries. Each
`SystemLibraries` group has `platform: String` (`all`, `windows`, `linux`, or
`macos`) and `libraries: Vector<String>`.

The compiler embeds participating Craft query results in its manifest. Crafts
without a build script remain ordinary Carven libraries. Build declarations in
subdirectories compose through functions and imports: the root script explicitly
assembles its plan; tools do not recursively execute every `build.cv`.

`cacheable` reports whether analysis permits reuse; callers still validate inputs
and outputs. `CompilerError` owns a diagnostic `message`. Process failures and
unreadable or invalid manifests raise this error. A failed compiler invocation
never consumes an earlier manifest.

This is ordinary Carven code using `std::process`, `std::fs`, and `std::json`.
The same script can run through `carven interpret foo.cv` or compile and run
through `carven run foo.cv`. Both require the installed compiler on `PATH`;
native build scripts do not link compiler internals.

## Native toolchain and execution

`CXX` selects one native compiler executable; otherwise the host finds `clang++`
on `PATH`. Names and paths containing spaces are supported. Flags belong in
`NativeOptions`, not `CXX`. Cross-compilation uses a target-specific `CXX` or
`compile_flags` such as `--target=...`; these flags also reach the executable link.
System libraries follow that configured driver's `-dumpmachine` target, including
any additional `link_flags`, rather than the build host. Supply `output` explicitly
when the target needs a different filename suffix from the host.
The compiler, SDK, linker, and archive tool must be available in the environment.
Native arguments follow GCC/Clang conventions with C++20 as the baseline. `CXXFLAGS` and `LDFLAGS` are not imported.

The builder uses the selected compiler's Crafts resources. It analyzes the union
of product Carven roots and their source-library inputs. Retained analysis is
reused when the compiler, loaded sources, and available Craft source names are
unchanged and analysis permits reuse. Generation also checks its outputs and test
mode. Craft script analysis is shared across the plan's application and test
builds; the scripts execute on each invocation so external configuration changes
are observed. Explicit `std::compiler.compile` calls always invoke the compiler.

`carven test [target]` builds test blocks from each selected target and its
dependencies. An omitted name selects all targets. Source libraries use default
native settings; static dependencies retain their native options and archives.
Test products live under `tests/<target>/` in the output directory. Targets without
runtime tests skip native compilation. Keep handwritten C++ entries in separate
targets from tested sources. External Xmake and CMake projects declare their own
test targets.

Native compilation follows generated implementation dependencies, Craft source
ownership, and the C++ compiler's header dependencies. Type-only dependencies
supply headers; static-library dependencies supply their own implementation
objects through archives.

Objects are shared only for the same source and native configuration. Reuse checks
the command, compiler, relevant environment, source and header contents, output,
and header search inventory. Analysis that observes host effects prevents object
reuse for that invocation. Link reuse requires known file inputs; named libraries
and unrestricted flags require a fresh link.

Generated C++, objects, and dependency records live under the output directory's
`.carven/`. Remove the output directory for a fresh build. Build calls using the
same output directory run serially; native compilation runs up to the host's
available parallelism. A compilation failure stops dispatch and waits for running
children before returning. Earlier successful outputs remain available.

`BuildError` contains a diagnostic `message`. Invalid plans, failed compilation,
filesystem errors, and child-process failures stop the build.
