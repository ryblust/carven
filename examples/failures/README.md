# Failure examples

A quote combines catalog validation and delivery pricing. A configuration
loader distinguishes missing input from corrupt input. A policy callback adds
its own rejection conditions. Each task needs a successful result, a precise
account of what can fail, and a place to decide what recovery means.

Carven expresses these decisions with ordinary result types and closed failure
contracts. Read the series in table order, beginning with one declared failure in
Booking, then composing providers, adding recovery, and accepting callbacks.
Within each program, follow failure declarations and producer functions before
the handlers and entry point that use them.

## Run the series

From the repository root:

```sh
./xmakew build
./xmakew build examples
./xmakew run carven-example-booking
./xmakew run carven-example-order-quote
./xmakew run carven-example-configuration
./xmakew run carven-example-policies
./xmakew test -g examples
```

| Read | Task | What the program makes visible |
| --- | --- | --- |
| [Booking](basic/) | Reserve a small number of seats | One payload, explicit propagation, one handler |
| [Order quote](composition/) | Combine stock and delivery rules | Cross-module failure sets, private inference, composite `?`, guarded recovery, `rethrow`, enum patterns |
| [Configuration](recovery/) | Select and validate a port | Recovery that can fail, translating an interface, nested payload patterns, value-form `try` |
| [Policies](callbacks/) | Apply caller-selected admission rules | Failure contracts on callable views, inferred closure effects, widening from a smaller set |
| [Native parser](../interop/import/) | Adapt `std::stoi` | C++ exceptions handled in C++, then explicit Carven failure construction |

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
