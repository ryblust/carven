# Carven

A programming language that transpiles to standard C++.

## Quick Start

```shell
# Configure
xmake f --toolchain=clang-cl  # Windows
xmake f --toolchain=llvm      # macOS

# Build
xmake build

# Run a .cv file
xmake run carven run <file.cv>
```

## Documentation

- [Language Grammar](docs/grammar.md)
- [Design Philosophy](docs/design.md)
- [Feature Roadmap](docs/roadmap.md)
