# Bindings, access, and mutation

This page defines binding roles, Read/Write/Take access, value transfer, and
lifetime rules shared by language operations.

- [Binding roles and access](#binding-roles-and-access)
- [Read values and aliases](#read-values-and-aliases)
- [Ownership transfer and mutation](#ownership-transfer-and-mutation)
- [Value ownership and lifetime](#value-ownership-and-lifetime)

## Binding roles and access

Carven separates value type, binding role, and access mode. Binding and argument
markers `&` and `&&` describe access; they do not construct reference types.
Inside `ptr<&T>`, `&` is the pointer type's explicit target-access parameter.

| Form | Binding or access |
| --- | --- |
| `let` | Immutable runtime owner |
| `var` | Mutable runtime owner |
| Local `const` | Static binding |
| Unmarked parameter | `Read` access |
| Parameter marked `&` | `Write` access |
| Parameter marked `&&` | `Take` access |
| Unmarked range binding | `Read` element access |
| Range binding marked `&` | `Write` element access when the source permits mutation |
| Unmarked pattern binding | `Read` access to the selected value |
| Pattern binding marked `&` | `Write` access when the subject permits mutation |

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

Read parameters preserve Carven array, Vector, String, and closure storage
through const references, including storage nested in aggregates. Pure Carven
values use const value snapshots. Native-containing values use a const value
when C++ copy construction and destruction are both trivial, and a const
reference otherwise. Object size does not affect this choice.

When a callee retains a parameter holder through its result or external state,
that parameter uses a const reference throughout the call chain, including
argument temporaries and native wrappers. Async native-containing Read parameters
retain their holders from cold construction until operation closure.
`import(cpp)` and `export(cpp)` use the same policy. Callable views use the
ordinary value-or-reference policy; a target requiring stable native storage
cannot adapt to a view that lacks that borrowing contract.

Pattern and range Read bindings are non-owning. They snapshot pure Carven
values and borrow storage-containing values, including native holders, from the
selected subject or current element.

A by-value Read argument saves its value when that argument is evaluated. A
by-reference Read argument retains the selected storage, so writes through
another alias can affect subsequent reads. An explicit owning copy establishes
a separate value before the call; non-owning contents in that copy retain their
referents. Both representations obey the same access markers and Take-conflict
checks.

## Ownership transfer and mutation

Runtime `let`, `var`, and `Take` parameters are owners. A `Take` parameter is an
immutable owner and may itself be taken.
`Read` and `Write` parameters, pattern and range bindings, closure state, and
`const` are not Take sources. The initial Take operand must be a complete owner or a
temporary; taking a member or element of a still-available owner is invalid.

Tuple parameter and loop targets inherit these access rules. Named slots in a
Take parameter are independent immutable owners; Read and Write targets remain
projections of the enclosing parameter or current element. Positional fields
such as `.0` obey the same complete-owner restriction as named fields.

Field selection from an owning value transfers the selected field using the
ordinary Carven value-delivery policy. `(&&owner).field` first consumes the
complete owner; the complete source is evaluated once and retained through the
full expression, with remaining fields cleaned up normally. Nested owning field
selection follows the same rule. `owner.field` remains an ordinary Read and
never consumes `owner`. Returning a view into a destroyed temporary is invalid.
Tuple binding targets decompose a value into independently owned element bindings
with matching arity and nesting.

`&&expression` is the ownership-transfer expression and has its operand's value
type. Taking a complete owner makes that binding unavailable, including for a
copyable type. Analysis checks this state transition; the selected C++ operation
must preserve its ownership and observable effects.

Returning a name that denotes a complete local `let` or `var` owner or a `Take`
parameter of the current function or lambda implicitly transfers that owner.
Parentheses do not change this rule, and expression bodies follow the same rule
as `return`. An active borrow that prevents Take also prevents this return.
Other operands retain their ordinary access, including Read and Write parameters,
captures, fields, and indexed elements. Result conversions retain their ordinary
value or borrowing rules. To return a copy of an owner with type `T`, write
`return owner as T;`; the type must support copying.

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
the referenced storage. A copied view retains its escape restrictions, and every
live closure copy retains its Write-capture associations.

Local owners live in their enclosing lexical scope. Exiting that scope by
normal completion or a control transfer ends its local lifetimes. Temporary
storage belongs to the full expression that evaluates it unless an explicit
construct retains it, such as an owned match subject or a temporary range
source. Delivering a result into a longer-lived owner does not prolong the
lifetimes of that result's borrowed backing. Carven has no source destructor
or general lifetime-extension syntax.
