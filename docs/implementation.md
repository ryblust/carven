# Implementation Model

This document is Carven's implementation architecture guide. Read it when
changing module boundaries, public APIs, driver/core layering, runtime
integration, or test strategy. Ordinary small edits should not need this file.

Carven's implementation is split between a pure source pipeline and runtime
driver edges. The source pipeline should stay deterministic and
constexpr-friendly where practical; filesystem, process, terminal, and xmake
integration should stay at the edges.

## Source Pipeline

The pure source-to-C++ path includes:

- tokenization
- parsing
- semantic checks
- C++ text generation through `carven.backend.codegen`

These stages should remain value-oriented and constexpr-friendly where
practical. Unit tests should be able to exercise small examples without
filesystem or process dependencies.

Runtime edges are outside this source pipeline:

- filesystem access
- process execution
- xmake invocation
- terminal output
- installation
- workspace cache management

These edges should be thin wrappers over the pure core.

## Driver Boundaries

Driver modules keep CLI integration separate from the pure source pipeline:

| Module | Owns | Does not own | Public boundary |
|--------|------|--------------|-----------------|
| `carven.driver.command` | root command parsing, help/version output, CLI error formatting, sub-command dispatch | command execution internals, source processing, xmake details | `carven_main(argc, argv)` |
| `carven.driver.command.*` | final execution endpoint for one user-facing sub-command | root routing, reusable compiler stages | command type and `execute(command)` |
| `carven.driver.xmake` | xmake project file generation, embedded `carven.lua` rule writing, target-name sanitization, local xmake project discovery | subprocess execution, CLI parsing | xmake helpers used by commands |
| `carven.driver.diagnostics` | CLI source diagnostic rendering | frontend diagnostic ownership, terminal-independent diagnostic data | rendering adapters such as `report_errors` |
| `carven.common.filesystem` | low-level file mapping and file writing | source parsing, CLI policy | file IO primitives |
| `carven.common.process` | subprocess execution and captured output | command construction, xmake policy | process execution primitives |

Project-mode commands are intentionally current-directory local. Commands such
as `carven build` and project-mode `carven run` check only `./xmake.lua`; they
do not search parent directories or infer a project root. If Carven later
supports another project file location, that path must come from an explicit
CLI option.

Diagnostics follow the same layering: frontend and backend code return
diagnostic values, while driver code owns terminal rendering. A shared
`carven.common.diagnostics` layer should exist only when multiple compiler
stages need common diagnostic value types.

## Core Rules

- Prefer explicit inputs and return values over hidden global state.
- Shape product APIs around correct module boundaries and minimal public
  surface, not test convenience.
- Prefer testing public behavior and stable contracts over exporting internal
  helpers.
- Keep internal helpers internal unless they represent a real product-level
  abstraction.
- When fine-grained tests need implementation access, use explicit internal
  test seams instead of accidental product exports.
- Keep source text owned outside AST nodes. AST nodes store spans.
- Keep diagnostics as values, not side effects.
- Keep parser decisions context-free. Name lookup and type checks belong after
  parsing.
- Keep codegen deterministic for the same AST, source text, and options.
- Use `constexpr` as a design promise for pure core logic, not as decoration for
  arbitrary runtime code.

## Modules

Carven is module-first. The project should not mechanically split every module
into `.cppm` interface files and `.cpp` implementation files.

Exported declarations are product boundaries. Runtime and driver modules should
keep those boundaries small, stable, and facade-like: export product-level
entrypoints, and move behavior into non-exported implementation functions.
Public API count should stay low enough that each export is designed
deliberately rather than exposed for convenience.

For constexpr core code:

- keep exported APIs small
- keep required definitions reachable from module interfaces
- use non-exported declarations inside module interfaces when useful
- prefer module interface partitions over hidden implementation units when
  constant evaluation needs the definitions

For runtime-only code, `.cpp` files, private module fragments, or internal
module partitions can be used when they make the implementation clearer.
Use private module fragments when they make the interface/implementation
boundary clearer without forcing awkward declarations or weakening constexpr
contracts.

## Allocation

Transient allocation is acceptable inside constant evaluation as long as it does
not escape the immediate constexpr computation. `std::vector`, `std::string`,
and arena-backed AST construction can be useful inside `static_assert` tests
when the resulting owned objects do not need static storage.

The current arena design treats ASTs as short-lived compiler data. If Carven
later adds long-lived services such as an LSP or daemon mode, arena destruction
and non-trivial AST member cleanup must be revisited.

## Tests

Tests should match the boundary being protected:

- constexpr unit tests cover pure tokenization, parsing, sema, and codegen
  examples
- black-box e2e tests cover the installed CLI, xmake-backed workflows, cache
  projects, generated program execution, and human-reviewed `tests/cases`
  outputs
- expected `tests/cases/*.cpp` outputs are updated explicitly with
  `carven transpile -o` and reviewed as diffs
- e2e tests should stay at CLI workflow level instead of duplicating
  unit-level lexer, parser, codegen, or dump-format assertions

Inline language tests can follow the same principle later: pure compile-time
examples first, runtime integration second.
