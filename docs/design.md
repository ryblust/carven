# Design Philosophy

Carven is a programming language that transpiles to C++. It does not seek to replace
C++ — it offers C++ programmers a simpler, more elegant path to the same machine.

## Principles

### 1. Reduce cognitive overhead

C++ is powerful, but its syntax is verbose and carries decades of accumulated
complexity. Carven lets programmers express the same intent in fewer, clearer
lines — preserving C++'s semantic model while shedding the ceremony.

### 2. No backward-compatibility tax

C++ pays a permanent cost for its history: header files, the preprocessor, C
compatibility layers, and syntax frozen by standards decades old. Carven owes
none of these debts. It starts from a clean slate and keeps only what modern C++
actually needs.

### 3. Embrace modern language design

Type inference, expression-oriented control flow, pattern matching, and concise
functional constructs are the baseline, not afterthoughts. Underneath, the
semantics remain C++. The syntax reads like a language designed in the 2020s.

### 4. Trust the programmer

Carven does not pursue the ownership, borrowing, or lifetime guarantees of Rust.
It inherits C++'s "trust the programmer" philosophy — control is not surrendered
in the name of safety. This does not mean Carven is deliberately unsafe; it
adopts modern C++ best practices, but never at the cost of restricting what the
programmer can do.

### 5. Principle of least surprise

- A C++ programmer reading Carven code should broadly understand what it does.
- Novelty should never come at the expense of clarity. Carven is syntactic sugar
  for C++, not a new language.
- The generated C++ must remain readable and debuggable.

### 6. Context-free grammar

Carven's parser decides structure by token type alone — it performs no
symbol-table lookup. This means:

- The same syntax never produces a different AST depending on context.
- There is no C++ "most vexing parse" class of problem.
- Where semantic distinction is needed, the syntax makes it explicit (keywords,
  token types, delimiters).

### 7. Progressive complexity

- Fundamentals first: variables, functions, control flow, structs, enums.
- Advanced features on demand: pattern matching, generics, traits.
- Interop with C++ is direct and deliberate: inline C++ blocks, import
  pass-through.

## Influences

Carven's syntax is informed by several languages:

- **Rust** — `let`/`fn`/`match`, expression orientation, no-paren conditions
- **Zig** — compile-time execution, lean type system
- **Swift** — labelled arguments, readability
- **Kotlin** — modern syntactic sugar, concise expressiveness

Carven is none of these. It is C++, re-skinned.

## Syntax Conventions

- Types follow names (`name: Type`), consistent with modern C++ and Rust.
- Control-flow conditions omit parentheses.
- Blocks are preferred as expressions over statements (`if` returns a value).
