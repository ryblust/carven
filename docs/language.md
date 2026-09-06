# Language guide

Carven is a source language that compiles `.cv` modules into C++ artifacts. This
document introduces the shape of supported Carven source, following
the order in which a reader normally encounters it in a file.

This guide is nonnormative. Its examples present the ordinary language surface
without enumerating every validity, evaluation, ownership, failure, or C++
interoperation rule.

## A source file

A source file contains one contiguous import prefix followed by top-level
declarations, tests, and optional C++ source fragments:

```carven
import .math using answer;

const expected: i32 = 42;

struct Result {
    value: i32,
}

fn compute() -> Result {
    return Result { value: answer() };
}

test "result uses the imported answer" {
    check(compute().value == expected);
}
```

Imports must precede every other top-level item. Declarations after the import
prefix are not required to follow dependency order: module signatures and
compile-time facts may refer forward, and functions may be mutually recursive.

## Imports

An import names a module and selects one name, a list of names, or every visible
name from it. The same prefix may also contain C++ header imports:

```carven
import math using answer;
import .constants using *;
import json::parser using parse;
import geometry.vector using { Point, length };
import <cstdint>;
import "native/provider.hpp";
```

An unprefixed reference starts at the current module domain root. A reference
beginning with `.` starts in the importing module's logical directory, so
`.math` in module `a.b.main` denotes `a.b.math`. This form stays inside the
current module domain. `name::path` selects a named craft domain. Resolution
uses logical module paths and considers only source modules explicitly supplied
in the same compilation; it does not discover files.

C++ header imports supply opaque include spellings to the downstream C++ build.
They do not create Carven names, resolve files, or link libraries.

## Top-level declarations

Modules declare constants, structures, enums, functions, and tests. Constants,
structures, enums, and functions may be `private`, bare, or `export`:

```carven
private const retry_limit: i32 = 3;

export struct Point {
    x: i32,
    y: i32,
}

export fn origin() -> Point {
    return Point { x: 0, y: 0 };
}
```

`private` declarations are visible only in their defining module. Bare
declarations are visible within their module domain. `export` declarations are
visible throughout the compilation. A cross-module use always requires an
import. Tests are always module-local.

Module `const` declarations are compile-time values and create no runtime
storage. Local `const` bindings have the same compile-time character.

## Types and values

The source-spellable builtin types are `bool`, `char`, `str`, `void`, fixed-width
signed and unsigned integers, native-size `isize` and `usize`, and `f32` and
`f64`. `void` denotes the absence of a value rather
than a value-bearing type. Structures and enums introduce nominal types; arrays
include their element type and extent in their type.

```carven
fn values() {
    let enabled: bool = true;
    let count = 42;
    let label: str = "carven";
    let items: [i32; 3] = [1, 2, 3];
    let point = Point { x: 20, y: 22 };

    if enabled && !label.is_empty() {
        let _ = count + items[0] + point.x;
    }
}
```

An annotation may state the type explicitly; otherwise the initializer supplies
it when one concrete type can be selected. Carven does not apply general
implicit numeric promotions. Explicit numeric conversion uses `as`.

## Functions and calls

Functions state their parameters and may state a result after `->`. Omitting the
result means `void`.

```carven
fn add(left: i32, right: i32) -> i32 {
    return left + right;
}

fn record(_: i32) {}

fn main() {
    let answer = add(20, 22);
    record(answer);
}
```

The callee and arguments are evaluated exactly once from left to right. A
compilation may contain at most one function named `main`; it is the process
entry point and must handle every recoverable failure before completion.

## Bindings and access

`let` creates an immutable runtime owner, `var` creates a mutable runtime owner,
and `const` creates a compile-time binding. Function access is visible both in
the parameter and at the call site:

```carven
fn increment(&value: i32) {
    value += 1;
}

fn consume(&&point: Point) -> i32 {
    return point.x + point.y;
}

fn use_values() -> i32 {
    var counter = 41;
    increment(&counter);

    let point = Point { x: 20, y: 22 };
    return counter + consume(&&point);
}
```

An unmarked parameter reads without taking ownership. `&` grants mutable Write
access without ownership transfer. `&&` transfers ownership and makes the
source binding unavailable; a complete assignment may restore a taken `var`.

## Structures, arrays, and enums

Structures are nominal products. Construction may name every field or supply
values in declaration order. Arrays have a fixed extent.

Enums are closed nominal sums and may contain nullary or payload cases:

```carven
enum Value {
    Number(i32),
    Pair(i32, i32),
    Empty,
}

fn first(values: [i32; 3]) -> i32 {
    return values[0];
}

fn number(value: i32) -> Value {
    return .Number(value);
}
```

An expected enum type permits the contextual `.Case` form. `Value::Number`
names the same case explicitly through its owner.

## Expressions and control flow

Conditions require `bool`. `if` may be a statement or a value when it has a
compatible result on every branch:

```carven
fn sign(value: i32) -> i32 {
    return if value < 0 {
        -1
    } else if value > 0 {
        1
    } else {
        0
    };
}
```

Carven provides `while`, C-style `for`, range iteration, `break`, and
`continue`:

```carven
fn accumulate() -> i32 {
    var total = 0;
    for value in 1..4 {
        total += value;
    }

    while total < 10 {
        total += 1;
    }
    return total;
}
```

Ordinary expression evaluation is left to right. `&&` and `||` short-circuit;
control forms evaluate only the selected branch.

## Patterns and matches

`match` selects the first matching arm and must be exhaustive. Patterns may
inspect enum cases and payloads, bind values, discard with `_`, combine
alternatives with `|`, and use guards:

```carven
fn describe(value: Value) -> i32 {
    return match value {
        .Number(number) if number > 0 => number,
        .Number(_) => 0,
        .Pair(0 | 1, item) => item,
        .Pair(_, _) => -2,
        .Empty => -1,
    };
}
```

The match subject expression is evaluated once. A value match requires
compatible results from every arm. Guarded place-subject redispatch is defined
precisely by [Semantics](semantics.md#patterns-and-matches).

## Failure contracts

Recoverable failures are part of a callable's contract. A function may declare
a closed failure set after its success result. `throw` produces one of those
values, postfix `?` propagates pending failures to an enclosing `try` or
callable boundary, and `try` handles them:

```carven
struct ReadError {
    code: i32,
}

fn read(ok: bool) -> i32 throw ReadError {
    if ok {
        return 42;
    }
    throw ReadError { code: 7 };
}

fn recovered() -> i32 {
    return try {
        read(false)?
    } catch {
        ReadError(error) => error.code,
    };
}
```

Private functions and lambdas may infer their failure set. A bare or exported
function with failures states its contract explicitly. `main` and tests handle
every failure before completion.

## Lambdas and callable views

A lambda has a unique closure type and lists every captured runtime binding.
Value captures take immutable snapshots; `&` captures provide Write access:

```carven
fn invoke(callback: fn(i32) -> i32, value: i32) -> i32 {
    return callback(value);
}

fn doubled(value: i32) -> i32 {
    return invoke([](item: i32) { return item * 2; }, value);
}
```

An expected callable view may supply omitted lambda parameter and result types.
Without an expected view, parameters state their types and the result may be
inferred from returns. Callable views are non-owning and retain the access,
parameter, result, and failure shape of the callable.

An inferred binding such as `let copy = closure` owns a closure copy. A typed
binding such as `let view: fn(i32) -> i32 = closure` borrows its target instead.
Copies retain any Write aliases inside their captured values. Neither copying
nor borrowing extends the lifetime of those referents. Calling a closure does
not implicitly snapshot its captures. See the
[closure contract](semantics.md#lambdas-and-callable-views) for creation,
assignment, invocation order, escape restrictions, and examples.

## Tests

A test is a named module-local body. `check` reports a failed condition and
continues; `require` reports and exits the current test; `fail` reports and exits
unconditionally:

```carven
test "arithmetic produces the expected value" {
    let answer = 20 + 22;
    check(answer == 42);
    require(answer > 0, "answer must be positive");
}
```

Tests are parsed and analyzed with the rest of their compilation. The selected
test-emission mode determines whether generated artifacts contain and invoke
them.

## C++ interoperation

Carven can name C++ types and functions with a leading `::` or through a
header import's `using` clause. For example, `import "parser.hpp";` supplies
the header for `::parse_port(text)`. The selection
`import "parser.hpp" using parse_port;` introduces the short name `parse_port`.
The runnable [C++ parser example](../examples/interop/importing/) shows the
provider header and its Carven caller together.

An explicit `import(cpp)` declares a scalar function capability backed by a
global C++ provider. A C++ header import makes a declaration available to
the generated module, while the definition may be inline in that header or
supplied by a linked C++ source:

```carven
import "native/provider.hpp";

private import(cpp) fn native_answer() -> i32;

export(cpp) fn answer() -> i32 {
    return native_answer();
}
```

`export(cpp)` publishes an ordinary Carven function to C++ consumers. An
imported capability is never re-exported directly; widening it requires the
explicit Carven wrapper shown above.

A top-level C++ source fragment places byte-opaque C++ in the generated module
implementation:

```carven
#[cpp] ---
#define NATIVE_FLAG 1

template<typename T>
struct native_traits;
---
```

The opening and closing fences contain the same number of at least three `-`
characters. A matching fence line inside the payload closes the fragment; use a
longer fence when those bytes are needed. Carven does not parse the payload.
Fragments are implementation-only and do not contribute to a public C++
header.

The `import(cpp)` / `export(cpp)` boundary supports concrete, infallible scalar
functions. The provider has the same unqualified name at global C++ scope;
`export(cpp)` declarations use a self-contained `carven/api/<module>.hpp`
header. Exact validity, type mapping, Unicode checks, and C++ responsibilities
are defined by
[semantics.md](semantics.md#c-interoperation). Generated paths and public-output
roles are defined by
[toolchain.md](toolchain.md#artifact-paths).

C++ adapters handle native exceptions before returning to Carven. They may
recover internally or return an application-defined result for Carven to
interpret. An adapter can live in a header, a linked source file, or a `#[cpp]`
fragment. The [native exception boundary](semantics.md#native-exception-boundary)
defines exception behavior for both imported operations and exported functions.
