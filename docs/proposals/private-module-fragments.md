# Private Module Fragments

Status: implementation/tooling proposal.

## Goal

Use C++20 private module fragments to make Carven's `.cppm` files keep a
single-file module style while clearly separating public module interfaces from
implementation details.

Carven already avoids the traditional `.h` plus `.cpp` layout. Private module
fragments let each module keep that shape while making the top of the file read
like the module contract and the rest of the file read like implementation.

## Motivation

Many Carven modules export a small API but contain substantial internal
machinery. For example, the lexer module exports tokenization while its
character classification helpers and scanner state machine are implementation
details.

Today those helpers are not exported, but they still live in the module
interface portion of the file. Moving them after `module : private;` makes the
boundary explicit:

```cpp
export module carven.frontend.lexer;

import carven.frontend.token;
import std;

export constexpr auto tokenize(std::string_view source) noexcept -> std::vector<Token>;

module : private;

class Lexer final {
    // scanner implementation
};

constexpr auto tokenize(std::string_view source) noexcept -> std::vector<Token> {
    // implementation
}
```

The important distinction is that `export` controls who can name a declaration,
while `module : private;` controls whether a declaration is part of the module
interface at all.

## Proposed Rule

Prefer this structure for modules whose public API can be declared compactly:

```cpp
export module carven.some.module;

import carven.dependency;
import std;

export struct PublicType;
export auto public_function(PublicType value) noexcept -> void;

module : private;

// helpers, private classes, private constants, implementation bodies
```

Keep declarations before the private fragment when importers need them as part
of the module contract:

- exported enum, struct, and class definitions
- exported inline or constexpr definitions that must remain visible
- exported templates and template specializations required by importers
- non-exported declarations used by exported declarations in a way that affects
  their type, layout, constraints, or constant evaluation

Move declarations after `module : private;` when they are pure implementation:

- helper functions
- implementation-only classes
- scanner, parser, or command state machines
- private constants and tables
- function bodies for exported non-template APIs

## Candidate Modules

Good early candidates:

- `src/frontend/lexer.cppm`: exported API is currently `tokenize`; the scanner
  implementation can live in the private fragment.
- `src/backend/codegen.cppm`: the exported generation entry point can stay in
  the interface while the `Codegen` class and helper functions move private.
- `src/driver/command/*.cppm`: command types and `execute` functions can remain
  public while command helpers move private.
- `src/common/process.cppm` and `src/common/filesystem.cppm`: platform-specific
  implementation should be hidden behind the exported process/file APIs.

More cautious candidates:

- `src/frontend/parser.cppm`: parser internals are a good fit, but result and
  error types must remain visible where importers depend on them.
- `src/frontend/sema.cppm`: semantic checker internals can move private only
  after confirming exported result types stay complete enough for callers.

Lower-benefit candidates:

- `src/frontend/ast.cppm`
- `src/frontend/token.cppm`

These modules are mostly shared data contracts, so many declarations must remain
in the interface. They are still valid migration targets: helper functions,
formatting implementation details, lookup tables, and any non-contract code can
move into a private fragment. The expected benefit is smaller, but the project
can still converge on a consistent single-file interface/private layout across
all `.cppm` modules.

## Migration Plan

Start with one module and keep each change mechanical:

1. Identify the exported API used by other modules and tests.
2. Add forward declarations for exported functions or types that can be declared
   before the private fragment.
3. Insert `module : private;`.
4. Move implementation-only declarations and function bodies below it.
5. Build and run the focused test area.
6. Run `python3 tests/test.py --unit` before broadening the pattern.

Avoid mixing this refactor with behavior changes. The output, public API, and
tests should remain the same unless a module boundary bug is discovered.

## Tests

For each migrated module:

- run the nearest unit test file when one exists
- run `python3 tests/test.py --unit`
- run an e2e case when the module affects generated output or CLI behavior

For the initial lexer migration, `python3 tests/test.py --unit` is sufficient
because lexer behavior is already covered directly and through parser/codegen
tests.

## Non-Goals

- Do not split `.cppm` modules into separate interface and implementation files.
- Do not change Carven's public module names.
- Do not change generated C++ output.
- Do not force a declaration behind a private fragment when it is part of an
  exported type, template, constexpr contract, or caller-visible layout.
- Do not move declarations private when doing so forces awkward forward
  declarations or makes an exported type incomplete for normal callers.

## Open Questions

- Should Carven adopt a house style where private fragments appear immediately
  after imports and exported declarations?
- Should small helper functions stay above the fragment when they make exported
  constexpr definitions easier to read?
- Should tests ever assert that implementation-only names are not importable, or
  is successful compilation of callers enough?
