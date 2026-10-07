# Bindings, access, and mutation

[Language](README.md)

This page defines binding roles, Read/Write/Take access, value transfer, and
lifetime rules shared by language operations.

- [Read values and aliases](#read-values-and-aliases)
- [Ownership transfer and mutation](#ownership-transfer-and-mutation)
- [Value ownership and lifetime](#value-ownership-and-lifetime)

Carven separates value type, binding role, and access mode. Binding and argument
markers `&` and `&&` describe access; they do not construct reference types.
Inside `ptr<&T>`, `&` is the pointer type's explicit target-access parameter.

- `let` creates an immutable runtime owner.
- `var` creates a mutable runtime owner.
- A local `const` creates a static binding.
- An unmarked parameter has `Read` access.
- A parameter marked `&` has `Write` access.
- A parameter marked `&&` has `Take` access.
- An unmarked range binding has `Read` element access.
- A range binding marked `&` has `Write` element access when the source permits
  mutation.

Every binding declaration has an initializer. An optional declared type must be
compatible with that initializer. The exact target `_` creates no symbol, may
be repeated, and is never an unused-warning candidate. `_name` is an ordinary
identifier. A discarded runtime initializer is still evaluated and its value
lives until the enclosing scope ends; local `const _` still requires a constant
fact. Module constants always require a named target.

A local declaration publishes its name only after its declared type,
initializer, and any constant it needs are complete. Its own name is not
visible in those inputs, so an initializer may select an outer binding with the
same spelling. The completed binding is visible to following statements in its
scope.

A call repeats every declared parameter access exactly: `read(value)`,
`write(&value)`, or `take(&&value)`. `Read` grants non-owning read-only access,
`Write` grants non-owning mutable access, and `Take` transfers ownership. Write
access permits but does not require mutation. It is non-owning and nonexclusive:
the same mutable owner may be passed to multiple `Write` parameters in one call.
Arguments are evaluated left to right, and mutations take effect in the
function body's execution order. Read prevents writes to the parameter's current
storage. Contained pointers and Write captures retain their separate target access.

## Read values and aliases

Read parameters and Read range bindings preserve Carven array, String, closure,
and native storage through const references, including storage nested in Carven
aggregates. Pure Carven values use const value snapshots. Native types borrow
the source holder even when their C++ copy construction and destruction are
trivial.
`import(cpp)` and `export(cpp)` use the same Read policy.

A by-value Read argument saves its value when that argument is evaluated. A
by-reference Read argument retains the selected storage, so writes through
another alias can affect subsequent reads. An explicit owning copy establishes
a separate value before the call; non-owning contents in that copy retain their
referents. Both representations obey the same access markers and Take-conflict
checks.

## Ownership transfer and mutation

Runtime `let`, `var`, ordinary pattern bindings, and `Take` parameters are
owners. A `Take` parameter is an immutable owner and may itself be taken.
`Read` and `Write` parameters, range bindings, closure state, and `const` are
not Take sources. The initial Take operand must be a complete owner or a
temporary; taking a member or element of a still-available owner is invalid.

Field selection from an owning value transfers the selected field using the
ordinary Carven value-delivery policy. `(&&owner).field` first consumes the
complete owner; the complete source is evaluated once and retained through the
full expression, with remaining fields cleaned up normally. Nested owning field
selection follows the same rule. `owner.field` remains an ordinary Read and
never consumes `owner`. Returning a view into a destroyed temporary is invalid.
This does not provide simultaneous decomposition into multiple field owners.

`&&expression` is the ownership-transfer expression and has its operand's value
type. Taking a complete owner makes that binding unavailable, including for a
copyable type. This is a state transition checked during analysis, not a runtime wrapper or a
promise of a particular C++ move operation.

Returning a named owner copies it; a return ends the owner but does not imply
Take. `CV-LINT-RETURN-COPY` reports a returned owner whose type can hold Carven
`String` storage by value, excluding zero-length array elements, and has no
native value component, when Take is admitted at that return in every analyzed
call context. Writing `return &&owner;` states the transfer and removes the warning.

Every use of an unavailable binding is invalid. A complete plain assignment to
a `var` is the only operation that may restore it, and restoration happens only
after the right-hand side completes normally. Partial assignment, compound
assignment, and update operators require the prior value. At a control-flow
join, a binding is available only when it is available on every normally
continuing path. Condition effects are checked before proven Boolean results
select the possible normal paths. Loops include possible zero-iteration paths
and all normally continuing backedges.

Member and element mutation inherit eligibility from their receiver. Plain
assignment requires a compatible value and uses the target C++ assignment
operation; it does not end the destination object's lifetime and reconstruct it.
A complete writable owner may be consumed by its RHS and restored by a normally
completed assignment, as in `x = relay(&&x)`. If the RHS fails, the owner remains
unavailable. Direct self-transfer assignment `x = &&x` is invalid, including
parenthesized forms. Member and compound assignment still require the old value.
Compound `+`, `-`, `*`, and `/`
assignment require numeric operands; `%`, bitwise, and shift assignment require
integer operands. Increment and decrement require an integer target. Assignment
evaluates the target before the new value, once each.

Take cannot conflict with a place access retained by an unfinished call,
including its callee and outer call arguments. A completed independent result
retains no read access to its inputs: `f(x + 1, &&x)` and
`f(identity(x), &&x)` are valid when their results are independent values.
Direct `f(x, &&x)` is invalid regardless of the target Read parameter policy.
Value-capture snapshots are
independent after creation, but capture creation requires the source to be
available. Lambda captures and range bindings do not support Take access.

A Write capture remains active with every live value that directly or
transitively contains its closure. Moving the closure into an aggregate,
projecting or reading it back out, and carrying it through a control-flow
result preserve that association. The capture ends with its actual holders,
not with the expression that created it; its source cannot be taken while any
such holder remains live.

## Value ownership and lifetime

Initializing an owner from an existing value without `&&` copies that value;
the source remains available. Initializing from `&&owner` transfers ownership
and changes source availability. An independently produced temporary can be
delivered to its destination without creating another source binding. These
are value and availability rules, not a fixed count of native constructor calls.

A copy owns its immediate value, including structure fields, array elements,
and enum payloads. Non-owning contents retain their backing: copying `str`, a
callable view, or a closure containing Write captures does not clone or prolong
the referenced storage. The recursive [callable-view restrictions](functions.md#signatures-and-views)
and [closure holder rules](functions.md#closure-identity-copies-and-aliases)
apply to these copies as well.

Local owners live in their enclosing lexical scope. Exiting that scope by
normal completion or a control transfer ends its local lifetimes. Temporary
storage belongs to the full expression that evaluates it unless an explicit
construct retains it, such as an owned match subject or a temporary range
source. Delivering a result into a longer-lived owner does not prolong the
lifetimes of that result's borrowed backing. Carven has no source destructor
or general lifetime-extension syntax.
