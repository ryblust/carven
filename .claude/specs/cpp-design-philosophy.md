# C++ Design Philosophy

## Overview

This document defines a unified design philosophy for C++ codebases. It goes beyond formatting rules and focuses on type design semantics, initialization semantics, API design patterns, value vs behavior modeling, visual structure of code, and robust error handling.

The core goal is to make C++ code:
* Semantically explicit
* Visually structured
* Low in unnecessary token density
* Consistent at scale
* Robust against historical C++ pitfalls

## 1. Core Philosophy & Architecture Model

### 1.1 Semantic Code & The Three-Layer Model
C++ constructs are not only syntactic choices, but semantic signals. Every construct should communicate whether it is a value, a data structure, a behavior/process, or a resource with a lifetime.

All C++ code can be classified into three conceptual layers:
* **Value Layer:** Simple, immutable or trivially copyable values.
* **Data Layer:** Structured records, aggregates, configuration-like objects.
* **Behavior Layer:** Objects with invariants, resource ownership, or runtime logic.

## 2. Type, State, and API Design

### 2.1 Data vs. Behavior Encapsulation
* **`struct` = Data:** Use `struct` when the type is primarily a data container. Free functions implement behavior on data structs.
    * Characteristics: Public fields, no invariants, no resource ownership, aggregate-friendly, value semantics.
    * Rule: Mark as `final` unless the struct is designed for inheritance.
    ```cpp
    struct Point final {
        int x;
        int y;
    };
    ```
* **`class` = Behavior + Invariants:** Use `class` when the type encapsulates behavior.
    * Characteristics: Invariants, private state, non-trivial but infallible construction logic, lifecycle control.
    ```cpp
    class Lexer final {
    public:
        explicit constexpr Lexer(std::string_view source) noexcept;
        constexpr auto next() noexcept -> Token;

    private:
        std::string_view source;
        // ... remaining members
    };
    ```
* **Single-Field Structs:** Prefer designated initialization for semantic wrappers to preserve semantic meaning, avoid degenerating into primitive types, and improve self-documentation.
    ```cpp
    UserId { .value = 42 }
    Timeout { .ms = 100 }
    Meters { .value = 3.5f }
    ```

### 2.2 Error Handling & Qualifiers
* **The Monadic Error Paradigm:**
    * Use `std::optional<T>` as the standard return type for functions that may produce no value, but where the lack of a value does not require an error explanation.
    * Use `std::expected<T, E>` as the standard return type for operations that can fail and need to propagate an error state. 
    * Avoid `try`/`catch`/`throw`. The project must compile with exceptions disabled (`-fno-exceptions`). Mark every function `noexcept`.

* **Do vs. Don't: API Signatures**
    ```cpp
    // DON'T: The Historical Anti-pattern
    // Ambiguous intent, mutable out-parameters, lacks compile-time guarantees.
    bool parse_endpoint(std::string_view config_line, EndpointConfig& out_config);

    // DO: The Declarative Signature
    // Explicit return state, pure function, zero-cost error propagation.
    constexpr auto parse_endpoint(std::string_view config_line) noexcept -> std::expected<EndpointConfig, ParseError>;
    ```

* **Qualifier Philosophy:**
    * `const` on every local variable that is not explicitly mutated.
    * `constexpr` on every pure function that can be evaluated at compile time.
    * `noexcept` on every project function.

### 2.3 Declarative APIs & Parameter Passing
* **Prefer Declarative APIs:** Design APIs to express intent rather than steps. Avoid positional overloads when semantic clarity is required.
* **Parameter Type Selection:**
    * `std::string_view` over `const std::string&` for read-only string parameters.
    * `std::span<const T>` over `const std::vector<T>&` for read-only range parameters.
    * Pass-by-value for trivially copyable types instead of `const Type&`.
    * Do not wrap `const char*` or `string_view` in `std::filesystem::path` for function parameters — let the caller decide the conversion.
* **Output and I/O:**
    * Use `std::print` / `std::println` over `std::cout` / `printf`.
    * Prefer `std::format` for string composition; use `.append()` only for plain literal fragments.

## 3. Initialization & Formatting Rules

### 3.1 The Semantic Mapping of Initialization
* **Value Initialization (`=`):** Use for simple scalar values (`const auto n = 42;`). Avoid unnecessary brace initialization for scalars unless semantic value is added.
* **Behavior Initialization (`()`):** Use when constructing objects with logic, invariants, or explicit resource-allocation commands (`const auto lexer = Lexer(source);`).
* **Data Initialization (`{}`):** Use for aggregates, declarative data structures, and literal data arrays.

### 3.2 Aggregates, Designated Initializers & Brace Formatting
* **Brace Formatting Philosophy:**
    * **Single-line aggregate style:** `Point { .x = 1, .y = 2 }`. **Require a space after the type and before `{`**, with no extra spaces inside `{}`. This improves visual separation and prevents token sticking.
    * **Multi-line aggregate style:** Type followed by space + `{`, one field per line, trailing comma allowed. 
    * **Omit type when context provides it:** `run({ .host = "localhost", .port = 8080 });`
    * **Control-flow braces:** Use braces `{}` when the body is on its own line; omit for single-line bodies.
* **Designated Initializer Strategy:** Use when field names carry semantic meaning and the structure is descriptive. Do not use when data is purely positional in nature (e.g., `Color { 255, 0, 0 }`).

### 3.3 Standard Library & Container Semantics
* **Polymorphic Container Semantics (`std::vector`, etc.):** Resource-owning container types exhibit dual semantic facets depending on the invocation context. 
    * **Data Facet (`{}`):** When a container functions purely as a transparent data array initialized with explicit element literals, it belongs to the *Data Layer*. Use brace initialization and rely on CTAD (Class Template Argument Deduction) to eliminate redundant type declarations when the deduced type matches intent.
        ```cpp
        const auto scores = std::vector { 95, 88, 100 };
        ```
    * **Behavior Facet (`()`):** When a container initialization commands active resource allocation or size bounding, it belongs to the *Behavior Layer*. Use parentheses.
        ```cpp
        const auto buffer = std::vector<int>(1024, 0);
        ```
* **Resource-Owning Value Types (`std::string`):** Types that encapsulate complex resource management but act as pure values within application code should cleanly decouple left-hand value binding from right-hand behavior construction:
    ```cpp
    const auto dynamic_key = std::string("init_system_token");
    ```

### 3.4 Type Deduction (`auto` & CTAD)
* **Intent-Driven Deduction:** Embrace `auto` for variables and **CTAD** (Class Template Argument Deduction) for templated containers only when the naturally deduced type perfectly aligns with your semantic intent and is immediately obvious to the reader.
* **Explicit Over Brevity:** You must explicitly specify template arguments when forcing a specific type that differs from the default deduction (e.g., using `std::array<std::string> { "a", "b" }` instead of letting it deduce to `const char*`), or when the initialization expression is too complex to parse at a glance.
* **`const` by default:** Use `const auto` for every local variable that is not explicitly mutated. Mutable locals are the exceptional case.

## 4. Control Flow & Visual Hierarchy

### 4.1 Whitespace & Logical Block Layout
* **Whitespace as structure:** Whitespace is a semantic tool, not decoration. 
* **Logical blocks:** Separate logical blocks within a function with a single blank line. Do not double-space.

### 4.2 Control Flow Rules
* **if-with-initializer:** Use this structure to limit variable scope and unwrap optionals/pointers/expected values. 
* **Range-for on temporaries:** Run range-for directly on function-returned temporaries.
* **Loop increment:** Always use pre-increment `++i` in all loops.

## 5. Canonical Example: The Endpoint Parser

This example demonstrates the culmination of the design philosophy: compile-time evaluation, monadic error handling, scope-limited unwrapping, and the strict semantic mapping of initialization (`=`, `{}`, `()`).

```cpp
struct EndpointConfig final {
    std::string host;
    int port;
};

enum class ParseError {
    MissingDelimiter,
    InvalidPortFormat
};

// Pure function helper, compile-time capable
constexpr auto extract_port(std::string_view text) noexcept -> std::optional<int>;

// The Behavior Layer: Declarative signature, monadic error flow
constexpr auto parse_endpoint(std::string_view config_line) noexcept -> std::expected<EndpointConfig, ParseError> {
    // Value Semantics (=): Used for simple cursors and state markers.
    const auto colon_pos = config_line.find(':');
    if (colon_pos == std::string_view::npos) {
        return std::unexpected(ParseError::MissingDelimiter);
    }

    const auto host_view = config_line.substr(0, colon_pos);
    const auto port_view = config_line.substr(colon_pos + 1);

    // Control Flow: Scope-limited unwrapping of the optional value.
    if (const auto port_opt = extract_port(port_view); port_opt.has_value()) {
        // Data Semantics ({}): Aggregate initialization with a required space after type.
        // Behavior Semantics (()): Explicitly signaling string resource allocation.
        return EndpointConfig {
            .host = std::string(host_view), 
            .port = *port_opt
        };
    }

    return std::unexpected(ParseError::InvalidPortFormat);
}
```

## 6. Quick Reference

| Concept | Representation |
|---|---|
| Value | `=` |
| Behavior | `()` |
| Data | `{}` |
| Container Data Facet | `{}` |
| Container Behavior Facet | `()` |
| String Literal Construction | `const auto s = std::string("...");` |
| Natural Type Deduction | `const auto v = std::vector { 1, 2 };` (CTAD) |
| Forced Type Alignment | `const auto names = std::array<std::string> { "a", "b" };` |
| Semantic Record | Designated Initializer |
| Tuple-like Value | Positional Initializer |
| Optional Value | `std::optional<T>` |
| Error State | `std::expected<T, E>` |
| Private Invariant | `class final` |
| Pure Data | `struct final` + Free Functions |
| Read-only Parameter | `std::string_view` / `std::span` |
| Trivial Parameter | Pass by Value |
| Scope-limited Unwrap | `if`-with-initializer |
| Loop Traversal | Pre-increment `++i` |
| Compile-time Capable | `constexpr` |
| No-throw Guarantee | `noexcept` |
| Output | `std::print` / `std::println` |
| String Composition | `std::format` |
