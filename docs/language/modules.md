# Modules and names

[Language](README.md)

Module resolution and declaration visibility determine which names a program can use.

## Compilations, crafts, and modules

A compilation is one closed compiler boundary supplied by the driver or build
system. Its module catalog contains the source batch assembled by that caller. Every
source input has one canonical module path, a nonempty sequence of components matching
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

The official craft is `carven`, stored in `crafts/carven/`. Its standard-library
modules live under `std/`. The reserved import prefix `std::` selects that
standard library, so `std::utf.text` names `crafts.carven.std.utf.text`.

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
`crafts.carven.std`. Resolution succeeds only when the resulting path names
another module in the supplied input batch; an absent module or self-import is
an error.

For an importer at `crafts/foo/models/user.cv`, the three forms select:

| Import | Input module path |
| --- | --- |
| `std::utf.text` | `crafts/carven/std/utf/text.cv` |
| `std.utf` | `crafts/foo/std/utf.cv` |
| `.std.utf` | `crafts/foo/models/std/utf.cv` |

Cross-module selection always requires an explicit import. A selected name
must be visible to the importer under the declaration rules below. Explicit
selections cannot collide with a local declaration or bind one name to different
symbols, and they deterministically shadow same-name wildcard candidates.
Multiple wildcard providers are ambiguous only when an actual use requires that
name. Import declarations have no runtime side effects. A declaration is used
when one of its selected bindings uniquely resolves an actual reference; an
otherwise unused declaration produces one `CV-LINT-UNUSED-IMPORT` warning.

The same prefix may contain C++ header imports. These supply native
declarations; a `using` clause also supplies external name lookup. They do not
resolve Carven modules.

## Declarations and names

Modules may declare functions, structures, classes, enums, constants, tests, constant
blocks, and C++ source fragments. Function, structure, class, enum, and constant names
share one module namespace. Duplicate module declarations are invalid; function
overloading is not supported.

Nominal and callable identities are collected across the closed compilation.
Declaration signatures, required constant facts, and any function bodies needed
for constant execution are completed through their dependencies. Declaration order
therefore does not control whether a module declaration can be named; valid
forward constant dependencies and forward or mutually recursive function calls
are supported. A cycle among constant-required facts is invalid.

Unqualified lookup checks the innermost lexical scope first, then enclosing
scopes, module declarations, and imports. A declaration in an inner lexical
scope may shadow an outer binding. Declaring the same name twice in one lexical
scope is invalid. Lambda bodies add a capture boundary: a free runtime binding
must be captured explicitly before ordinary lexical lookup may cross it.

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

Every declaration is visible in its defining module. Qualified lookup selects
the named module or nominal owner.

A declaration surface may refer only to nominal declarations whose audience
contains the surface's audience. This structural check recursively covers
function parameters, results, failure sets, nested callable types, structure
fields, enum payloads and underlying types, arrays, and a module constant's
type and normalized semantic value. Function bodies and constant-evaluation
proofs are implementation; identities eliminated by normalization do not enter
the published surface. A violation uses `CV-TYPE-VISIBILITY-LEAK`.
Class fields are private to the class body and may use module-private types;
class operations remain subject to their declared audience.
