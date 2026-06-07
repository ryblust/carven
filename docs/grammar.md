# Carven Language Grammar

Carven transpiles to standard C++. Its syntax draws from Rust and other modern languages:
`while` and `if` conditions omit parentheses, semicolons are optional before `}` at block
end, variables are declared with `let`/`var`/`const`, and functions use `fn`.

## Module System (Imports)

```
import <module> ;
import <module> using <name> ;
import <module> using { <name>, <name>, ... } ;
import <module> using * ;
```

- `import std;` triggers emission of a `#include` preamble rather than generating
  `import std;` in the C++ output. This fallback is automatic; the `--import-std` CLI
  flag forces it regardless of source content.
- `using *` generates `using namespace <module>;`
- `using { a, b }` generates individual `using <module>::a;` and `using <module>::b;`
  declarations.
- The semicolon after an import is optional.

## Top-Level Items

A Carven source file consists of:

- `import` statements
- `fn` function definitions
- `enum` definitions
- `struct` definitions

### Functions

```
fn <name> ( <params> ) -> <return-type> { <body> }
fn <name> ( <params> ) { <body> }
```

- Parameters: `<name> : <type>` or `<name>` alone. Untyped parameters become `auto`
  in the generated C++.
- Return type is optional. `main` does not require an explicit return type; the
  generated C++ supplies `-> int` automatically.
- Body: `{ <statements> }`

### Enums

```
enum <Name> { <field>, <field>, ... }
enum <Name> : <type> { <field>, <field>, ... }
```

- Fields are comma-separated identifiers.
- An explicit underlying type may be specified:
  `enum Color : u8 { Red, Green }` → `enum class Color : std::uint8_t { ... }`

### Structs

```
struct <Name> { <field>: <type>, <field>: <type>, ... }
```

- Fields are written `<name> : <type>`. Separators may be commas or semicolons.

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

- `let` → `const auto`. Requires an initializer.
- `var` → `auto`. Initializer is optional; defaults to `Type()`.
- `const` → `constexpr auto`. Requires an initializer.
- Type annotations are optional. When both a type annotation and initializer are
  present, the generated C++ wraps the initializer in a functional cast:
  `let x: i32 = 5` → `const auto x = std::int32_t(5)`
- Semicolons are required.

### Return

```
return ;
return <expr> ;
```

- Semicolon is required.

### While

```
while <condition> <body>
while <condition> { <body> }
```

- Conditions do not use parentheses.
- The body may be a single statement or a block.

### For

```
for ( <init> ; <condition> ; <step> ) <body>
for ( <init> ; <condition> ; <step> ) { <body> }
for ( ; <condition> ; <step> ) ...
for ( ; ; ) ...
```

- The init clause may be a `var`/`let`/`const` declaration or an expression
  (e.g. `i = 0`).
- All three clauses are optional.
- Generates a standard C-style `for (init; cond; step)` loop.

### Expression Statements

```
<expr> ;
<expr>          // semicolon optional before `}` at end of block
```

- A semicolon is required mid-block; it is optional immediately before `}`.
- Empty statement: `;`

### If Expression

```
if <condition> { <body> }
if <condition> { <body> } else { <body> }
```

- `if` is always an expression. When used in statement position it is wrapped in
  an `ExprStmt` node.
- When used as a value, `if` requires an `else` branch. Each branch must end
  with an expression that omits the trailing semicolon; earlier statements in
  the branch are allowed.
- Value-position `if` lowers to an immediately invoked lambda in generated C++.
- `return` is not allowed inside value-position `if` branches. Statement-position
  `if` branches may still use `return` to return from the surrounding function.
- The keywords `true`, `false`, `if`, and `match` are valid in expression
  position. Other keywords (`fn`, `let`, `while`, etc.) produce an error.

### Match Expression

```
match <expr> {
    <pattern> => { <body> }
    <pattern> | <pattern> => { <body> }
    _ => { <body> }
}
```

- `match` is always an expression, like `if`.
- Patterns are distinguished by token type — the parser performs no symbol-table
  lookup (context-free):
  - Literals (`42`, `true`, `"hello"`) — runtime value match: `value == pattern`
  - Identifiers (`i32`, `Point`) — compile-time type match via
    `if constexpr (std::is_same_v<T, Type>)`
  - `_` — wildcard, matches everything
- Identifier patterns are always type patterns. They cannot currently match the
  value of a variable with the same name. See
  [Match `is` Patterns](proposals/match-is-patterns.md) for the planned explicit
  type/value matching extension.
- Multi-patterns use `|`: `1 | 2 | 3 => ...`
- Trailing commas are allowed between arms.
- When used as a value, `match` must include a wildcard `_` arm. The wildcard
  arm, when present, must be last.
- Value-position `match` arms must end with an expression that omits the trailing
  semicolon; earlier statements in the arm are allowed.
- Value-position `match` lowers to an immediately invoked lambda and evaluates
  the matched expression once.
- `return` is not allowed inside value-position `match` arms. Statement-position
  `match` arms may still use `return` to return from the surrounding function.

## Expressions

### Literals

- Number: `42`, `3.14`
- String: `"hello"` (supports `\"` and `\\` escapes)
- Char: `'a'` (supports `\'` and `\\` escapes)
- Bool: `true`, `false`

### Array Literals

```
[ <expr>, <expr>, ... ]
```

- Array literals generate `std::array { ... }` via CTAD.
- `let a = [1, 2, 3]` → `const auto a = std::array { 1, 2, 3 }`
- `let m = [[1, 2], [3, 4]]` → nested `std::array { std::array { 1, 2 }, std::array { 3, 4 } }`
- Trailing commas are allowed.
- Empty arrays (`[]`) require an explicit type annotation.

### Identifiers

`[a-zA-Z_][a-zA-Z0-9_]*`

### Operators (lowest to highest precedence)

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

- `-` `!` `~` (negation, logical not, bitwise not)
- `++` `--` (pre-increment, pre-decrement)
- `&` `*` (address-of, dereference)

### Binary Expressions

Standard infix binary operators with the precedence levels listed above.
Left-associative, except assignment operators which are right-associative.

### Ternary

Ternary expressions are not directly writable in Carven source. They appear only
in the generated C++ as an optimization for single-expression `if`/`else` branches.

## Built-in Types

| Carven    | C++              |
|-----------|------------------|
| `i8`      | `std::int8_t`    |
| `i16`     | `std::int16_t`   |
| `i32`     | `std::int32_t`   |
| `i64`     | `std::int64_t`   |
| `u8`      | `std::uint8_t`   |
| `u16`     | `std::uint16_t`  |
| `u32`     | `std::uint32_t`  |
| `u64`     | `std::uint64_t`  |
| `f32`     | `float`          |
| `f64`     | `double`         |
| `bool`    | `bool`           |
| `char`    | `char`           |
| `usize`   | `std::size_t`    |

Unrecognised type names are passed through unchanged to the generated C++.

### Array Types

```
[ <type> ; <size> ]
```

- `let a: [i32; 3] = [1, 2, 3]` → `const auto a = std::array<std::int32_t, 3> { ... }`
- `fn f(p: [i32; 3])` → parameter type `std::array<std::int32_t, 3>`
- The element type may be any Carven type. The size must be a compile-time expression.

## Comments

Line comments: `//` to end of line. Block comments are not supported.

## Keywords

`import` `export` `using` `enum` `struct` `fn` `var` `let` `const` `match`
`as` `if` `else` `while` `for` `return` `true` `false`

## Generated C++ Conventions

- All functions are marked `noexcept`.
- `main` does not need an explicit return type — `-> int` is supplied automatically.
- Functions without an explicit return type omit the trailing return type, relying
  on C++ deduction.
- Untyped parameters become `auto`.
- Value-position `if` and `match` expressions are emitted as immediately invoked
  lambdas.
- `import std;` does not emit `import std;` in the output. It triggers a
  `#include` preamble instead, selected by the C++ standard level
  (14, 17, 20, 23, or 26).

## Limitations

The following features are not yet supported:

- Struct literals in expression position (e.g. `Point { x: 1, y: 2 }`)
- Generics or templates
- Traits or interfaces
- Lambdas
- Multi-file projects (single-file model only)
- Enum variants with payloads (plain enums only)
- Module-level `const` or `static` items
- `extern` function declarations (all functions must have a body)
