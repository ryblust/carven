# Failure examples

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

## Follow the data

In the quote example, `catalog.cv` and `delivery.cv` produce distinct failure
payloads. `orders.cv` handles one delivery case by choosing another carrier;
`main.cv` reports the remaining cases. The calculation does not reserve stock
or charge money.

In configuration recovery, `parser.cv` accepts decimal ASCII ports in
1..65535. `settings.cv` decides when to use a fallback and when to report a new
failure. No files or environment variables are read.

In the callback example, `positive` can produce `InvalidAmount`; the capturing
policy adds `LimitExceeded`. The shared callable view admits both, and the
program keeps the borrowed closure alive while invoking it.

The native parser handles C++ exceptions in its adapter and returns a value
that Carven interprets. That target enables C++ exceptions for the adapter.
