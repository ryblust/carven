# Incremental workloads

The baseline is `library.cv -> facade.cv -> app.cv`, with an independent
`unrelated.cv`. `workloads.lua` selects baseline states, edited source fixtures,
and module artifact counts. `.cv.fixture` files supply replacement bytes.

The driver in `run.lua` writes each scenario to its own
build workspace and runs the Carven package's existing Xmake rule. Baseline
restoration, source edits and filesystem timestamp waits precede the measured
build. Fixtures and the selected compiler's Crafts are captured through the shared
input snapshot; the generated Xmake project builds against that captured Crafts
tree. The native project uses debug mode and C++20.

Each sample observes generated `.cpp` and `.hpp` hashes and timestamps before
and after the build. JSON records added, content-changed, removed, and
unchanged-content files whose timestamps changed, separately from native object
timestamp changes. These counts describe the build and do not enforce a fixed
number of compiler invocations or native objects.

With `--timings`, Carven invocation counts come from the compiler's existing
timing reports. The initial fresh build must produce a report before later
samples can interpret no report as zero invocations. Without `--timings`, the
count is unobserved and displays `-`.

After each successful measured build, the driver runs the executable reported by
Xmake and validates its output: `42` normally, or `43` after the private function
edit. Snapshot hashing and executable validation are outside the measured build
time. An incorrect result fails the sample instead of reporting a fast stale
build as successful.

```sh
./xmakew bench incremental --list
./xmakew bench incremental --case=private_function_edit
```
