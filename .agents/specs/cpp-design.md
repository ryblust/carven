# cpp-design.md

## Semantic Model

### Value
Small value object. Trivially copyable. No ownership semantics. No invariants.

Examples: `int`, `float`, `enum`, `std::string_view`, `std::span`.

Preferred initialization:
```cpp
const auto count = 42;
```

### Data
Aggregate. Public fields. No hidden state. No ownership policy. No invariant enforcement.

Preferred representation:
```cpp
struct Endpoint final {
    std::string host;
    int port;
};
```

Preferred initialization:
```cpp
Endpoint {
    .host = "localhost",
    .port = 8080,
};
```

### Behavior
Maintains invariants. Controls state transitions. Owns resources. Encapsulates lifecycle.

Preferred representation:
```cpp
class Lexer final {
public:
    explicit Lexer(std::string_view source) noexcept;
};
```

Preferred initialization:
```cpp
const auto lexer = Lexer(source);
```

## Struct vs Class

Prefer `struct` when ALL conditions hold: public fields, no invariants, no ownership, aggregate initialization desired.

Prefer `class` when ANY condition holds: private state, resource ownership, lifecycle control, invariant maintenance.

## Error Handling

Exceptions disabled. Never `throw`, `try`, `catch`.

Use `std::optional<T>` when absence of value is expected and no error explanation is required.

Use `std::expected<T, E>` when operation can fail and failure reason matters.

## Function Qualifiers

Every project function must be `noexcept`.

Prefer `constexpr` on every pure function that can be evaluated at compile time.

## Local Variables

`const` by default. `const auto` for every local variable that is not explicitly mutated.

`const auto&` for non-trivial types (ownership, invariants, resource management). `const auto` for trivially-copyable scalars (`int`, `float`, `bool`, `enum`), pointers, and trivial small values (`Span`, `std::string_view`).

## Parameter Policy

Read-only string parameters: `std::string_view`. Not `const std::string&`.

Read-only range parameters: `std::span<const T>`. Not `const std::vector<T>&`.

Trivial types (`int`, `float`, `bool`, `enum`, pointer, iterator): pass by value. Never `const int&`.

Do not wrap `const char*` or `string_view` in `std::filesystem::path` for function parameters — let the caller decide the conversion.

## Return Value Policy

Avoid output parameters. Prefer monadic returns. Exception: `std::string& result` sink parameters for code generation and string composition are acceptable for performance.

```cpp
// bad
bool parse(X& out);

// good
auto parse() -> std::expected<X, Error>;
```

## Initialization Semantics

Value initialization (`=`): scalars. `const auto n = 42;`

Behavior initialization (`()`): objects with logic, invariants, resource allocation. `Lexer(source)`.

Data initialization (`{}`): aggregates, declarative data structures, literal arrays.

### Designated Initializers

Prefer when field names carry semantic meaning. Do not use when data is purely positional (`Color { 255, 0, 0 }`, `Vec3 { 1, 2, 3 }`).

## Container Semantics

Data facet (`{}`): literal element initialization. Rely on CTAD.
```cpp
const auto scores = std::vector { 95, 88, 100 };
```

Behavior facet (`()`): active resource allocation or size bounding.
```cpp
const auto buffer = std::vector<int>(1024, 0);
```

Resource-owning value types:
```cpp
const auto dynamic_key = std::string("init_system_token");
```

## Auto and CTAD

Use `auto` when type is obvious. `const auto count = items.size();`

Avoid `auto` when type is non-obvious, deduction hides intent, or specific type selection matters. Explicitly specify template arguments when needed:
```cpp
const auto names = std::array<std::string, 2> { "a", "b" };
```

## Control Flow

Prefer `if`-with-initializer to limit variable scope:
```cpp
if (const auto value = find(); value) { }
```

Loop increment: always `++i`. Never `i++` unless the post-increment value is required.

## Output

Prefer `std::print` / `std::println`. Avoid `std::cout` / `printf`.
