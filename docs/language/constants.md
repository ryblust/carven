# Values and constants

[Language](README.md)

This page defines constant values, static execution, and result
freezing. A program has two stages. The static stage executes during
compilation; the runtime stage is the generated program. `const fn` states that
a function can execute in the static stage. A `const` declaration, `const if`,
`const for`, `const { ... }`, and `const test` state that execution happens
there. A constant is the completed, frozen value such execution produces.

- [Module constants](#module-constants)
- [Constant expressions](#constant-expressions)
- [`const` blocks](#const-blocks)
- [Static execution of functions](#static-execution-of-functions)
- [Frozen constant slices](#frozen-constant-slices)

## Module constants

A top-level `const` creates a module-scope name for one typed constant:

```carven
private const radix = 10;
const retry_limit: i32 = 3;
export const protocol_version: u32 = 1;
```

Private and bare module constants may infer a unique concrete type or state it
explicitly. An exported constant must state its type; a literal suffix in the
initializer is not a declaration annotation. Omitting that type produces
`CV-CONST-EXPORTED-TYPE`. The annotation supplies the initializer's expected
type and uses ordinary compatibility rules. The target must be a named
identifier, so top-level `const _ = ...;` is invalid.

Module-constant initialization is elaborated from resolved declaration
dependencies rather than source order. Every successful use selects the same
normalized value; a dependency cycle produces `CV-CONST-CYCLE`. A module
constant has Read access only and introduces no runtime binding, source address,
or linkage. A frozen slice's contents have program-lifetime backing storage; that storage
does not give the source declaration an identity.

## Constant expressions

Carven evaluates `const` initializers during semantic analysis. Constant
facts include scalar and string literals, numeric enum cases, resolved local and
module constants, grouping, supported casts, supported pure unary and binary
operations, constant `str.len()` and `str.is_empty()`, and payload-case
construction whose payloads are all constant. Direct interpolation within the
supported builtin subset, pure String construction and text queries, and direct
calls to explicitly declared `const fn` functions with constant arguments can
also produce a constant when the executed operations are supported.
Fallible calls require explicit `?`; an actual failure escaping the static
root produces a diagnostic, while successful results may form constants.
Owning text can be passed between these expressions; only the completed
initializer freezes its result to `str`. Fixed-array literals, struct
construction, indexing, and field access also produce constants for the
aggregate types admitted during static execution. Arrays support equality
when their element type supports it. Arrays retain `[T; N]`; structs retain their
nominal type.
An explicit `[T]` constant annotation or `as_slice()` in a constant initializer
retains a completed array as a frozen slice. Its `len`, `is_empty`, indexing,
and `slice` queries can also produce constants. Calls to ordinary `fn` and direct
control-flow expressions are not Carven constant expressions. Local and module
constant declarations exist only in the static stage; their later uses denote the
selected normalized value without creating a runtime binding or lambda capture.

A body-local `const` executes where it stands in the static stage: once for
each selected instance or expanded occurrence, and not at all in an unselected
`const if` arm or an empty `const for`.

Types are checked in every arm before any stage executes. An array extent, a
C++ construction type query, or a separately constructed body that names a
local constant reads its value by computing the initializer. That computation
is part of checking the type: it writes no output, its failure is reported even
in an unselected arm, and it requires a value independent of unbound static
parameters. The declaration still executes in its own stage, and a `const fn`
result depends only on its arguments, so both yield the same value.

Static values do not guide ordinary control analysis. Only
`const if` selects by a static value.

A constant integer cast to an `N`-bit integer reduces the mathematical value
modulo `2^N`. An unsigned target is that residue; a signed target interprets the
same bits as an `N`-bit two's-complement value. Accepted constant casts remain
constant facts.

Integer literals are range checked for their selected type. Integer negation,
addition, subtraction, multiplication, and left shift wrap to that type's width
in both static execution and runtime execution. Division by zero and invalid
shift counts are diagnostics when evaluated during compilation; runtime execution
terminates for those conditions. Evaluation never relies on undefined host
arithmetic. A wrapping operation in an ordinary `let` initializer follows the
same rule as one in a constant initializer.

Floating literals, supported casts, and equality can supply constant facts.
Static execution supports floating arithmetic and ordering; for
example, `const sum = 1.0 + 2.0;` evaluates to `3.0`. Ordinary runtime floating
arithmetic and ordering do not acquire optional constant facts from host execution;
they retain the target's native operations. Negation of a numeric literal is
normalized with its sign during literal checking, including through grouping
parentheses.
Thus `-(2147483648)` is a valid `i32` minimum even though the positive literal
alone is out of range.

A known runtime result does not make an expression admissible in a `const`
initializer. For example, `(source() == 1) && false` still contains a general
call and is not a constant expression. In a runtime expression, knowing the
result does not remove evaluation of operands that execute under the ordinary
short-circuit rules.

## `const` blocks

`const { ... }` executes a statement block during compilation. An optional
string after `const` labels diagnostics, as in `const "prepare table" { ... }`.
The label is not a declared symbol and need not be unique. A block may appear
at module scope or wherever a block accepts statements:

```carven
const {
    var total = 0;
    for index in 0..10 { total += index; }
    println(total);
}

fn resize(const width: i32) {
    const for lane in 0..3 {
        const {
            assert(width > 0);
            println(f"lane {lane} of width {width}");
        }
    }
}
```

A block at module scope executes once. A block in a function, lambda, or test
body is a block of that body's static stage, as a local `const` is a binding of
it: it executes once for each selected instance or expanded occurrence and not
at all in an unselected `const if` arm or an empty `const for`. Runtime control
does not select it, so a block in an uncalled function, a runtime branch, or a
runtime loop executes once for its body, and runtime execution never repeats it.

Blocks use the shared static-execution type and operation rules, including
mutable locals, control flow, aggregates, text, `const fn` calls and failure
recovery. Type checking, ownership and lifetime validation apply to their
statements. External C++ construction and calls are outside this execution
subset. Execution runs in the Carven executor; it does not compile or execute
C++ providers.

A block has its own lexical scope. Module constants and the enclosing body's
static bindings (local constants, `const` parameters, and `const for` indices)
are available through ordinary lookup and shadowing. Runtime parameters and
locals of the enclosing body do not exist while the block executes and cannot
be read, written, or captured. Declarations and object lifetimes end within the
block. All locals inside the block are static values and execute in source
order. A local `const` is an immutable local at this stage; `const if` and
`const for` follow the same execution order as `if` and `for`. Nested blocks
can read enclosing static locals and retain their own diagnostic labels.
These rules also apply inside `const test`; they do not change `const fn`
body specialization.

A block is not a callable: `return` cannot leave it, and `break` or `continue`
reach only loops inside it. Blocks in one body execute in source order with its
local constants. Execution order between bodies and module-scope blocks is
unspecified. Imports resolve declarations and immutable values; they do not
establish initialization ordering between blocks. Output and diagnostics are
visible to the compiler's caller and cannot be read back by another block.

Explicit `?` propagates a fallible call to the block's execution boundary.
Escaping failures, execution errors and exhausted budgets are compilation errors.
Completed output remains observable if execution or later validation fails.

`check`, `require`, and `fail` need a test: a block in a test body reports to
that test during compilation, and a block elsewhere rejects them. Blocks execute
independently of test-artifact selection, including in `check`, and produce no
runtime code or test entries.

## Static execution of functions

Static roots can call only explicitly declared `const fn` Carven
functions, including in local and module constant initializers, array extents,
`const` blocks, and static tests. Arguments follow the declared type and access
rules. Every function body undergoes ordinary type, effect, ownership, and
lifetime validation. A `const fn` can also run at runtime; a runtime call remains
an ordinary call even when all arguments happen to be known.

A [static parameter](functions.md#static-parameters) is a separate input contract.
It requires an admitted constant source in every execution mode. Executor-known
values of ordinary parameters, `let`, and `var` do not acquire that qualification.
The compiler checks the contract before execution; executing an entire function
in the static stage does not make its ordinary parameters static inputs.

Each `const fn` definition checks its signature and semantically reachable
operations for executor capability. Reachable calls, including through local
callable bindings, must select a known `const fn`. Unsupported operations or
unproven callees produce `CV-CONST-ADMISSION` at the definition.
Operations after an unconditional return still receive ordinary semantic checks
but need no executor capability. Paths
selected by call arguments remain part of the definition's capability check.

Execution checks budgets and dynamic errors. Completed static roots also
check result publication. Language arithmetic and effects are the same in
both stages.

```carven
const fn label(count: i32) -> String {
    var result = String {};
    for index in 0..count {
        result.append_format(f"{index:02}");
    }
    return result;
}

const name: str = label(3); // "000102", with no runtime String owner
const fn decorate(value: String) -> String => f"[{value}]";
const decorated = decorate(label(3)); // "[000102]"
```

Function signatures follow ordinary rules, including Read, Write, and Take
parameters. A void result cannot initialize a constant; non-void results must
be eligible for the consuming constant boundary. Static execution supports
scalar arithmetic, comparisons, logical operations and casts; local
initialization, assignment and Take; `if`, `match`, `while`, C-style loops,
integer ranges, array and slice loops; `return`, `break`, `continue`, direct
calls to `const fn`, and indirect calls through local bindings of named
`const fn`. Match supports builtin and enum subjects
with literal, integer range, enum-case, binding, wildcard and or patterns, including guards.
Recursion is allowed when signatures and bodies can be completed without a
construction dependency cycle; the `const fn` capability check handles its
recursive direct-call graph without proving termination.

Text operations include `String {}`, `String::from_str`, the owning
`str as String` conversion, `as_str`, `len`, `is_empty`, `append`, `append_format`,
`push`, `clear`, byte views and iteration, and interpolation. Constant formatting
accepts default integer, bool, char and text formatting, including known C string contents, plus integer
`b`, `B`, `o`, `d`, `x` and `X` presentations with decimal width and optional zero
padding. Floating formatting and printing use native standard-library conversion
for f32/f64. Floating specifications accept alignment, Unicode fill, sign,
alternate form, zero padding, width, precision,
and `a/A/e/E/f/F/g/G` presentations, without locale-dependent `L` formatting.
Dynamic integer widths and floating widths/precisions evaluate before formatting;
their values must be nonnegative integers. Width, precision, and output bytes are
bounded by the constant text budget. Static execution reports invalid
or unsupported specifications instead of deferring them to runtime.

Fixed arrays support construction, indexing, element assignment, equality,
independent copies, Read and Write iteration, whole-binding Take, and function
parameters and results.
Structs support positional and named construction, field access and assignment,
independent copies, whole-binding Take, and parameters and results.
Enums support case construction, payload matching, equality, copies, Take, and
parameters and results. Fields, elements, and payloads follow the same execution
type rules recursively; publishing a completed constant also requires each
component to support freezing. Construction evaluates initializers in source
order. Freezing preserves nominal identity, array extents, and component types;
it does not convert owning String fields or elements to `str`.
Empty array literals require an expected element type.

Read arguments containing array or String storage observe their contents after
all arguments have evaluated. Other supported values are snapshotted at their
argument position. Field and index projections follow the selected value's type.
Slice operations, including `as_slice().len()`, indexing, and subslicing, can
execute against live backing storage. Slice chains preserve the selected element
identity during execution. Completed results become frozen slices at a constant
initializer; runtime copies use the ordinary slice API and borrowing rules.

String values retain owning copy and Take behavior within and between constant
function calls. Read String operands observe their contents after all arguments
or interpolation holes have evaluated, following the ordinary evaluation order.
Only the completed constant initializer freezes an owning result into immutable
`str` bytes. Consequently `const text = label(3)` has type `str`, and an explicit
`String` annotation on that constant is invalid.

Typed failure contracts use the ordinary language rules during static execution:
`throw` creates a typed payload, `?` propagates it, and `try/catch` matches its type
and payload. Guards, alternatives, nested recovery and `rethrow` work with the
same execution machinery as calls and control flow. A catch binding can be taken
without consuming the original failure retained for `rethrow`. Failure payloads
can contain owning text and supported aggregates.

At a `const` initializer or array extent, `fallible()?` explicitly
propagates to the evaluation entry: a successful result can form the constant;
an escaping failure produces a diagnostic at its throw site with call context.
An unmarked fallible call remains invalid even when it succeeds during evaluation,
and `?` on a failure-free expression remains invalid. Inferred contracts are
validated by the same failure solver as ordinary bodies. `const test` retains its
static rule that no typed failure may escape the test. Evaluator errors, exhausted
budgets and failed test assertions cannot be caught as typed failures.

Static execution uses the same admitted Carven function bodies as
ordinary execution, including local pointer creation, Read and Write calls,
and dereference of live local targets. The evaluator tracks each local object's
lifetime across calls and scopes. A pointer to an object after its scope exits
or after Take fails on dereference with `CV-CONST-EVALUATION`, even if that
storage is assigned again. An ordinary assignment to a live object preserves
its identity. Non-null local pointers cannot become published constant values.

Native operations, calls through callable values without an executable Carven
body, text character iteration, and unchecked borrowed text construction remain outside
the static execution subset. Ordinary type, access, ownership and lifetime
validation applies throughout execution.

Integer operations during static execution use the same wrapping
arithmetic as runtime calls. Division by zero and invalid shifts produce
diagnostics when executed during compilation. Short-circuiting and control flow select what executes, while all
source remains subject to ordinary semantic validation.

Each evaluated call tree has a budget of 100,000 execution steps and 128 nested
calls. Text construction is limited to 1 MiB per value and 8 MiB of accumulated
text construction and copying work. Appending charges the added bytes, and
queries do not copy their receiver. This is a work budget, not a live-memory limit.
An aggregate value is limited to 65,536 array-element and struct-field slots
across its nested aggregates and 64 aggregate levels. A call tree permits
524,288 accumulated slot constructions and copies. Direct aggregate initializers
use the same value limits and a bounded construction-work budget.
Constant format specifications additionally have bounded
nesting. Exhausting a budget diagnoses the static execution; it does not
fall back to runtime execution. Definition violations use `CV-CONST-ADMISSION`,
execution-specific failures use `CV-CONST-EVALUATION`, and budget failures use
`CV-CONST-LIMIT`; type, arithmetic and dependency diagnostics remain
applicable.

`char::from_u32_unchecked` is admitted in constant initializers and `const fn`
bodies. The executed operand must be a Unicode scalar value; surrogates and
values above U+10FFFF produce `CV-CONST-EVALUATION`. Successful construction
produces an ordinary `char` constant with the same publication rules as a literal.

## Frozen constant slices

A constant initializer can retain a completed array as `[T]`,
using an explicit annotation or `as_slice()`:

```carven
const table: [i32] = [2, 4, 6];
const middle = table.slice(1, 3);
const count = middle.len();
fn table_view() -> [i32] => table;
```

The elements have program-lifetime backing storage, so these views can be copied, stored,
and returned independently of the scope containing their constant declaration.
The declaration and its backing have no source-level address identity; native
code must not rely on different uses or generated artifacts sharing an address.
Empty slices retain their element type. Indexing and half-open subslicing are
checked during static execution; invalid evaluated bounds produce a diagnostic.

Elements must be admitted during static execution and freeze without changing
their declared type. Element types are preserved:
`[Entry; N]` becomes `[Entry]`, and `[[i32; 2]; N]` becomes `[[i32; 2]]`.
Struct fields keep their declared types. There is no recursive conversion of
owning fields or nested arrays to views. Slice comparison remains unsupported.

Array results of static function calls can reach this initializer boundary.
During static execution, slice parameters, locals, indexing, and subslicing
borrow live backing storage. Copies and chained slices retain that backing
relationship; they do not extend its lifetime. A completed static root freezes
the selected elements into immutable program-lifetime backing before releasing the
evaluation storage.
Ordinary runtime array-to-slice conversions still borrow their source storage;
known contents alone do not extend that storage's lifetime.

Retaining an array and constructing a constant subslice charge their number of
element references against the initializer's 524,288-element work budget. The
source array retains its value-size and nesting limits.
