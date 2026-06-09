# Carven Distribution

## Goal

Define how Carven is installed and distributed as a language toolchain.

The immediate goal is to make a locally built Carven usable through `xmake
install`. Later distribution layers, such as an xrepo package and binary
archives, should build on the same install layout instead of inventing separate
paths.

## Recommended Order

1. Support `xmake install carven` as the first-class local install path.
2. Stabilize the installed toolchain layout.
3. Keep `scripts/install.sh` as a small source-install wrapper.
4. Add a Homebrew tap for macOS users.
5. Add an xrepo package that builds and installs Carven from source.
6. Add binary release packaging, likely through `xmake pack` or CI-specific
   packaging.

## Install Layout

`xmake install carven` should eventually install the complete Carven toolchain:

```text
<prefix>/bin/carven
<prefix>/share/carven/xmake/rules/carven.lua
<prefix>/include/carven/runtime.hpp
```

The binary is the user-facing CLI and the build-time transpiler invoked by xmake
rules. The shared rule and runtime header are toolchain assets, not per-project
implementation details.

Rule files should be installed as shared data, not next to the executable:

```lua
target("carven")
    add_installfiles("(xmake/rules/*.lua)", {prefixdir = "share/carven"})
```

The parenthesized path keeps the `xmake/rules` directory structure under
`share/carven`, so adding more rule files later does not require changing the
install shape.

Keeping rule files out of `bin` follows xmake's normal install model:
executables live in `bin`, headers live in `include`, and toolchain data lives
under `share/<tool>`.

## Local Install

For source users and early development, the recommended flow is:

```shell
xmake f --toolchain=llvm
xmake build carven
xmake install -o ~/.local carven
```

The default user-level prefix should be `$HOME/.local`. That installs the CLI
as `$HOME/.local/bin/carven` and shared assets under
`$HOME/.local/share/carven`.

This follows the XDG Base Directory Specification's user executable convention:
user-specific executable files may be stored in `$HOME/.local/bin`, while
user-specific data defaults to `$HOME/.local/share`.

Carven should also provide a small source install script:

```shell
./scripts/install.sh
./scripts/install.sh --prefix /usr/local
```

The script should only run `xmake build carven` and
`xmake install -o <prefix> carven`. It must not edit shell profiles, export
environment variables, create Carven-specific hidden home directories such as
`~/.carven`, or otherwise mutate user state beyond the install prefix. Without
`--prefix`, it may create `$HOME/.local/bin` as the standard user executable
directory. If `<prefix>/bin` is not on `PATH`, the script should report that
fact and leave the choice to the user.

`xmake install` does not build missing targets automatically, so documentation
should keep `xmake build carven` and `xmake install carven` as separate steps.

Uninstall should also stay with xmake instead of growing a Carven-specific
script:

```shell
xmake uninstall --installdir="$HOME/.local" carven
```

Build cleanup should likewise use xmake's own cleanup commands first:

```shell
xmake clean -a
```

For C++ module/BMI cache corruption during Carven development, a stronger local
reset is acceptable:

```shell
rm -rf build .xmake
xmake f --toolchain=llvm
xmake build
```

## Install Script Scope

`scripts/install.sh` is useful and should stay, but it should not become
Carven's package manager.

Its job is intentionally small:

- build the `carven` target
- call `xmake install -o <prefix> carven`
- default to `$HOME/.local`
- create `$HOME/.local/bin` when using the default prefix
- report whether `<prefix>/bin` is visible on `PATH`

It should not:

- edit shell profiles
- own upgrades or uninstalls
- own build-cache cleanup
- decide system-wide install policy
- duplicate the install layout already defined by `xmake install`
- grow platform-specific package-manager behavior

This keeps source installs pleasant while leaving normal user distribution to
real package managers.

## Homebrew Direction

Homebrew should be the first user-facing package manager for macOS. It solves
the parts Carven should not own directly: package discovery, upgrades,
uninstalls, formula testing, bottles, and linking the executable into
Homebrew's prefix.

The first step should be a project-owned tap, not `homebrew-core`:

```shell
brew tap-new <owner>/carven
```

The conventional GitHub repository name would be:

```text
<owner>/homebrew-carven
```

Users would install Carven with either:

```shell
brew tap <owner>/carven
brew install carven
```

or:

```shell
brew install <owner>/carven/carven
```

The formula should call the same install contract used everywhere else:

```ruby
class Carven < Formula
  desc "Programming language that transpiles to standard C++"
  homepage "https://github.com/<owner>/carven"
  url "https://github.com/<owner>/carven/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "<sha256>"
  license "MIT"

  depends_on "xmake"
  depends_on "llvm" => :build

  def install
    ENV.prepend_path "PATH", Formula["llvm"].opt_bin

    system "xmake", "f", "--toolchain=llvm", "--mode=release"
    system "xmake", "build", "carven"
    system "xmake", "install", "-o", prefix, "carven"
  end

  test do
    assert_match "carven", shell_output("#{bin}/carven --version")
    assert_path_exists pkgshare/"xmake/rules/carven.lua"

    (testpath/"main.cv").write <<~EOS
      fn main() {
      }
    EOS

    assert_match "auto main()", shell_output("#{bin}/carven transpile #{testpath}/main.cv")
  end
end
```

The formula should not manually install `carven` or the rule file. It should let
`xmake install` define the layout so Homebrew, xrepo, local source installs, and
CI all agree.

Before publishing the tap, Carven needs:

- a stable release tag
- a source tarball checksum
- a formula test that does not depend on external project state
- confirmation that the installed rule is available under
  `share/carven/xmake/rules/carven.lua`

Local formula validation should use:

```shell
brew install --build-from-source ./Formula/carven.rb
brew test carven
brew audit --strict --online carven
```

Moving to `homebrew-core` is a later question. A project-owned tap is enough for
early distribution and gives Carven room to change without Homebrew core review
pressure.

## Xrepo Direction

The xrepo package should model Carven as a build tool, similar to xrepo's
`cppfront` package:

```lua
package("carven")
    set_kind("toolchain")
```

The package should:

- use `package:find_tool("carven", {check = "--version"})` for system tool
  discovery when appropriate
- build from source with xmake
- install the same layout as `xmake install carven`
- rely on xmake's default PATH export for `binary` or `toolchain` packages,
  where the package `bin` directory is made available to consumers
- provide a minimal `on_test` that verifies `carven --version` and a tiny
  transpile flow

This keeps xrepo as a distribution and discovery layer. It should not replace
the normal project build model.

Doing xrepo first is possible, but it would force the package and rule contract
to stabilize before the local install layout is proven. `add_requires("carven")`
and `add_packages("carven")` can provide the tool package to a target, but they
do not by themselves teach xmake how to compile `.cv` files unless the `carven`
rule is also available.

There are two likely xrepo-era rule shapes:

```lua
-- If Carven's rule is still project-vendored or installed separately:
includes("xmake/rules/carven.lua")
add_requires("carven")

target("app")
    set_kind("binary")
    add_packages("carven")
    add_rules("carven")
    add_files("src/main.cv")
```

```lua
-- If the xrepo package installs package rules under its package rules dir:
add_requires("carven")

target("app")
    set_kind("binary")
    add_packages("carven")
    add_rules("@carven/carven")
    add_files("src/main.cv")
```

Package rules have extra constraints compared with normal project rules; xmake
warns that package rules should use `on_config` instead of `on_load`. The current
Carven rule uses `on_load` to patch source kinds and future include paths, so it
should be adapted before becoming a package rule.

Before switching `carven init` to emit the package-rule form, confirm that the
package rule can preserve the same behavior without `on_load`.

## Rule And Package Relationship

The xmake rule and the xrepo package are related but separate concerns:

- the package installs or discovers the `carven` toolchain
- the rule teaches xmake how to build `.cv` files

The rule should eventually support this lookup order:

```text
target:values("carven.program")
CARVEN environment variable
target:pkg("carven")
find_tool("carven")
```

This lets local development, explicit overrides, xrepo package use, and normal
PATH-based installs all share the same rule.

## Binary Packaging

`xmake pack` is a later release layer. It should package a known-good install
layout into archives or platform packages. It should not be the first mechanism
used to define where Carven's toolchain files belong.

## Non-Goals

- Do not build a Carven package manager for v1.
- Do not require xrepo before local `xmake install` works.
- Do not require Homebrew for source users or development workflows.
- Do not duplicate the xmake rule in multiple install-specific forms.

## Open Questions

- Should project scaffolds vendor `xmake/rules/carven.lua` forever, or switch to
  a shared installed rule when Carven is installed?
- Should xrepo package rules be the first non-vendored rule mechanism, or should
  Carven also support loading the shared installed rule from
  `<prefix>/share/carven/xmake/rules/carven.lua`?
- Should the runtime header be included by every generated C++ file, or only
  when generated constructs need runtime helpers?
- What compiler and C++ module capability checks should the future xrepo package
  enforce?
- What should the official Homebrew tap owner/name be?
- Should the Homebrew formula build from source only at first, or should it add
  bottles immediately once CI is ready?
