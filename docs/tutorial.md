# Carven tutorial

This tutorial introduces the basic forms used to write Carven programs.
It starts with a first program, then covers imports, types, functions, and their
bodies. Each snippet is a separate example, with declarations before their uses.

## A first program

Save this program as `main.cv`:

```carven
fn add(left: i32, right: i32) -> i32 {
    return left + right;
}

let answer = add(20, 22);
println(answer);
```

`fn` declares a function. Parameters state their types, and `-> i32` states
that `add` returns a signed 32-bit integer. Omitting the result type lets the
compiler infer it from returns; a body with no return operands infers `void`.
The top-level statements form an implicit program entry, and `answer` is local
to that entry. You can instead put those statements inside an explicit `fn main()`;
a compilation may have only one entry.

From the repository root:

```sh
./xmakew build
./xmakew run carven main.cv
./xmakew run carven interpret main.cv
./xmakew run carven compile --stdout main.cv
```

The first execution command compiles and runs a native program on POSIX hosts.
The second interprets the same source using the supported semantic subset. Both
print `42`. The third prints generated C++ and artifact headings for inspection.
Use `carven interpret --trace main.cv` to observe statement locations and calls.
In Windows PowerShell, use `.\xmakew.ps1`; native execution through the CLI is
currently POSIX-only, while generation and interpretation are available separately.

Imports form the start of a source file. Types and constants normally precede
functions that use them; helper functions precede their callers. Carven also
permits forward references to module declarations.

## Modules

Imports precede all other items in a source file. Put this function in `math.cv`:

```carven
fn answer() -> i32 {
    return 42;
}
```

A sibling `main.cv` selects and calls it:

```carven
import .math using answer;

fn main() {
    let _ = answer();
}
```

Supply both files to the compiler, for example
`./xmakew run carven compile --stdout main.cv math.cv`. Imports do not discover files.

An unprefixed module reference starts at the current craft's root. A leading
`.` starts in the importing module's directory. `json::parser` selects a module
in the external `json` craft. The reserved `std::` prefix selects the official
Carven standard library.

For a file at `crafts/foo/models/user.cv`:

| Import | Selected source |
| --- | --- |
| `std::utf.text` | `crafts/carven/std/utf/text.cv` |
| `std.utf` | `crafts/foo/std/utf.cv` |
| `.std.utf` | `crafts/foo/models/std/utf.cv` |

Use `using { first, second }` to select several names or `using *` for all
visible names.

Functions, structures, enums, and constants may be `private`, bare, or `export`.
Private declarations are module-local; bare means no visibility modifier and
allows use within the same craft; exported declarations are visible across
crafts in the compilation. Application modules outside crafts share one module
domain. Cross-module use requires an import and a visible declaration.

## Records and arrays

A structure groups named fields. An array has one element type and a fixed
length:

```carven
struct Point {
    x: i32,
    y: i32,
}

fn total() -> i32 {
    let point = Point { x: 20, y: 22 };
    let values: [i32; 3] = [1, 2, 3];
    return point.x + point.y + values[0];
}
```

Structure construction initializes every field. Positional construction is
also available: `Point { 20, 22 }`. Array indices start at zero.

## Enums and matches

An enum lists alternatives, optionally carrying values:

```carven
enum Value {
    Number(i32),
    Pair(i32, i32),
    Empty,
}

fn describe(value: Value) -> i32 {
    return match value {
        .Number(number) if number > 0 => number,
        .Number(_) => 0,
        .Pair(left, right) => left + right,
        .Empty => -1,
    };
}
```

Construct a case with `Value::Number(42)`, or `.Number(42)` when the surrounding
context supplies `Value`. A match chooses the first matching arm whose guard
succeeds and must cover all possible values. `_` ignores a payload. Alternatives
within a pattern can be joined with `|`.

## Functions

A function declares its parameters and result before its body. Parameters of an
ordinary function state their types. A function can state its result after `->`
or infer it from its returns. A value-returning function returns on every reachable
path:

```carven
fn add(left: i32, right: i32) -> i32 {
    return left + right;
}

fn next(value: i32) => add(value, 1);
```

An expression body follows `=>` and implicitly returns that expression. Its
result type can be inferred, as in `next`. Block bodies use the same inference
rule: return operands must independently determine a consistent type. An explicit
annotation supplies type context, for example when returning unsuffixed literals.
A body with no return operands infers `void`. A `void` call can be used as an expression body too. Recursive
result dependencies require enough explicit result types to break the cycle.

## Read, Write, and Take

An unmarked parameter reads a value. `&` permits mutation through a parameter;
`&&` transfers ownership. Calls repeat the parameter's access marker:

```carven
struct Point {
    x: i32,
    y: i32,
}

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

After `consume(&&point)`, `point` is unavailable. Initializing another owner
without `&&`, such as `let copy = point`, copies the value and leaves the
source available. Copies of views and aliases still refer to their original
backing storage.

## Bindings and values

Inside a function, `let` binds an immutable runtime value, `var` permits
assignment, and `const` names a compile-time value:

```carven
fn calculate() -> i32 {
    const unit_price: i32 = 10;
    let quantity = 3;
    var total = unit_price * quantity;
    total += 2;
    return total;
}
```

A binding can state its type after `:` or infer it from its initializer.
Unsuffixed integers normally use `i32`; unsuffixed floating literals use `f64`.
Other builtin types include `bool`, `char`, `str`, fixed-width integers,
`isize`, `usize`, and `f32`. Explicit numeric conversion uses `as`, for example
`quantity as i64`. Ordinary numeric values do not implicitly change type.

String literals use double quotes and default to `str`, a non-owning UTF-8 view.
A String context, such as `let text: String = "hello"`, creates an owning value.
An existing view can be copied with `view as String`. String values borrow
automatically in `str` contexts; `.as_str()` also creates an explicit view.
Borrowed views must remain within their owners' lifetimes and prevent mutation
while the borrow remains live. `String::from_str(text)` is also available for
explicit owning construction. Characters use single quotes.

For either text type, `text.len()` counts UTF-8 bytes and `text.is_empty()` tests
for empty text. `text.bytes` and `text.chars` can be traversed in a loop.
Interpolation such as `f"Count: {quantity}"` produces an owning `String`. A format
specification follows a colon inside a hole, as in `f"ID: {42:04x}"`.

`_` discards a binding name. Its runtime initializer still executes.
Use `//` for a line comment.

## Conditions and loops

Conditions use `bool`. An `if` can select a statement block or produce a value:

```carven
fn magnitude(value: i32) -> i32 {
    return if value < 0 { -value } else { value };
}

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

The range `1..4` visits 1, 2, and 3; `1..=4` also visits 4. Ranges are values:
`let interval = 1..4;` can be used with `for n in interval`. Their type is
`range<i32>` here, and their bounds are snapshots taken when the value is created.

A range pattern classifies an integer by interval:

```carven
fn band(value: i32) -> i32 {
    return match value {
        ..0 => -1,
        0..=100 => 0,
        101.. => 1,
    };
}
```

Bounds can also be runtime expressions; then include a fallback unless the
remaining static patterns already cover the domain. Favor `match` for classifying
one value and `if` for deciding actions from computed conditions.

A range loop can also traverse an array, `text.bytes`, or `text.chars`. `break` exits a loop and `continue` advances to
its next iteration. C-style loops use `for var i = 0; i < 3; ++i { ... }`.

Value branches end with a result expression without a semicolon. Logical
`&&` and `||` short-circuit; ordinary operands evaluate left to right.

## Recoverable failures

A function can declare failure types after its result. `throw` produces a
failure, `?` propagates it, and `try` handles it:

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

A contract may list multiple types, such as `throw ReadError + ParseError`.
Private functions and lambdas may infer their failure sets. Bare and exported
functions with failures state their contracts. Tests handle every failure.
`main` may propagate failures through an explicit `throw` contract; an escaping
failure produces a failure process status without printing its payload. A handler
may produce another failure; `rethrow` passes on the caught failure.

## Closures and callbacks

A lambda can use a block body or return one expression with `=>`:

```carven
fn use_callbacks() {
    let double = [](value: i32) => value * 2;
    let increment: fn(i32) -> i32 = [](value) => value + 1;
    let result = double(increment(20));
}
```

An expected callable view can supply omitted parameter and result types.
Without that context, parameter types must be explicit and the body determines
an omitted result type. Block-bodied lambdas can infer a result from their
return operands.

A lambda lists its captured runtime bindings in brackets. `[]` captures nothing,
`[value]` copies a value, and `[&value]` grants access to mutable storage:

```carven
fn invoke(callback: fn(i32) -> i32, value: i32) -> i32 {
    return callback(value);
}

fn scaled(value: i32) -> i32 {
    let factor = 2;
    let multiply = [factor](item: i32) { return item * factor; };
    return invoke(multiply, value);
}
```

`multiply` owns its closure and captured value. The parameter type
`fn(i32) -> i32` is a non-owning callable view; the call borrows `multiply`.
An inferred `let copy = multiply` instead owns a closure copy. A copied Write
capture still refers to the same mutable storage. Keep borrowed targets alive
for their uses.

## Calling C++

A header import supplies C++ declarations. A leading `::` names a global C++
entity; a `using` selection introduces a short name:

```carven
import <vector> using std::vector;

fn count() -> usize {
    var values = vector<i32> { 1, 2, 3 };
    values.push_back(4);
    return values.size();
}
```

C++ checks external types, members, overloads, and conversions. The return above
converts the external result to `usize`. Carven still checks access and ownership.
A header import without `using` introduces no short names. Include search paths
and linked libraries are supplied by the native build.

`import(cpp)` declares a scalar function provided by C++, while `export(cpp)`
publishes a Carven function to a C++ caller:

```carven
import "native/provider.hpp";

private import(cpp) fn native_answer() -> i32;

export(cpp) fn answer() -> i32 {
    return native_answer();
}
```

These explicit boundaries accept infallible scalar signatures. The provider
uses the same unqualified global C++ name. A top-level `#[cpp]` fragment can
also supply implementation C++ between matching fences of at least three `-`
characters.

Carven failures and C++ exceptions are separate. A C++ adapter must handle an
exception before it escapes a generated `noexcept` boundary if execution is
to continue.

## Acquiring an external address

Explicit `ptr` types let an external address enter ordinary Carven code. This
complete example uses a small native provider:

```carven
import <cstdint>;

#[cpp] ---
auto counter_address() noexcept -> std::int32_t* {
    static std::int32_t counter = 0;
    return &counter;
}
---

fn observe(p: ptr<i32>) -> i32 {
    if p == nullptr { return 0; }
    return *p;
}

fn main() {
    let p: ptr<&i32> = ::counter_address();
    if p != nullptr { *p += 1; }
    let value = observe(p);
}
```

The `let` keeps the address slot fixed. `ptr<&i32>` permits writing the target;
`observe` receives a narrowed `ptr<i32>` address value and checks it locally.
For object targets, `p->field` abbreviates `(*p).field`. Use `var` and a Write
parameter `&p` when a helper must replace the address slot. Copying or taking a
pointer does not release or retain the external object. Its owner or provider
still determines how long it lives and how it must be released.

## Tests

A test is a named module-local body:

```carven
fn add(left: i32, right: i32) -> i32 {
    return left + right;
}

test "addition produces the expected value" {
    let answer = add(20, 22);
    check(answer == 42);
    require(answer > 0, "answer must be positive");
}
```

A failed `check` reports and continues. A failed `require` reports and exits
the test. `fail()` reports and exits unconditionally. Tests are analyzed with
the source; generating a test executable requires selecting test emission.

## Compile-time tests

Use `const test` to execute a test during compilation. It shares the ordinary
`check`, `require`, `fail`, and print operations, within the supported constant
execution subset:

```cv
const fn sum(size: i32) {
    var result = 0;
    for value in 0..size { result += value; }
    return result;
}

const test "sum at compile time" {
    println("checking sum");
    check(sum(5) == 10);
}

test "sum through generated C++" {
    check(sum(5) == 10);
}
```

The first test runs during Carven compilation and produces no runtime test
function. A failed check makes compilation fail. The ordinary test runs through
the generated test runner, checking the C++ implementation. Calling a `const fn`
at runtime still executes it at runtime.
