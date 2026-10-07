# C++ representation

This reference defines how published types, constants, parameter access, and
callable contracts select native representations. Realization owns operand
sequencing and result storage; artifact construction places their declarations
and dependencies.

## Representation

The backend expresses resolved operations using C++ types, constructors,
references, scopes, calls, and control flow. Carven determines source evaluation
order, access, lifetimes, and exit destinations. C++ performs overload resolution,
object construction, copy elision, scope cleanup, layout, and native optimization.
Lowering preserves the types, value categories, and initialization syntax supplied
to those C++ operations.

Representation selection expresses published facts through literals, concrete
types, template arguments, and specialized runtime entries.
Target template-id name expressions carry ordered type or boolean/integer
literal arguments independently of invocation. Calls consume a callee expression
and runtime arguments; function-template calls and variable-template references
use the same name representation.
These describe native C++ syntax, with type dependencies visited normally.

Structure declarations lower to C++ aggregates containing their declared fields.
Payload enum factories, storage constructors, and projections are ordinary C++
functions. Carven evaluates source constants during semantic analysis; their
uses reconstruct the normalized values through the same target operations.

## Constants and default values

Calls executed in the static stage and direct constant expressions arrive as completed
semantic values. Finite floating values use round-tripping literals; nonfinite
values lower to typed `std::bit_cast` calls from their stored integer bits, with
ordinary target-symbol and type dependencies. Emission only serializes those
constructed expressions. Text constants become byte literals with explicit lengths,
including internal NUL. Fixed arrays and structs become typed initializers of
their completed children. These value initializers establish no source address
identity and do not extend temporary backing lifetimes.

`SliceConstant` instead requests persistent backing for its completed elements.
`backend.lowering.constant` reconstructs completed values. Each `ModuleLowering`
owns one `ConstantStorage` within its artifact. Storage records which canonical
constant identities have been materialized; declarations and references use
names from `TargetNamePlan`. It emits an `inline constexpr` declaration initialized
by a typed `std::array` and realizes the slice as `runtime::as_slice` of that array.
Generated names distinguish linkage domains and source modules; artifacts can
materialize the same planned backing independently. Storage remains in its source
module's C++ namespace so user types resolve in the same scope. Empty values use
the same representation. Elements are reconstructed from their canonical constants,
preserving the slice's element type. Private type definitions precede slice backing,
followed by function bodies; dependencies request complete element definitions for
that storage. Target variable declarations participate in ordinary traversal,
verification, dependency collection, and emission. The runtime slice supplies the
read-only access and bounds operations.

`const` blocks have already executed during semantic analysis. A module-scope
block's body remains available for semantic validation but has no callable or
module item to schedule; a block in a body is absent from every realized region.
Neither produces a C++ body or a runtime call.

`SemDefault` realizes as typed C++ value initialization (`T {}`), with pointers
using a typed null cast. Semantic analysis has already established Carven default
availability. Empty structure construction defaults fields in declaration order;
nonempty construction supplies every field explicitly. Aggregate operand
scheduling preserves source operand order, snapshots and cleanup. Empty structures
and default arrays stay compact in generated syntax. Scalar, pointer, slice and
range defaults need no execution when discarded.
Native default constructors remain observable even when the result is discarded,
and C++ checks their availability and access. Defaults in static roots arrive
at lowering as completed values through the usual freezing path.

Constant functions also retain ordinary runtime bodies. Runtime calls use normal
lowering, operand evaluation, ownership, cleanup, and wrapping integer arithmetic.
The qualifier alone supplies no call-result fact or permission to discard a call.

## Parameters and access

Read parameters, Read argument storage, and Read range bindings preserve
Carven array, String, closure, and native value storage through const references,
including storage in Carven aggregate fields. Lowering uses the resolved
type-contents query shared with ownership analysis. Pure Carven value Read
parameters use const values. Native value containment propagates through array
elements, struct fields, and enum payloads; pointer and slice targets do not
contribute. Native template arguments alone leave the instantiated type's storage
contents unknown. Interface planning includes complete definitions where source
representations require them. A pointer representation is complete without completing its
target; pointer dependencies request target declarations. Forming that target's
type expression can still require complete definitions, such as Read parameter
types in a callable signature. Declaration ordering includes these requirements;
cycles in type formation remain C++ errors.

Write parameters use `T&`; Take parameters own a `T`. `runtime::transfer` exposes
a mutable owner's value for construction: trivial values are read, other values
are supplied as rvalues. It returns a reference and adds no intermediate owner.
C++ selects the constructor. Semantic analysis enforces source availability.

## Pointer values

`ptr<T>` lowers to a pointer to `std::add_const_t<T>` and `ptr<&T>` to a
pointer to T, composed per layer for nested ptr values. Read ptr operands
snapshot the address before later operands can replace its slot. Dereference
selects that saved address before evaluating the rest of a store or call. Typed
null constants retain the complete pointer type in native overload resolution.
`addressof` selects the source place once and lowers its address through
`std::addressof`, preserving the selected Read or Write target access. It does
not construct a temporary owner or change that owner's lifetime.
Native adoption uses typed initialization and C++ conversion checks. External
owners supply resource cleanup.

Native Read pointer operands preserve the pointer type and const-lvalue category
used by C++ type queries. Adaptation isolates the selected address from subsequent
slot writes, using storage when sequencing requires it.

Source-owned writable-pointer declarations retain explicit pointee access. Lowering
records this declaration contract; emission attaches its const-correctness
annotation. Backend operand storage and projections receive ordinary analysis.

## Callables and native boundaries

Callable thunks borrow the already evaluated parameters until invocation. Reference
parameters retain their declared category; value parameters are delivered through
the ordinary transfer policy. Callable admission checks that exact delivery
expression and any required result widening. Same-carrier propagation and Outcome
widening reconstruct the active payload through the same transfer operation,
including success payloads inside their wrapper. Widening requires construction
only for the source alternatives; newly admitted failure types need no transfer.

The parameter policy is shared by declarations, definitions, and callable signatures.
C++ imports and export façades use this same parameter policy, type realization,
and failure ABI. Export Take parameters are forwarded through `transfer`; import
Take parameters use the native rvalue category. Read and Write retain their
reference/value categories. Import bridges and native expression operands share
the same typed rvalue construction. Export forwarding retains the ordinary
transfer policy even for native types with trivial copying and observable or
deleted move constructors. Import bridges call the globally qualified
provider directly, allowing C++ overload resolution and template deduction.
Type lowering receives an explicit naming scope: public signatures use globally
qualified nominal names, while module bodies retain local names. Both use the
same dependency discovery and callable work queue. Export façades project internal
test-stop transport to the declared result carrier, retaining declared failures
and terminating on an escaping test stop.

Concrete closures use named structures with const call operators defined in the
source artifact. Closures exposed by function result types publish their layouts
in the generated interface. Noncapturing closures adapt to callable views without
borrowing an object; the source closure expression is evaluated once.
Value captures are fields and are read-only through the call operator. Write
captures are `std::reference_wrapper<T>` fields whose referents are accessed
through `.get()`. Capture fields permit the generated default
C++ assignment operation to copy values and rebind reference targets.
Callable borrows use non-owning `FunctionRef` target descriptions.

Runtime type-selected operations use `Stateless<T>` and `stateless_value<T>`
from `runtime/stateless.hpp`. The contract admits unqualified empty object types
with trivial default construction and destruction. Each type has a shared const
instance; consumers check invocation arguments and results. Operations may
observe external state.

`FunctionRef::from_stateless(value)` invokes that instance without retaining a
source-object borrow. The source expression executes once. Stateless array
adaptation uses the same entry and result adaptation. Ordinary callable borrows
preserve the supplied object's identity and constness.

Generated function bodies, closure call operators, evaluation lambdas, import
bridges, and export façades use unconditional `noexcept` specifications.

`FunctionRef` invocation and `Outcome` payload construction and movement also
establish `noexcept` boundaries. Their constraints require the constructions
and invocations they perform, without requiring those operations to declare
`noexcept`. Outcome remains move-constructible when its alternatives permit
it, with no copy, assignment, or default construction. Function-pointer thunks
restore the original pointer type before invocation.

Ordinary class methods arrive as checked ordinary calls with explicit receiver
arguments. Factories and field access reuse nominal product realization. Source
privacy does not require a separate C++ class hierarchy, a runtime access check,
or re-resolution of methods in the backend. C++ still checks delegated native
construction and invocation expressions; external implementations remain governed
by their interoperation contracts.

## Failure and test-stop ABI

`FailureABI` gives each failure set a deterministic member order. Failing
results use `Outcome`; other results are direct. Widening accepts identity or a
strict failure-set superset. Calls, propagation, handlers, and callable
adaptation use this one contract.

A concrete callable whose published effect admits
test stop returns `Outcome<Result, TestStopped, Failures...>`. Callable views
use the same transport, with result adaptation lifting ordinary returns.
Unaffected concrete callables retain their ordinary return representation.

Native export façades remove `TestStopped` from the result carrier, preserving
success and every declared failure. An escaping test stop terminates. Arbitrary
C++ callbacks do not participate in Carven propagation.

Control realization applies these carriers at calls, handlers, returns, and test exits.

## External result types

External result queries use `TargetDecltypeType` to preserve the queried
expression's type and value category as `decltype((...))`. Semantic object types
explicitly wrap that query in `std::remove_cvref_t`; normalization belongs to
representation selection. Restricted query shapes have structural equality for
type interning; general target expressions remain move-only and have no equality
protocol.

C string constants have an intrinsic external `const char*` type. Lowering emits
their decoded bytes as a narrow C++ string literal converted to a pointer, so
calls and deduction receive the declared pointer type with program-lifetime storage.
Ordinary string literals retain `std::string_view` realization.

Artifact-local query aliases name shared queries without changing their type or
value category.

### Native construction results

A native construction records its target separately from its result query.
Each query argument records its type, access, and optional known scalar value.
Read scalar expressions with known results and no selected source storage deliver
that constant in both the query and executed construction. Their original
expressions retain all evaluation and cleanup obligations. Other arguments retain
their access-qualified type queries and ordinary value delivery.

Native construction preparation maps query arguments to the retained operands
used for execution.

The shared argument lowering preserves constant-expression narrowing and native
overload selection. Later indexing, members, and storage use the construction's
result type. Implicit value initialization consumes the result; direct prvalue
construction retains C++ copy elision, including for immovable types.

An explicitly specialized native construction uses its named target as the
result type. Deduction remains a C++ query when the target has no explicit template
arguments. The semantic construction query retains argument-delivery facts in
both cases.
