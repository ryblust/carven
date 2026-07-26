# Typed Failure Effects

- **Status:** Accepted
- **Implementation:** Complete
- **Scope:** Structured semantic failure facts, private C++ transport, and
  direct Target realization
- **Permanent contracts:** [Failure-contract semantics](../docs/semantics.md#failure-contracts),
  [compiler architecture](../docs/compiler.md), and
  [backend](../docs/backend.md)

## Summary

Carven exposes recoverable failure as callable failure contracts and models it
as a closed typed control effect rather than a source-level result value. The
implemented transport has one path:

```text
Semantic FailureSetID
  -> TargetFailureSetProfile
  -> raw carven::runtime::Outcome<Result, Failures...>
  -> ordinary C++ move construction
```

SemanticProgram owns exact contracts, evaluation order, handler order,
coverage, guard fallback, rethrow identity, and ownership on every path. Target
planning owns only the stable C++ representation order. Lowering consumes both
facts without introducing another failure model.

## Semantic authority

Every callable failure contract is one exact closed set of nominal failure
types. One interned `FailureSetID` identifies each distinct set; normalization
by `HIRTypeID` is private storage policy and has no language-visible order.
Callable signatures and expression, block, catch, and try facts retain IDs,
including an expression evaluation set equal to the union of its pending and
outward sets.

Control solving may use temporary vectors while computing least fixed points
and catch coverage. Publication interns those vectors and exposes no second
persistent failure-vector API. Final validation checks each interned set once,
checks every published ID, and verifies the required unions and subsets.

`throw`, postfix `?`, `try`/`catch`, guards, handler-produced failure, and
`rethrow` remain structured HIR. Catch arms retain source order; the failure
sets they inspect are unordered. Target lowering does not infer a failure set,
rerun coverage, select a call, or clone a handler by failure identity.

## Target representation

Each `FailureSetID` maps to one `TargetFailureSetProfile`. Its `ordered_members`
sort by canonical module path, qualified source name, and nominal kind. Equal
keys for distinct nominal types are an invariant violation, never resolved by
an unstable semantic ID.

Functions, closures, and callable views query the same profile. Failing
signatures spell the raw `carven::runtime::Outcome<Result, Failures...>` type;
no callable-specific aliases or declaration-scope graph participates in failure
transport. Entity names carry an owner module and complete relative target path,
and cross-module resolution adds the module namespace in one place.

Lowering represents a carrier with its success result, `FailureSetID`, and
unit-local target type. Carrier construction, failure iteration, identity, and
widening all use the profile-backed entry points. Identity forwards the existing
carrier. Widening constructs the destination Outcome from an rvalue source.
Narrowing, incomparable sets, and result changes are invariant violations.

## Private runtime transport

`Outcome<Result, Failures...>` stores `SuccessState<Result>` and each failure
type directly in one variant, preserving the distinction when Result and a
failure have the same C++ type. Failure alternatives are nonempty, unique, and
nothrow move constructible. Same-specialization move assignment reconstructs
the active alternative and does not require alternative move assignment.

The ordinary move constructor handles identity. An implicit `noexcept`
cross-specialization constructor accepts only rvalues with the same Result and
a source failure set that is a strict subset of the destination. Its constraints
reject lvalues, narrowing, incomparable sets, and changed results. The public
operations are success/failure construction, state queries, and move extraction;
there is no separate propagation member protocol.

`FunctionRef` accepts a borrowed object or compatible function pointer when
invocation and conversion to its Result are `noexcept`. This lets the same
Outcome constructor adapt smaller failure results to wider callable views.
Function pointers use erased pointer storage plus a typed thunk that restores
the original pointer type. Noncapturing temporary lambdas use that pointer path;
capturing objects retain the existing borrowed lifetime.

## Target realization

`TargetCallableLowerer` lowers operands left-to-right and checks Outcome at the
call site. Value-form `try` uses an immediately invoked lambda when a C++ value
region is required. Statement-form `try` uses one scoped protected body, a local
carrier, and limited local label/goto for failure transfer when C++ has no
smaller structured representation. This route is not a rendered semantic CFG
and is preferred to a runtime control-state machine. Ordered matching, guard
rejection, fallback, rethrow, and handler-produced failure follow facts stored
on semantic nodes.

Return, local carrier assignment, and callable-view adaptation use the same
runtime identity-or-widening rule. No wrapper lambda, alias layer, or special
member conversion call is generated.

## Deferred work

### Interprocedural private-call specialization

Removing a private Outcome boundary would add cloning, recursion, code-size,
and cross-unit policy. It remains inactive until measurement demonstrates a
material cost that C++ optimization cannot remove.

### Async suspension

Failure state crossing suspension belongs to the async proposal's completion
and frame contract. The synchronous Outcome is neither automatically reused nor
published as that contract.

### Richer failure values

Move-only, managed, or dynamically typed failures require a dedicated source
ownership decision before they can extend the current nominal failure model.

## Validation

Semantic tests cover order-independent interning, valid IDs for all persisted
facts, and evaluation-set unions. Runtime tests cover value and void success,
failure alternatives, state replacement without alternative move assignment,
strict widening constraints, and FunctionRef adaptation for function pointers,
borrowed objects, and noncapturing lambdas. Language tests reverse semantic and
target member orders and execute concrete functions, closures, exact and
widened callable views, success, typed failure, identity, widening, void
success, and try propagation. Interface tests establish that a private
normalized name collision cannot perturb a published header. Generated program
behavior is compiled and executed as C++20 and C++23 without private
carrier-shape snapshots.
