# Failure contracts: compose work, keep failures precise

A quote combines catalog validation and delivery pricing. A configuration
loader distinguishes missing input from corrupt input. A policy callback adds
its own rejection conditions. Each task needs a successful result, a precise
account of what can fail, and a place to decide what recovery means.

Carven expresses those responsibilities with ordinary result types and closed
failure contracts. Start with [composition/orders.cv](composition/orders.cv):
`quote` returns i32 and declares `QuantityError + OutOfStock + DeliveryError`.
Its private helper composes two fallible calculations, while the public module
function selects an alternate carrier for one case and preserves the other
failures. The caller receives the original typed payloads.

## Run the series

From the repository root, using the [example build setup](../README.md):

```sh
./xmakew build
./xmakew build examples
./xmakew run example-order-quote
./xmakew run example-configuration
./xmakew run example-policies
./xmakew test -g examples
```

| Read | Task | What the program makes visible |
| --- | --- | --- |
| [Booking](basic/) | Reserve a small number of seats | One payload, explicit propagation, one handler |
| [Order quote](composition/) | Combine stock and delivery rules | Cross-module failure sets, private inference, composite `?`, guarded recovery, `rethrow`, enum patterns |
| [Configuration](recovery/) | Select and validate a port | Recovery that can fail, translating an interface, nested payload patterns, value-form `try` |
| [Policies](callbacks/) | Apply caller-selected admission rules | Failure contracts on callable views, inferred closure effects, widening from a smaller set |
| [Native parser](../interop/importing/) | Adapt `std::stoi` | C++ exceptions handled in C++, then explicit Carven failure construction |

Each directory includes its reading path, full output, and small experiments.
The source files are the executable examples; the explanations link to them
rather than maintaining a second copy of each program.

## What to notice

**The success type stays about success.** `quote` returns an amount. The
`throw` clause separately lists admitted failure types. Callers use `?` to
propagate pending failure at a visible expression boundary and `try`/`catch`
to handle it. Carven's failure effect is not a source-level `Result` container
that these examples store or inspect later.

**Composition preserves distinct failures.** Catalog and delivery code declare
their own types. The quote combines those types without inventing a common
base class or an application-wide wrapper. `OutOfStock` carries both requested
and available quantities. `DeliveryError` has payload-bearing enum cases.
The caller handles those cases directly.

**Implementation inference and interface declarations have different jobs.**
Private helpers and lambdas can infer their failure sets. Shared module
functions with failures state a contract checked against their bodies. The
clause is an upper bound: callers reason from the declared set, even when one
particular implementation or input uses fewer alternatives. Member order does
not give failures a priority.

**Recovery is part of composition.** A handler may produce a value, throw a
replacement failure, call another fallible function, or `rethrow` the selected
failure. A failure produced by a handler travels outward; it is not caught
again by that same handler list. A guard chooses a recovery policy, while the
remaining cases still need coverage.

**Callbacks carry the same information.** A callback that only fails with
`InvalidAmount` fits a view admitting `InvalidAmount + LimitExceeded`. The
capturing policy adds the second failure through its body. Its view remains a
borrow, so the example keeps the closure owner alive.

## Boundaries of the demonstration

These programs implement calculations and parsing, not network requests or a
transactional order system. The quote does not reserve stock or charge money.
Failure propagation does not roll back earlier mutations or external effects.
The configuration parser accepts decimal ASCII ports in 1..65535; it performs
no filesystem or environment access.

Failure values currently use copyable nominal structures and enums. Native
C++ exceptions are outside Carven's failure sets. The pure Carven examples
build with native exceptions disabled; the separate native-parser target
enables them for its C++ adapter.

The current backend realizes failing call results with its `Outcome` support
and uses generated control flow for handling. This is an implementation detail,
not a public carrier API or a guarantee of a particular register layout,
allocation count, or performance relative to another language. See the
[backend description](../../docs/backend.md#evaluation-and-values).

The current rules remain in
[failure contracts](../../docs/semantics.md#failure-contracts).
