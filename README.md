# Carven

A programming language layer that transpiles to standard C++.

Carven provides a smaller syntax and toolchain surface for modern C++ practice
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

# Run compiled smoke tests
xmake run carven-e2e-test
```

### Install

```shell
# Install to $HOME/.local
./scripts/install.sh

# Or choose the install prefix explicitly
./scripts/install.sh --prefix <path>
```

The install script may create `$HOME/.local/bin`, but it does not modify shell
profiles, `PATH`, or other user environment files. If the selected
`<prefix>/bin` is not on `PATH`, the script reports that and leaves the choice
to you.

The underlying install layout is owned by xmake:

```shell
xmake build carven
xmake install -o "$HOME/.local" carven
xmake uninstall --installdir="$HOME/.local" carven
```

### Use Carven

```shell
# Create a minimal xmake-backed project
carven init hello
cd hello
xmake build
xmake run app

# Run a single .cv file as a script-like entry point
carven run <file.cv> -- <args...>

# Transpile only
carven transpile <file.cv>
carven transpile -o out.cpp <file.cv>

# Delegate project build/run to the nearest xmake.lua
carven build [target]
carven run <target> -- <args...>
```

Project builds are xmake projects. Carven's project-mode commands are thin
delegations; users maintain `xmake.lua` as the build source of truth. Runtime
arguments in project mode require an explicit target so xmake does not parse
program arguments as xmake options.

### Clean Build State

```shell
xmake clean -a
rm -rf build .xmake
xmake f --toolchain=llvm
xmake build
```

Use the stronger `rm -rf build .xmake` reset after C++ module/BMI cache issues,
strange module errors, or unexplained undefined symbols after exported module
API changes.

## Documentation

- [Language Grammar](docs/grammar.md)
- [Design Philosophy](docs/design.md)
- [Implementation Model](docs/implementation.md)
- [Feature Roadmap](docs/roadmap.md)
