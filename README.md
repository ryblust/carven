# Carven

Carven is a language layer for writing C++ with less ceremony and a clearer
surface. It keeps C++ visible: generated code is meant to be read, build files
stay explicit, and project builds remain ordinary xmake projects.

## Quick Start

Build Carven from source:

```shell
xmake f --toolchain=llvm # macOS and Linux
xmake f --toolchain=clang-cl[llvm] # Windows
xmake build carven
```

If C++ module or BMI state appears corrupted, remove the local `build` and
`.xmake` directories, then configure and build again.

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
This installs the CLI to `$HOME/.local/bin/carven`; ensure `$HOME/.local/bin`
is on your `PATH`.

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

Run the test suite:

```shell
python3 tests/test.py
```

Run selected e2e cases by repeating `--case` in one runner:

```shell
python3 tests/test.py --case transpile --case single_file_run
```

Do not run multiple e2e runner processes in parallel. They share
`tests/e2e/.sandbox`; concurrent runners can delete each other's install or
case workspace.

## Using Carven

Run a single `.cv` file without creating a project:

```shell
carven run path/to/file.cv arg1 arg2
```

This creates an xmake-backed cache project under `.carven/scripts`.

Transpile without building or running:

```shell
carven transpile path/to/file.cv
carven transpile -o out.cpp path/to/file.cv
```

Project commands delegate to the current directory's `xmake.lua`:

```shell
carven build
carven build <target>
carven run <target>
carven run <target> arg1 arg2
```

Runtime arguments in project mode require an explicit target. Use `--` when
forwarded arguments begin with `-`:

```shell
carven run <target> -- --flag value
```

## Learn More

- [Language Grammar](docs/grammar.md)
- [Design Philosophy](docs/design.md)
- [Implementation Model](docs/implementation.md)
- [Feature Roadmap](docs/roadmap.md)
