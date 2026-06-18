# cpp-format.md

## Includes / Imports
- Naming: `carven.<layer>.<name>`.
- Module `.cppm`: project imports in dependency order, `import std;` last.
- Test `.cpp`: `#include` block, blank line, `import` block.
- Blank line between the `export module` declaration and first import.

## Comments
- `//` only. On own line above code. Inline OK for short annotations.

## Indentation
- 4 spaces. No tabs. No trailing whitespace. LF line endings.
- Max 1 consecutive blank line. No blank line at EOF.

## Braces
- Always Attach (K&R): open on same line as introducer.
- Empty init `Span{}` — no space before `{`.
- Non-empty init `std::array { 1, 2, 3 }` — space before `{`, spaces inside.
- Designated init: one field per line, 4-space indent, closing at parent indent.

## Spaces
- Binary ops: `a + b`. Unary ops: `-x`, `!p`.
- Control: `if (`, `for (`, `while (`, `switch (`.
- No space inside `()`, `[]`, `<>`.
- After comma: `f(a, b)`. After for-semicolon: `for (i = 0; i < n; ++i)`.
- Trailing return: `auto f() noexcept -> T`.

## Pointer & Reference
- `Type* ptr`, `Type& ref`. Touches type, not name.
- Never `auto*` — `auto` deduces the pointer type. Use `auto p = ...` not `auto* p = ...`.

## Templates
- `template<typename T>`. No space after `template`.
- Specializations: `template<> struct fmt::formatter<Foo> final {`.

## Classes & Structs
- Naming: Type: `PascalCase`; Field: `snake_case`.
- `public:` / `private:` at column 0 (no indent from class body).
- Members indent 4. `final` after name: `struct Foo final : Bar {`.
- Single-line when trivial: `struct Span { u32 start; u32 end; };`.

## Enums
- Naming: `PascalCase`.
- Pack multiple per line. Trailing comma. Indent 4.

## Functions
- Naming: `snake_case`.
- Trailing return type always.
- Wrap params aligned to opening paren: `func(a, b,` <br> `      c, d)`.
- Single-statement accessors may be one-line: `auto f() noexcept -> int { return x; }`.

## Constructor Init Lists
- Short: same line as constructor. `: a(x), b(y) {}`
- Condensed: constructor signature on its own line, then `: a(x), b(y) {}` on the next at constructor indent + 4.
- Wrapped: comma-before, 8-space indent. Each member own line.

## Control Flow
- Short single-statement `if` may be one-line: `if (!p) break;`.

## Switch
- Case labels indent 4. `using enum` inside switch at same indent.
- Single-line cases: `case Foo: do_thing();   break;` (align break columns).

## Column Alignment
- Align columns in 3+ parallel lines (keyword tables, type mappings, case labels).
- Use minimum width to align, no excessive padding.
- `return` alignment in map-like functions encouraged.

## Trailing Commas
- Allowed in multi-line lists to reduce diff noise.
