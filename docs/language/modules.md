# Modules and names

[Language](README.md)

Module resolution and declaration visibility determine which names a program can use.

## Compilations, crafts, and modules

A compilation is one closed compiler boundary supplied by the driver or build
system. Its module catalog contains the source batch assembled by that caller and
the compiler-provided standard modules. Every source input has one canonical
module path, a nonempty sequence of components matching
`[A-Za-z_][A-Za-z0-9_]*`, including keyword spellings.
Deriving canonical module paths from host filenames is driver policy; the
language rules operate on the resulting paths.

A leading canonical path of `crafts.<name>.<path>` belongs to the module domain
anchored by `crafts.<name>`. All other canonical paths belong to the unprefixed
module domain. The reserved leading form requires both `<name>` and a nonempty
`<path>`; `crafts` and `crafts.<name>` alone are not complete module paths. A
non-leading `crafts` component is ordinary. The `crafts.<name>` prefix gives
modules a craft-qualified domain for name resolution and bare declaration
visibility within the compilation.

The official craft is `carven`, stored in `crafts/carven/`. Its source
standard-library modules live under `std/`. The import prefix `std::` selects the
official logical module domain for source and compiler-provided declarations,
so `std::utf` names `crafts.carven.std.utf`.

Imports form a prefix at the start of a module. Module imports use one of three
structured references:

- `model.user` starts at the importing module's domain root;
- `.value` starts in the importing module's logical module directory;
- `json::parser` starts at the external craft `json` and names `crafts.json.parser`.

A module's logical directory is its canonical path without the final module
name; removing that name does not change the module domain. The reference
components after `.` are appended to that directory. For example, `.value` in
`a.b.main` denotes `a.b.value`, and `.value` in `crafts.json.a.b.main` denotes
`crafts.json.a.b.value`.

Unprefixed and leading-`.` references stay inside the importer's module domain.
Craft-qualified references select the named craft, with `std::` selecting
`crafts.carven.std`. The normalized path identifies the module. An absent target
or source self-import is an error.
A standard entry combines any supplied source declarations with its
compiler-provided declarations. Both use ordinary lookup and visibility rules;
conflicting declarations are errors. `std::core` and `carven::std.core` select the
same module.

Compiler-provided declarations require no matching source file. Source exports
require their modules in the source batch. Directory layout does not determine
whether a declaration is provided or source-defined.

`crafts/carven/std/utf.cv` defines `std::utf`, and
`crafts/carven/std/simd/bytes.cv` defines `std::simd.bytes`. Each path selects
one module's declarations. A directory prefix groups paths; it does not import
sibling or descendant modules.

For an importer at `crafts/foo/models/user.cv`, the three forms select:

| Import | Input module path |
| --- | --- |
| `std::utf` | `crafts/carven/std/utf.cv` |
| `std.utf` | `crafts/foo/std/utf.cv` |
| `.std.utf` | `crafts/foo/models/std/utf.cv` |

Cross-module selection always requires an explicit import. A selected name
must be visible to the importer under the declaration rules below. Explicit
selections cannot collide with a module declaration or select one local name
twice, and they deterministically shadow same-name wildcard candidates.
Multiple wildcard providers are ambiguous only when an actual use requires that
name. Importing a module selects its visible declarations. Imported bindings remain
local to the importing module. Import declarations have
no runtime side effects. A declaration is used
when one of its selected bindings uniquely resolves an actual reference; an
otherwise unused declaration produces one `CV-LINT-UNUSED-IMPORT` warning.

The same prefix may contain C++ header imports. These supply native
declarations; a `using` clause also supplies external name lookup. They do not
resolve Carven modules.

## Standard modules and defaults

This table lists public compiler-provided exports and default bindings.

| Provided module | Exports | Default bindings supplied |
| --- | --- | --- |
| `std::core` | `bool`, `char`, `void`, `str`, `String`; `i8`, `i16`, `i32`, `i64`, `isize`; `u8`, `u16`, `u32`, `u64`, `usize`; `f32`, `f64`; `Vector`, `Range`; `assert`, `addressof` | These listed names |
| `std::io` | `print`, `println`, `eprint`, `eprintln` | Printing names and source-defined `stdin` |
| `std::testing` | `check`, `require`, `fail` | These listed names |
| `std::async` | `Operation`, `cancel_requested`, `cancel_point`, `yield_once`, `wait_ready`, `close`, `when_all`, `when_any`, `first_successful` | None |
| `std::simd` | `u8x16`, `mask16`, `f32x4`, `mask4`, `u8x32`, `mask32`, `f32x8`, `mask8` | These listed names |
| `std::time` | `timeout` | None |

The table specifies which names are available by default and which module
supplies their bindings. Explicit imports of default names use ordinary import
usage tracking. Test helpers retain their
active-test admission rules; `assert` works outside tests.

Standard entries expose compiler-provided declarations and source exports through
ordinary module paths and import selections. The default `stdin` binding selects
the same source declaration as an explicit import from `std::io`.
The associated functions `char::from_u32` and `str::from_utf8` use the same
declarations as `char_from_u32` and `from_utf8` from `std::utf`.

Crafts are source packages. Import their APIs through module paths such as
`std::json`, `std::simd.bytes`, and `std::utf`. `std::io` combines
builtin printing with source input and framing APIs. `std::time` combines
builtin timeout composition with source time values and waiting APIs.
A directory alone does not define an importable module. The compiler, runtime,
and standard sources ship together.

Importing an operation does not bind the types in its signature. Results and
failures can be inferred, used, and propagated without naming their types.
Import a standard type that is not already in scope when naming it in an
annotation, constructor, or failure handler. For example, duration literals need
no `Duration` binding, and numeric parsing needs no `NumberParseError` binding;
explicitly naming those types requires
imports from `std::time` and `std::core`, respectively.

## Library source organization

A public library entry is an ordinary module containing its exported declarations.
A capability can occupy one `.cv` file or place implementation modules beside it
under a matching directory. For example, `std/utf.cv` exports the UTF API, while
`std/utf/scan.cv` and `std/utf/block.cv` implement scanning. Every file retains its
ordinary module identity and explicit dependencies.

Use bare declarations for helpers shared within the Craft and `private` for
helpers confined to one module. Export declarations from the entry that owns their
public contract. Import selections remain local bindings; they do not re-export
declarations or turn a parent module into an aggregate of its children.

An independently useful public submodule follows the same rules at its own path.
`std::simd.bytes` and `std::simd.floats` provide operations for their element
families; `std::simd.scan` provides byte-set matching and scanning. Public path
depth follows the capability, while implementation files follow their responsibilities.

## Import aliases

```carven
import std::core using String as Text;
import std::async using { Operation, when_all as join };
import std::simd.bytes using load_block as load_bytes;
import std::simd.floats using load_block as load_floats;
```

The export name before `as` is checked against the target module's visibility;
the alias is the sole local name introduced by that selection. Both are ordinary
identifiers. Single and list selections share these rules; wildcard imports have
no alias clause. Distinct local names may select the same entity. Repeating a
local name is an error even for the same entity. Import aliases are local to their module.

Aliases preserve the selected entity and its type, ownership, failure, and call
contracts.
Default bindings are independent: `print as write` does not remove default `print`.

## Import style

Group imports in official source in this order:

1. Modules in the current craft, using unprefixed or leading-`.` references.
2. Modules in other crafts, using craft-qualified references.
3. Standard-library modules, using `std::`.
4. Native C++ headers.

Sort module references lexically within each group. Use one blank line to
separate authored groups when needed. Keep each selection list with its module
import. Preserve the required order of native headers; order independent headers
by the [C++ header style](../development/conventions.md#header-order).
Keep `std::` imports in the standard-library group within the official craft
as well. Module resolution and name conflicts are independent of import order.
The formatter preserves the written order.

## Declarations and names

Modules may declare functions, structures, classes, enums, constants, tests, constant
blocks, and C++ source fragments. Function, structure, class, enum, and constant names
share one module namespace. Duplicate module declarations are invalid; function
overloading is not supported.

Bare source declaration names require identifier tokens, so reserved keywords
cannot name declarations. Only `ptr` has an additional reservation for module
declarations, type parameters, and import local names, because `ptr<T>` and
`ptr<&T>` are structural type syntax. Named builtin exports can be shadowed.
Runtime locals, parameters, and members retain their ordinary identifier rules.

Module path components and qualified/member components accept identifier-shaped
keyword tokens. This permits keyword-named native members and paths without
permitting bare source declarations with those names. Source-module `using`
selections still require ordinary identifiers. There is no escaped-identifier form.

Nominal and callable identities are collected across the closed compilation.
Declaration signatures, constants, and any function bodies needed
for static execution are completed through their dependencies. Declaration order
therefore does not control whether a module declaration can be named; valid
forward constant dependencies and forward or mutually recursive function calls
are supported. A cycle among constant-required facts is invalid.

Expression lookup selects lexical bindings, then module declarations and Carven
imports, explicit native selections, default standard bindings, and finally
opened C++ namespaces. Module declarations suppress wildcard candidates; explicit
Carven imports suppress wildcards but conflict with module declarations.
Several wildcard providers remain ambiguous even if they select the same entity.
Explicit native selections conflict with actual Carven bindings, but can replace
default names. A selected wrong-kind entity is an error; lookup never falls back
to a default because its contract would fit better.

Type positions select type parameters before this module layer and ignore runtime
locals and parameters. Expression qualifiers select lexical bindings and type
parameters before the module layer. A type parameter requires a concrete type
before it can supply associated operations. For runtime shadowing,
`fn f(String: i32) -> String` still names the default result type, but that
parameter blocks `String::from_str`. Import `String as Text` to use
`Text::from_str` in that scope. Literal suffixes, inferred types, and structural
syntax retain their canonical meanings independently of bound names.

A declaration in an inner lexical scope may shadow an outer binding; declaring
the same name twice in one lexical scope is invalid. Lambda bodies retain their
capture boundary: free runtime bindings must be captured explicitly.

Named module functions, structures, classes, enums, and module constants use one visibility
model:

| Declaration form | Audience |
| --- | --- |
| `private` | Only its defining module |
| bare (no visibility modifier) | All modules in the same craft (module domain) |
| `export` | All modules in the compilation, across crafts |

Import resolution selects a module; visibility determines which of its
declarations the importer may select.

Visibility follows the defining craft, not the spelling used to import it.
Official standard-library modules belong to the `carven` craft, including those
selected through `std::`. Ordinary application modules share the unprefixed
module domain and can import each other's bare declarations.

Every declaration is visible in its defining module. In expressions, `::`
selects type-associated operations, enum cases, or qualified native names.
`char::from_u32_unchecked` selects an associated operation of the `char` type.
Provided callables such as `print` and imported `when_all` use ordinary
unqualified lookup.

A declaration surface may refer only to nominal declarations whose audience
contains the surface's audience. This structural check recursively covers
function parameters, results, failure sets, nested callable types, structure
fields, enum payloads and underlying types, arrays, and a module constant's
type and normalized semantic value. Function bodies and constant-evaluation
proofs are implementation; identities eliminated by normalization do not enter
the published surface. A violation uses `CV-TYPE-VISIBILITY-LEAK`.
Class fields are private to the class body and may use module-private types;
class operations remain subject to their declared audience.
