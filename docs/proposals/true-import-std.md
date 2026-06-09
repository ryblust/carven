# True `import std`

## Goal

Make Carven's `import std;` generate real C++23 standard-library module imports
instead of lowering to a generated include preamble.

Xmake is now Carven's build-system source of truth. It already has C++ modules
support, including standard-library module support, so Carven should stop
duplicating compiler- and build-graph decisions in codegen.

## Public Contract

Carven source:

```carven
import std;

fn main() {
    std::println("Hello World");
}
```

Generated C++:

```cpp
import std;

auto main() noexcept -> int {
    std::println("Hello World");
}
```

`import std;` is treated like any other import in the language:

- it is emitted verbatim as `import std;`
- `import std using println` emits `import std;` and `using std::println;`
- `import std using *` emits `import std;` and `using namespace std;`
- Carven no longer turns `import std;` into a header fallback

If the selected compiler or standard library cannot build C++23 standard
library modules, the xmake build fails. Carven should not silently fall back to
headers in this proposal.

## Xmake Integration

The Carven xmake rule should be the integration point:

- generated project scaffolds set `set_languages("c++23")` or newer
- generated single-file script projects enable C++ modules explicitly
- the Carven rule keeps emitting `.cpp` into xmake autogen output
- xmake owns std module discovery, BMI generation, dependency scanning, rebuilds,
  and compiler-specific flags

Generated `xmake.lua` files should include:

```lua
set_policy("build.c++.modules", true)
set_policy("build.c++.modules.std", true)
```

`build.c++.modules` is important for Carven because xmake sees generated `.cpp`
files rather than user-authored `.cppm`/`.mpp` module interface files. The std
module policy is enabled by default in current xmake, but Carven should set it
explicitly so generated projects document the intended build mode.

## Implementation Direction

Remove the current header-fallback special case:

- remove `ImportItem::is_std_module`
- remove `has_std_module`
- remove `import_std` from command request types and the user-facing
  `--import-std` flag
- remove `carven.import_std` from the xmake rule and generated xmake templates
- change codegen so every import item emits `import <module>;`
- stop passing an `import_std` boolean through `transpile` and `generate`
- delete the generated std include preamble path once no generated construct
  still depends on it

Generated-code features that use standard-library names, such as arrays,
integer aliases, `match` helpers, or `fn main(args)`, should continue to be
covered by tests that include `import std;` in source. Broader automatic runtime
dependency injection belongs to the lowering/runtime-header design, not this
proposal.

## Tests

Update existing tests to make the new contract explicit:

- parser no longer marks `std` imports specially
- codegen verifies `import std;` is emitted verbatim
- pipeline `transpile("import std; ...")` no longer emits the header preamble
- generated `carven init` and single-file xmake projects contain module policies
- golden files change from include preambles to real `import std;`
- E2E keeps using xmake and compiles at least one target with real `import std;`

When testing on toolchains without working standard-library module support, the
E2E should fail as a build-environment limitation rather than falling back inside
Carven.

## Non-Goals

- Do not implement a header fallback mode.
- Do not detect compiler-specific std module support in Carven.
- Do not implement Carven package/module resolution.
- Do not solve lower C++ standard targets; that belongs to the lowering
  strategy.

## Open Questions

- Should the minimum default generated C++ standard become `c++23` permanently,
  or should project authors be allowed to opt into `c++latest`?
- Should Carven keep a short-lived compatibility warning for `--import-std`, or
  remove the flag immediately?
- Should the xmake rule set module policies directly on targets, or should
  generated `xmake.lua` files remain responsible for declaring them?
