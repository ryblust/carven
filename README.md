# Carven

Carven is a small programming language layer that transpiles to readable,
standard C++.

It is meant for practicing and shaping C++ programs with less syntactic noise,
while keeping the generated code inspectable and the build system explicit.
Carven does not try to hide xmake; project builds remain ordinary xmake
projects, and `xmake.lua` stays the source of truth.

## Quick Start

Build Carven from source:

```shell
xmake f --toolchain=llvm # macOS and Linux
xmake f --toolchain=clang-cl # Windows
xmake build carven
```

During local development, run the built CLI through xmake:

```shell
xmake run carven transpile tests/cases/helloworld.cv
```

Install:

```shell
xmake install -o "$HOME/.local" carven
```

Choose the install prefix explicitly with `-o`. On Unix-like platforms, xmake's
default install prefix may be a system directory such as `/usr/local`.

Uninstall:

```shell
xmake uninstall --installdir="$HOME/.local" carven
```

Once installed, create and run a minimal project:

```shell
carven init helloworld
cd helloworld
carven build
carven run helloworld
```

## Testing

```shell
python3 tests/test.py
```

## Project Workflow

Run a single `.cv` file without creating a project:

```shell
carven run path/to/file.cv arg1 arg2
```

Transpile only:

```shell
carven transpile path/to/file.cv
carven transpile -o out.cpp path/to/file.cv
```

In project mode, `carven build` and `carven run` delegate to the current
directory's `xmake.lua`. Runtime arguments require an explicit target:

```shell
carven build <target>
carven run <target> arg1 arg2
```

Single-file run commands create xmake-backed cache projects under
`.carven/scripts`.

## Troubleshooting

If C++ module or BMI state appears corrupted, remove the local
`build` and `.xmake` directories, then configure and build again.

## Learn More

- [Language Grammar](docs/grammar.md)
- [Design Philosophy](docs/design.md)
- [Implementation Model](docs/implementation.md)
- [Feature Roadmap](docs/roadmap.md)
