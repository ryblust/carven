# Carven

A programming language layer that transpiles to standard C++.

Carven provides a smaller syntax and toolchain surface for C++ practice
while keeping generated C++ readable and build-system integration explicit.

## Quick Start

### Build From Source

```shell
# Configure
xmake f --toolchain=clang-cl  # Windows
xmake f --toolchain=llvm      # macOS

# Build
xmake build
```

### Test

```shell
# Run fast unit tests
xmake run carven-unit-test

# Run black-box e2e tests
python3 tests/e2e/run.py
```

The e2e runner writes generated projects and its staged install under
`tests/e2e/.sandbox`. Human-reviewed transpile cases live under
`tests/cases`: each `.cv` input is checked against the matching generated
`.cpp` file by the e2e runner. To update an expected output, regenerate that
case explicitly and review the diff:

```shell
carven transpile -o tests/cases/name.cpp tests/cases/name.cv
```

### Install

```shell
xmake build carven
xmake install -o "$HOME/.local" carven
```

Choose the install prefix explicitly with `-o`. On Unix-like platforms, xmake's
default install prefix may be a system directory such as `/usr/local`.

```shell
xmake uninstall --installdir="$HOME/.local" carven
```

### Use Carven

```shell
# Create a minimal xmake-backed project
carven init hello
cd hello
carven build
carven run app

# Run a single .cv file as a script-like entry point
carven run <file.cv> -- <args...>

# Transpile only
carven transpile <file.cv>
carven transpile -o out.cpp <file.cv>

# Delegate project build/run to the current directory's xmake.lua
carven build [target]
carven run <target> -- <args...>
```

Project builds are xmake projects. Carven's project-mode commands are thin
delegations from the current project directory; users maintain `xmake.lua` as
the build source of truth. Runtime arguments in project mode require an explicit
target so xmake does not parse program arguments as xmake options.
Single-file build and run commands create their xmake-backed cache projects
under the current directory's `.carven/scripts` directory.

### Clean Build State

Use xmake's normal clean command for ordinary rebuilds. If C++ module or BMI
state appears corrupted, such as strange module errors or unexplained undefined
symbols after exported module API changes, remove the local `build` and
`.xmake` directories with your platform's file-removal tool, then configure and
build the project again.

## Documentation

- [Language Grammar](docs/grammar.md)
- [Design Philosophy](docs/design.md)
- [Implementation Model](docs/implementation.md)
- [Feature Roadmap](docs/roadmap.md)
