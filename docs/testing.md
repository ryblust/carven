# Testing

This document defines how repository tests are divided and run. It records test
ownership and coverage without redefining the public behavior under test.

## Validation workflow

During implementation, build before running the relevant suite identified under
[Suite responsibilities](#suite-responsibilities):

```shell
./xmakew build
./xmakew test -g <suite>
```

The build selects the [Carven Xmake rule](https://github.com/ryblust/carven-xmake-repo)
from its default repository. For local rule development, select a
`carven-xmake-repo` checkout during the build:

```shell
CARVEN_XMAKE_REPO_DIR=/path/to/carven-xmake-repo ./xmakew build
./xmakew test -g <suite>
```

If an unexpected compiler, module, BMI, dependency-order, or apparently
impossible type error occurs, clean and rebuild with the repository wrapper:

```shell
./xmakew clean
./xmakew build
```

After implementation and relevant tests are complete, run the final validation
workflow:

```shell
./xmakew test
./xmakew check clang.tidy
```

The unfiltered clang-tidy invocation checks every C++ translation unit
registered with Xmake, including generated C++ artifacts, and analyzes
`crafts/` headers included by those translation units.

## Suite responsibilities

The repository has four test suites. Placement follows the execution boundary
needed to observe the behavior, rather than the spelling of the feature under
test.

| Suite | Execution boundary | Primary responsibility |
| --- | --- | --- |
| `internal` | C++ tests linked with `carven-modules` | Compiler representations, algorithms, artifact planning, sink invariants, in-process compilation, and structured diagnostics |
| `language` | Valid `.cv` sources compiled to C++ and executed | Observable Carven language behavior and the generated testing adapter |
| `interop` | `.cv` sources compiled with provider or consumer C++; C++20 behavioral targets execute and C++23 compatibility targets compile and link | C++ header imports, source fragments, `import(cpp)` bridges, `export(cpp)` façades, and generated public-API consumption |
| `cli` | The `carven` process, Xmake, and temporary filesystems | Arguments, status, standard streams, output selection, direct filesystem behavior, and Xmake generation policy |

Use the following placement rules:

- A test that imports compiler modules, calls the compiler in process, or
  inspects a structured compiler result belongs to `internal`.
- A test whose observation requires command invocation, an exit status,
  standard streams, or filesystem effects belongs to `cli`.
- A generated-program test whose subject is a C++ header import, source
  fragment, `import(cpp)` provider, `export(cpp)` consumer, or boundary runtime
  contract belongs to `interop`.
- Other generated-program tests of valid Carven behavior belong to `language`.

Stable diagnostic identities are part of the observable language contract,
but their structured tests belong to `internal` because they call the compiler
in process. CLI tests cover diagnostic transport through process status and
standard error without duplicating the complete diagnostic corpus.

Some behavior crosses more than one boundary. Keep checks at multiple
boundaries only when each check protects a distinct contract. Do not reproduce
an internal transition table through CLI cases or duplicate ordinary language
semantics in the interop suite.

## Consumer coverage

The generated-program compatibility matrix covers the C++20 and C++23 consumer
modes defined by [compatibility.md](compatibility.md). The `language` suite
executes its complete corpus in the C++20 baseline and compiles the same corpus
without executing it in C++23. This separates observable Carven behavior from
the newer-mode compatibility signal. The corpus covers types and values,
functions and calls, bindings and access, control flow, patterns and matches,
failure contracts, lambdas and callable views, modules and imports, and testing
integration.

The `interop/scalar_boundary` and `interop/provider_forms` targets execute in
C++20. The scalar consumer includes the generated
`carven/api/<module>.hpp`, checks its self-containment and public boundary
signatures, links the generated façades, and invokes them. Provider forms
cover both C++ header import spellings, source fragments, linked source,
callable adoption, import bridges, bare cross-module calls, and explicit
wrappers. The combined `carven-test-interop-cxx23-compatibility` target compiles
and links the scalar and provider-form sources in C++23 without executing them.
The two Unicode ingress contracts execute once in C++20 because import-result
and export-argument validation are distinct runtime entry points.

Generated tests use Carven's allocation-free intrusive registry. Registration
is deterministic by module and case name; the default runner executes the full
registry, while entry-point and reporting fixtures exercise their distinct
integration contracts. Generated translation units do not include doctest.
The entry-point fixture keeps external test emission, defines the Carven
`main(args)` shorthand, and invokes or observes the registry only through
scalar `import(cpp)` helpers; command-line argument contents remain opaque.

Language fixtures that need C++ observation helpers declare them through a
same-stem C++ provider header and call them through `import(cpp)`. C++ source
fragments remain in the interop corpus and focused frontend/backend tests where
their opaque bytes, delimiter behavior, placement, or attribution are the
subject.

The `internal` suite follows the compiler build configuration and uses the
vendored doctest header. It does not serve as generated-C++ consumer coverage.
Consumer-mode coverage belongs only to the generated-program suites.

## Architecture invariant coverage

The `internal` suite owns focused evidence for the sealed compiler
representations:

- semantic program fixtures cover canonical values, ID bounds, structural
  ownership, cycles, type/form relations, flow/effect alignment, explicit
  callable implementation origins, C++ header spelling validity, source/export
  origin ownership, `import(cpp)` implementation and `export(cpp)` origin
  exclusion, Symbol-rooted place projections, match coverage, and nominal
  containment;
- target-program fixtures cover total type/signature/failure domains, stable
  profile order, failure-carrier conversion laws, typed artifact dependencies,
  SCC schedules, and dependency-first order;
- target-unit fixtures cover reference bounds, deep occurrence cloning, unique
  item/statement/expression ownership, cycles, attribution, type-owned array
  extents, typed-for headers, all-arena reachability, artifact metadata, and
  lowered jump roles;
- prepared-statement fixtures cover move-only classify-once/publish-once
  behavior for C-style `for` initializers and steps;
- diagnostic, target-quality, interface, and language fixtures jointly cover
  covered-arm warning identity, target-only dead-arm/dependency omission,
  subject evaluation exactly once, and guard/materialization behavior.

These checks protect private representation invariants without turning exact
node counts, helper names, or complete generated C++ bytes into compatibility
contracts.

## Target organization

The root `xmake.lua` includes `tests/internal`, `tests/language`,
`tests/interop`, and `tests/cli`. Each suite owns its target definitions in
`tests/<suite>/xmake.lua`.

Test directories do not mix files and subdirectories, with these exceptions:

- `tests/<suite>/xmake.lua`: suite build metadata;
- `tests/cli/harness.lua`: CLI process infrastructure kept beside the suite
  entry point;
- `tests/cli/module_layout/project`: nested project layout fixture;
- `tests/language/modules_and_imports/namespace_collision`: file-module and
  directory-namespace collision fixture;
- `tests/language/modules_and_imports/same_leaf`: distinct sibling namespaces
  with equal module leaves fixture.

Internal tests share `tests/internal/harness/main.cpp`, and owner-specific
helpers live beside the tests that use them. The CLI process harness lives
beside the suite entry point as `tests/cli/harness.lua`.
Language and C++ interoperation targets use the local `carven` executable and
runtime headers.

Keep target definitions direct and mechanical. Add a matrix dimension only
when it represents a distinct supported boundary. Each consumer-mode target
owns its generated artifacts; do not add cross-target generated-artifact reuse
or a runner dimension that does not change the behavior under test.

## Test scope

Tests should protect observable behavior, stable public diagnostics,
representation invariants, or small serialization primitives. Complete
generated C++ text, private temporary names, incidental unit internals, and
private implementation shapes are not compatibility snapshots.
Generated-program correctness is normally established by compiling and
executing the output.

Object sizes, timings, and resource usage are not behavioral assertions.
Performance coverage requires a separately designed benchmark with an explicit
workload and measurement contract; do not approximate it with a private
representation-size assertion.

The manual build-workflow pulse lives in `benchmarks/build_pulse.py`. It reports
the median for a fresh 128-module batch, the median-time ratio between 128 and
16 modules, and the C++ object count invalidated by one private implementation
edit. Debug measures the normal
development loop; optional Release runs measure shipped compiler performance.
Comparisons must use the same mode. The pulse does not run in CI, store
baselines, or define an absolute performance gate.

The pulse stays centered on stable, system-level development workflows: fresh
batch throughput, module-count scaling, and private-edit locality across the
compiler/build-system boundary. Its workload and measurement contract are
maintained in `benchmarks/README.md`.

Do not add a test whose signal is already supplied by another case. Matrix
dimensions require an independently supported boundary. Refactors of context
types, implementation slices, node counts, or private names do not receive
dedicated tests.

A test assertion provides evidence for the current contract without defining a
public compatibility promise. Source paths and locations are asserted when they
are part of a documented diagnostic or test-reporting contract.

Internal diagnostic checks compare stable identity, severity, and required
source locations. They avoid matching prose or incidental ordering unless the
test specifically owns presentation behavior. An end-to-end output fixture does
not by itself make every captured word or ordering choice stable.

CLI cases use representative end-to-end paths for public modes and failure
classes. Detailed artifact planning and write ordering coverage stays with the
internal owner. A CLI case may inspect a stable source-derived sentinel
when an option has no other observable effect, but it should not compare the
private structure of generated C++. Every CLI process has a 30-second hard
timeout; a timed-out child is killed and reaped, and a failed case retains its
temporary directory.

`carven-test-xmake-default-domain-isolation` links and runs a binary composed
from two default-domain object targets compiled from the same canonical source.
Successful execution validates isolation between their generated symbols.
