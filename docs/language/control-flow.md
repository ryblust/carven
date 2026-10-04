# Evaluation and control flow

[Language](README.md)

This page defines operand evaluation, branches, loops, and pattern selection.

- [Operators and evaluation](#operators-and-evaluation)
- [Control flow and loops](#control-flow-and-loops)
- [Patterns and matches](#patterns-and-matches)

## Operators and evaluation

The operand domains below apply to Carven operations. External operands
use the delegated operation and conversion rules.

Logical negation requires `bool`; numeric negation requires a numeric operand;
bitwise complement requires an integer. Arithmetic operators require identical
numeric operands. Remainder, bitwise, and shift operators require integers.
Ordering requires identical numeric operands. Logical `&&` and `||` require
`bool` and short-circuit from left to right.

Equality requires compatible operands and an equality-capable type. It is
available for `bool`, `char`, integers, floating-point values, `str`, `String`, arrays
whose elements support equality, numeric enums, payload enums whose payloads all
support equality, and pointers with identical target types. Structures and classes
do not support `==` or `!=`. Callable types, process-entry arguments, slices, and
`str.chars` iteration views do not support equality.

Payload enum values with different cases compare unequal. Values of the same
case compare payloads in position order with short-circuiting. Floating-point
equality follows IEEE `==`; `!=` is its negation.

All Carven expression evaluation is left to right and exactly once. This
includes callee before arguments, binary left before right, receiver before
index, assignment target before value, and initializer clauses in source order.
Short-circuiting operators and control expressions evaluate only the selected
operands or branches.

Every source operand and branch receives operation, result-compatibility, and
failure-consumption checks, including after a terminal statement. Source that
follows a terminal statement contributes no runtime evaluation, outward
failure, ownership transition, or reachable-use evidence. A nonreturning expression can occupy a position whose type is already
known. If its type cannot be determined, the enclosing operation is rejected;
for example, a call still needs a callable type and match still needs a subject
type. Nonreturning control does not exempt later source from type checking.

## Control flow and loops

Conditions and guards require `bool`. Value-form `if` requires an `else` and
all result branches must be compatible. Statement-form conditionals do not
produce a value.

The value of a condition does not change analysis. Reachability, failure
contracts, ownership, pointer proofs, and return analysis consider every branch
of `if`, `&&`, `||`, `match`, and a loop with a condition, whether the condition
is a runtime value, a literal, or a `const`: `if false { ... }` and
`while true { ... }` retain both paths. A loop is known not to end by itself
only when it has no condition, written `while { ... }`; it then ends through
`break`. `const if` selects the arm that executes
and is generated; its arms follow the same analysis.

`while` evaluates its condition before each iteration; without a condition it
repeats until `break`. A C-style `for` creates one loop scope, evaluates its
initializer once, tests its required condition before each iteration, executes
the body, then evaluates step clauses in source order. `continue` in a C-style `for` proceeds
to its step clauses; `break` exits the loop.

An integer range value has type `range<T>` for a builtin integer `T`. Expressions
`begin..end` and `begin..=end` evaluate both bounds once, left to right, and own
snapshots of one compatible integer type. The former excludes the upper bound;
the latter includes it. Ranges support ordinary storage, copying, parameters,
returns, and static execution. They do not own element storage or borrow their
bound expressions. Omitted bounds are supported only in patterns.

An integer-range loop snapshots its source once. Changing the source range or
its original bounds during iteration does not change the sequence. Iteration
ascends; a reversed range is empty, and equal endpoints produce zero elements
for `..` or one for `..=`. Closed intervals may include the type maximum without
overflow. Integer-range bindings cannot use Write access.

Arrays support Read and, for a mutable source, Write range bindings. Slices
and `str.chars` support Read bindings only. A range binding is scoped to the
loop and cannot be taken. Its name is not visible in its declared type or range
source; it is published only after those inputs complete and is then visible
throughout the loop body. Array iteration retains access to its source owner
until the loop exits, so the source cannot be taken during traversal. Writing
an element does not restore an unavailable whole-array owner.

Array and text-range loops access an element only after confirming that the
cursor is within the range. The terminating check does not access an element.

`break` and `continue` are valid only inside a loop. `return` targets the
current function or lambda. A value-form `if`, `match`, or `try` is a control
boundary: its result branches cannot return from an enclosing callable or
break/continue an enclosing loop, though a transfer may target a loop nested
within that branch. A `const` block is the same kind of boundary. An invalid
crossing uses
`CV-FLOW-TRANSFER-BOUNDARY`.

## Patterns and matches

Both value and statement matches must be exhaustive. A value match also
requires compatible arm results. The subject expression is evaluated exactly
once. An rvalue subject is retained across guard rejection. When the subject
denotes a place, its receiver and index expressions identify that place once,
and selection requires that storage to remain stable. During a guard, obtaining
Write or Take access to overlapping subject storage is invalid, including
through aliases or callable captures. Guards may modify other storage, call
functions, and produce failures. A selected arm's body may modify the subject.
The first arm whose pattern matches and whose optional guard succeeds is selected.

Integer range patterns accept `a..b`, `a..=b`, `..b`, `..=b`, and `a..`.
Bounds have the subject's integer type. Each present bound evaluates once from
left to right when its pattern is attempted, before testing containment. Enum
case rejection, earlier payload rejection, and successful or-pattern alternatives
skip later bound evaluation. Reversed and empty intervals never match. Bound
failures propagate outward; bounds cannot obtain Write or Take access to the
match subject, and cannot reference bindings introduced by the same pattern.

Directly known, execution-free bounds contribute to coverage. Runtime
bounds provide no coverage proof; use a fallback when constant patterns are
insufficient. Effectful bounds retain their evaluation even when their result is
known.

Patterns are recursive. Enum payload positions admit case, literal, binding,
wildcard, integer range, `is`, and or-patterns. Structure and array destructuring
are not supported. Payload arity must be exact. A bare identifier creates an immutable
owning binding and never pins a constant. The selected payload is copied once
after its case matches and before the guard runs.

`is T` constrains the subject to `T`. The subject must already have a compatible
canonical type, so the constraint covers that type.

Or-pattern alternatives must bind the same names with the same types; an
alternative cannot bind one name twice. Guards run after pattern bindings are
available. They affect arm selection but do not contribute exhaustiveness
coverage.

Repeated alternatives and alternatives subsumed by another alternative in the
same or-pattern are errors. An arm whose complete pattern is covered by
preceding unguarded arms is unreachable. Missing-case diagnostics use a shortest
deterministic witness. Literal identity uses normalized language equality,
including treating `0.0` and `-0.0` as one floating pattern.

An unreachable match arm produces `CV-FLOW-UNREACHABLE-MATCH-ARM` at the arm's
pattern span. This warning does not prevent target artifacts from being
generated. The arm remains part of parsing and Carven semantic checking, but
it does not participate in runtime arm selection.
