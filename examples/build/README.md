# Project build scripts

The two self-contained projects use the same `std::build` API. Commands assume
`carven` is on `PATH` and a C++20 toolchain is available.

## declarative

`declarative/build.cv` describes two executable targets in one call to `project`.
Both use `src/catalog.cv`, listed once in `shared_sources`, and each supplies
its own entry.

Run in `declarative/`:

```sh
carven build
carven run inventory
carven run inventory-check
carven query
```

The application prints two stock counts; the checker prints
`Checked 2 catalog items`.
`query` emits target names, source files, and watch paths as JSON. The LSP reads
the same plan as the build, so it can discover targets while application code is
incomplete. No separate editor project configuration is needed.

## imperative

`imperative/build.cv` uses top-level statements to read `settings.toml`, generate
`build/generated/settings.cv`, and assemble a `BuildPlan`. A loop adds targets;
`include_checks` selects the checker, and `optimize` adds `-O2`. The script calls
`build(plan)`, prints the output names and paths, and runs the checker with
`std::process.run` when enabled.

This example uses explicit build actions and does not publish queryable targets.
Use the declarative example's `project(plan)` entry for editor discovery. Queries
execute ordinary scripts, so preparation statements still have effects; see
[project queries](../../docs/language/build.md#project-queries).

`settings.toml` is data read by the script. Its `minimum_stock` becomes a constant
in the generated source.

Run in `imperative/`:

```sh
carven build
./build/stock-report
```

On Windows, run `build\stock-report.exe` for the second command.
With the supplied settings, the report prints only the pencil stock, which meets
the generated minimum of 20. Set `include_checks` to `false` to omit the checker,
or change `minimum_stock` and run `carven build` again to update the threshold.
All outputs, including generated sources, stay under `build/`.
