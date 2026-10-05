# Toolchain

The toolchain collects Carven sources, runs the compiler, and supplies generated
C++ to native builds. These references define command behavior and the
requirements for consuming its output.

| Task | Reference |
| --- | --- |
| Run, check, interpret, inspect, or generate C++ | [Command-line interface](cli.md): invocation, source collection, output and test modes, linkage domains, and exit status |
| Compile and link generated code | [Artifacts](artifacts.md): native requirements, generated paths, build integration, and support headers |

See [Language](../language/README.md) for source rules and
[Backend](../compiler/backend/README.md) for how the compiler constructs its
generated artifacts.
