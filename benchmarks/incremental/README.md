# Incremental workloads

The baseline is `library.cv -> facade.cv -> app.cv`, with an independent
`unrelated.cv`. `workloads.lua` selects baseline states, edited source fixtures,
and module artifact counts. `.cv.fixture` files supply replacement bytes.

The driver in `xmake/benchmarks/incremental.lua` writes each scenario to its own
build workspace and runs the Carven package's existing Xmake rule. Baseline
restoration, source edits and filesystem timestamp waits precede the measured
build. The native project uses debug mode and C++20.

```sh
./xmakew bench incremental --list
./xmakew bench incremental --case=private_function_edit
```
