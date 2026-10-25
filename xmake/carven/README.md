# Xmake integration

Install Carven, Xmake 3.1.1 or newer, and a C++20-capable native toolchain.
Include the rules from your Carven installation:

```lua
includes("/path/to/carven/share/xmake/carven")

target("app")
    set_kind("binary")
    add_rules("carven")
    add_files("src/*.cv")
```

Run `xmake` to build and `xmake run app` to run.
Replace `/path/to/carven` with your toolchain installation directory. The project
supplies this path and prepares its native build environment, locally and in CI.
The rules use the compiler and Crafts from that installation.

Declare inline tests as an ordinary Xmake target:

```lua
target("app-tests")
    set_default(false)
    set_kind("binary")
    add_rules("carven", {tests = true})
    add_files("src/*.cv")
    add_tests("inline")
```

`xmake test` builds and runs registered tests.

Carven reports imported sources and Craft dependencies through its manifest.
Xmake owns native compilation, target settings, linking, and incremental builds.
Unchanged analysis and generated C++ are reused. Targets share checked Craft
build scripts; queries run on each build to observe current configuration.
`xmake build --verbose` includes compiler stage timings.
Compiler diagnostics are forwarded to stderr on successful and failed builds.
