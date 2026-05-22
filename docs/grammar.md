# The Carven Programming Language Grammar

Carven transpiles to standard C++. The syntax is Rust-inspired: no parens for `while`, optional semicolons at
block-end for expressions, `let`/`var`/`const` for variable declarations, and `fn` for functions. Single-file
model — `import` statements generate C++ module imports verbatim; the transpiler does not resolve imported files.

## Module System (Imports)

```
import <module> ;
import <module> using <name> ;
import <module> using { <name>, <name>, ... } ;
import <module> using * ;
```

- `import std;` triggers `#include` preamble emission instead of `import std;` in generated C++.
  This fallback is automatic. The `--import-std` CLI flag forces it regardless of source content.
- `using *` generates `using namespace <module>;`
- `using { a, b }` generates individual `using <module>::a;` and `using <module>::b;`
- Semicolon after import is optional (consumed if present, not required)

## Top-Level Items

A Carven source file contains a sequence of:

- `import` statements
- `fn` function definitions
- `enum` definitions
- `struct` definitions

### Functions

```
fn <name> ( <params> ) -> <return-type> { <body> }
fn <name> ( <params> ) { <body> }
```

- Parameters: `<name> : <type>` or just `<name>` (untyped → `auto` in C++)
- Return type: optional. `main` always gets `-> int` in generated C++. Other functions without explicit return
  type emit no return type annotation (deduced return).
- Body: `{ <statements> }`

### Enums

```
enum <Name> { <field>, <field>, ... }
enum <Name> : <type> { <field>, <field>, ... }
```

- Fields are comma-separated identifiers
- Optional explicit size type: `enum Color : u8 { Red, Green }` → `enum class Color : std::uint8_t { ... }`

### Structs

```
struct <Name> { <field>: <type>, <field>: <type>, ... }
```

- Fields are `<name> : <type>`, separated by commas or semicolons (either is accepted)

## Statements

### Variable Declarations

```
let <name> = <expr> ;
let <name> : <type> = <expr> ;
var <name> = <expr> ;
var <name> : <type> = <expr> ;
const <name> = <expr> ;
const <name> : <type> = <expr> ;
```

- `let` → `const auto` in C++. Requires an initializer.
- `var` → `auto` in C++. Initializer is optional; defaults to `Type()`.
- `const` → `constexpr auto` in C++. Requires an initializer.
- Type annotation is optional. When present with an initializer, generated C++ wraps the init in a cast:
  `let x: i32 = 5` → `const auto x = std::int32_t(5)`
- Semicolon required.

### Return

```
return ;
return <expr> ;
```

- Semicolon required.

### While

```
while <condition> <body>
while <condition> { <body> }
```

- No parentheses around condition
- Body can be a single statement or a block

### For

```
for ( <init> ; <condition> ; <step> ) <body>
for ( <init> ; <condition> ; <step> ) { <body> }
for ( ; <condition> ; <step> ) ...
for ( ; ; ) ...
```

- Init can be a `var`/`let`/`const` declaration or an expression (`i = 0`)
- All three clauses are optional
- Generated C++ uses C-style `for (init; cond; step)` loops

### Expression Statements

```
<expr> ;
<expr>          // semicolon optional at end of block (before `}`)
```

- Semicolon required mid-block, optional before `}`
- Empty statement: `;` (just a semicolon)

### If as Expression

```
if <condition> { <body> }
if <condition> { <body> } else { <body> }
```

- `if` is always an expression (not a statement). When used in statement position, it wraps in `ExprStmt`.
- Codegen optimization: when both branches are single-expression blocks, emits ternary `? :` instead of
  `if`/`else` blocks.
  - `let x = if true { 1 } else { 2 };` → `const auto x = true ? 1 : 2;`
- Expression-position keywords: only `true`, `false`, `if` are valid in expression context. Other keywords
  (`fn`, `let`, `while`, etc.) produce an error.

## Expressions

### Literals

- Number: `42`, `3.14`
- String: `"hello"` (supports `\"` and `\\` escapes)
- Char: `'a'` (supports `\'` and `\\` escapes)
- Bool: `true`, `false`

### Identifiers

`[a-zA-Z_][a-zA-Z0-9_]*`

### Operators (precedence from lowest to highest)

| Precedence | Operators | Associativity |
|-----------|-----------|---------------|
| Assignment | `=` | Right |
| Compound assignment | `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | Right |
| Comma | `,` | Left |
| Logical OR | `\|\|` | Left |
| Logical AND | `&&` | Left |
| Bitwise OR | `\|` | Left |
| Bitwise XOR | `^` | Left |
| Bitwise AND | `&` | Left |
| Equality | `==` `!=` | Left |
| Relational | `<` `<=` `>` `>=` | Left |
| Shift | `<<` `>>` | Left |
| Additive | `+` `-` | Left |
| Multiplicative | `*` `/` `%` | Left |
| Prefix unary | `-` `!` `~` `++` `--` `&` `*` | Right |
| Postfix unary | `++` `--` | Left |

### Primary Expressions

- Literals (number, string, char, bool)
- Identifiers
- Grouped: `( <expr> )`
- If-expression: `if <condition> { <body> } else { <body> }`

### Postfix Expressions

- Function call: `<expr>( <args> )`
- Index: `<expr>[ <expr> ]`
- Field access: `<expr>.<name>`, `<expr>-><name>`, `<expr>::<name>`
- Post-increment/decrement: `<expr>++`, `<expr>--`

### Prefix Expressions

- `-` `!` `~` (negate, logical not, bitwise not)
- `++` `--` (pre-increment, pre-decrement)
- `&` `*` (address-of, deref)

### Binary Expressions

Standard infix binary operators with the precedence table above. Left-associative except assignment (right).

### Ternary

Ternary expressions are not directly writable in Carven source. They are an AST node emitted by the codegen
optimization for single-expression `if`/`else` branches.

## Built-in Types

| Carven | C++ |
|------|-----|
| `i8`  | `std::int8_t`  |
| `i16` | `std::int16_t` |
| `i32` | `std::int32_t` |
| `i64` | `std::int64_t` |
| `u8`  | `std::uint8_t`  |
| `u16` | `std::uint16_t` |
| `u32` | `std::uint32_t` |
| `u64` | `std::uint64_t` |
| `f32` | `float`        |
| `f64` | `double`       |
| `bool`| `bool`         |
| `char`| `char`         |
| `usize` | `std::size_t` |

Unrecognized type names pass through unchanged to generated C++.

## Comments

Line comments: `//` to end of line. No block comments.

## Keywords

`import` `export` `using` `enum` `struct` `fn` `var` `let` `const` `as`
`if` `else` `while` `for` `return` `true` `false`

## Generated C++ Conventions

- All functions are `noexcept`
- `main` always returns `int`
- Functions without return type omit the trailing return type (C++ deduces it)
- Untyped parameters become `auto`
- `import std;` does NOT generate `import std;` in output — it emits `#include` preamble instead
- The `#include` preamble is selected by C++ standard level (14/17/20/23/26)

## What Isn't Supported (Yet)

- No struct literals (cannot write `Point { x: 1, y: 2 }` in expressions)
- No `match`/pattern matching
- No generics/templates
- No traits or interfaces
- No lambdas
- No multi-file projects (single-file model)
- No `enum` variant payloads (plain enums only)
- No module-level `const` or `static` items
- No `extern` function declarations (all functions must have bodies)
