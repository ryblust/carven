# Toolchain

The toolchain collects Carven sources, runs the compiler, and supplies generated
C++ to native builds. These references define command behavior and the
requirements for consuming its output.

| Task | Reference |
| --- | --- |
| Run, check, interpret, or inspect a program | [Command-line interface](cli.md): invocation, source collection, options, and exit status |
| Generate C++ and select output or test modes | [CLI artifact destinations](cli.md#artifact-destinations), [test emission](cli.md#test-emission), and [linkage domains](cli.md#linkage-domain) |
| Compile and link generated code | [Artifacts](artifacts.md): native requirements, generated paths, build integration, and support headers |

See [Language](../language/README.md) for source rules and
[Backend](../compiler/backend/README.md) for how the compiler constructs its
generated artifacts.
