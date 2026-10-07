# Compile workloads

`workloads.lua` defines parameterized compiler inputs and their source construction.
Module batches emit independent source files. Structured cases exercise call
chains, shared types, matching, constant execution, ownership and expression flow.
The driver in `xmake/benchmarks/compile.lua` prepares build workspaces and measures
source-to-C++ compiler processes without native compilation.

```sh
./xmakew bench compile --list
./xmakew bench compile --case=modules_16 --timings
```
