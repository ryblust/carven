# Standard modules and explicit symbol bindings

- **Status:** Draft
- **Implementation:** Partial: runtime domain layout implemented; module binding not started
- **Scope:** Compiler-provided standard modules, default names, import renaming, unified symbol selection, and source-build integration

## Summary

Give named builtin types and operations owning standard modules. Supply common
APIs through an explicit default binding set; allow explicit imports and local
aliases, and require imports for specialized names. Preserve compiler-defined
contracts for operations such as variadic printing and async composition without
requiring Carven source to express their implementations.

Use one import-path resolution and name-selection model for source modules and
compiler-provided modules. Normalize a path, resolve its module identity, and
select that module's visible exports. The target's origin determines where its
exports come from and how selected entities are interpreted.

Keep Crafts as source packages. Standard and user source modules remain directly
importable under their existing paths and declaration visibility rules. Source
APIs are defined by their actual declarations; adding an ordinary source export
does not require an entry in compiler metadata. This scope supersedes the earlier
compiler-owned mapping of source exports into aggregate standard interfaces and
the restriction on importing deeper standard source modules.

## Current constraints

- `std::` selects the official `crafts.carven.std` domain. Import paths
  currently resolve only to modules in the caller-supplied source batch.
- Ordinary source declarations, explicit imports, and wildcard imports
  have conflict, visibility, ambiguity, and unused-import rules. Imports select
  declarations, not the target module's imported names.
- Builtin callables are selected by spelling after ordinary Carven lookup.
  Async selection repeats this fallback in body calls, static-callable lookup,
  constant analysis, and awaited child operations.
- Fixed type names and `Vector`, `Range`, and `AsyncOperation` bypass ordinary
  type selection and are reserved for module declarations and type parameters.
  `String::from_str` also bypasses a same-name local runtime value.
- `ptr<T>` and `ptr<&T>` are parsed directly into `ASTPointerType`. Pointer target
  access is syntax, not an ordinary generic type argument.
- Time, UTF, JSON, number parsing, and SIMD algorithms are real source APIs.
  `timeout` additionally checks the existing source declarations `Duration`,
  `Timeout`, and time `RangeError` at fixed official paths.
- `carven-xmake-repo` collects installed and project Crafts by physical source
  files. It shares installed-library native objects by compilation configuration,
  registers generated implementations by source module, and validates the actual
  artifact manifest. Its rules contain no standard export-name dispatch.

## Module organization

Keep `std::` and the existing module-path syntax. The prefix selects the official
`crafts.carven.std` domain for both source and compiler-provided modules. The
following compiler-provided modules group builtin APIs by capability. Their
current one-level layout is an API organization choice, not a module-path depth
restriction.

| Compiler-provided module | Exports |
| --- | --- |
| `std::core` | `bool`, `char`, `void`, `str`, `String`; `i8`, `i16`, `i32`, `i64`, `isize`; `u8`, `u16`, `u32`, `u64`, `usize`; `f32`, `f64`; `Vector`, `Range`; `assert`, `addressof` |
| `std::io` | `print`, `println`, `eprint`, `eprintln` |
| `std::testing` | `check`, `require`, `fail` |
| `std::async` | `Operation`, `cancellation_requested`, `cancel_point`, `yield_once`, `wait_ready`, `close`, `when_all`, `when_any`, `first_successful` |
| `std::simd` | `u8x16`, `mask16`, `f32x4`, `mask4`, `u8x32`, `mask32`, `f32x8`, `mask8` |
| `std::time` | `timeout` |

`std::core` owns core value types and a small set of foundational operations:
scalar and text types, `Vector`, `Range`, assertion, and address access. Keep
domain-specific capabilities with their owners: printing in `std::io`, test
reporting in `std::testing`, async operations in `std::async`, and vector
primitives in `std::simd`. Frequency of use alone does not place an API in
`std::core`. Module ownership and default name availability are separate choices;
a commonly used API can remain in its domain module and be available by default.

A compiler-provided module is a logical importable module with a canonical path
and an export set. It can be described as a *virtual module*: it has no `.cv`
source file, AST, or generated implementation of its own. Its exported entities
still have canonical identities, semantic contracts, and runtime support where
required. Virtual describes its origin, not a separate import syntax or a weaker
name-resolution contract. `std::core` is such a module.

Use one candidate-selection policy for defaults and imports. Allow explicit
imports and aliases of default names through their owning modules. Keep compiler
enums and operation kinds independent of public spellings.

Use the same default set in functions, tests, helpers, and static analysis.
Require an active test for `check`, `require`, and `fail`; `assert` is available
outside tests. Preserve the syntax and execution rules of `test` and `const test`.

Keep the existing source module hierarchy. Grouping follows capabilities and
coherent use, without requiring every standard module path to have the same
depth. This change does not flatten JSON or SIMD source modules or add a mechanism
solely to provide a single-level entry point. Source modules keep their existing
paths and exports, including:

| Source module | Example exports |
| --- | --- |
| `std::time.duration` | `Duration`, `RangeError`, `milliseconds`, `seconds` |
| `std::time.clock` | `TimePoint` |
| `std::time.sleep` | `sleep`, `sleep_until` |
| `std::time.timeout` | `Timeout` |
| `std::utf.text` | `from_utf8`, `to_string` |
| `std::json.value` | `Value`, `ValueKind` |
| `std::number.parse` | `ParseError`, `parse_i64`, `parse_u64`, `parse_f64` |
| `std::simd.bytes`, `std::simd.floats` | Each defines its own `block_count`, `load_block`, and other helpers |
| `std::simd.scan` | `ByteSet`, `one_of`, `match_set`, and other scan helpers |

A path prefix does not aggregate the exports of descendant modules. Importing
`std::simd` selects provided vector and mask types; importing `std::simd.bytes`
selects declarations in `bytes.cv`. Importing `std::time` selects `timeout`;
`Duration` remains in `std::time.duration`. A source directory alone does not
establish an importable module such as `std::json` or `std::utf`.

Applications can import standard source modules directly, subject to ordinary
`private`, craft-visible, and `export` rules. Source modules select declarations,
not the imported names of their dependencies. Keep the same model for ordinary
Crafts; this scope adds no aggregate export mapping or re-export facility.

Keep entry arguments and character-iteration views as inferred compiler-only
types. Attach associated factories and receiver operations to canonical types.

### Physical source layout

Retain the current source and native-support files under `crafts/carven`:

```text
crafts/carven/
├── runtime/                       native support grouped by domain
│   ├── call/
│   ├── collection/
│   ├── diagnostics/
│   ├── display/
│   ├── execution/
│   ├── format/
│   ├── io/
│   ├── numeric/
│   ├── simd/
│   ├── testing/
│   ├── text/
│   └── value/
└── std/
    ├── README.md
    ├── time/
    │   ├── README.md
    │   ├── duration.cv
    │   ├── clock.cv
    │   ├── sleep.cv
    │   ├── timeout.cv
    │   └── native.hpp
    ├── utf/
    │   ├── README.md
    │   ├── error.cv
    │   ├── scalar.cv
    │   ├── codec.cv
    │   ├── validation.cv
    │   ├── text.cv
    │   ├── scan.cv
    │   └── block.cv
    ├── simd/
    │   ├── README.md
    │   ├── bytes.cv
    │   ├── floats.cv
    │   └── scan.cv
    ├── json/
    │   ├── README.md
    │   ├── error.cv
    │   ├── value.cv
    │   ├── validation.cv
    │   ├── string.cv
    │   ├── node.cv
    │   ├── syntax.cv
    │   ├── parse.cv
    │   └── serialize.cv
    └── number/
        ├── README.md
        ├── parse.cv
        └── parse.hpp
```

Keep source modules, algorithms, and native adapters with their owners. Their
canonical module identities continue to determine generated implementation paths:
`Duration` from `std::time.duration` belongs to
`crafts/carven/std/time/duration.cpp`.

Runtime domains group related support headers, such as `collection/array.hpp`,
`value/outcome.hpp`, and `format/writer.hpp`. Consumers select the contracts they
use; domain entries organize shared implementation headers where needed.
Their include paths and standard native imports use angle brackets with the
complete `carven/` prefix under the Crafts search root, following the
[runtime domain conventions](../docs/development/conventions.md#runtime-domains).
Crafts discovery checks the `crafts/carven` directory.

Describe only compiler-provided modules and exports in finite metadata under
`src/`. Create source files and generated native owners only for actual source
implementations. Import `Operation` from `std::async` in `time/clock.cv` and named
vector and mask types from `std::simd` in SIMD and UTF source. Keep existing
source-module imports and source declaration spellings.

### Public naming

Use `Operation` as the provided async type's public name in place of
`AsyncOperation`. Keep the other provided names listed above and their existing
contracts. Choose clear local aliases when imported source names conflict.
Preserve acronym spelling in type aliases and use lower-snake-case callable
aliases.

Source API names remain owned by their declarations. The earlier facade-only
source renamings are superseded by direct source imports and local aliases;
this change does not rename ordinary source declarations or add alternative
compiler-registered public names for them.

```carven
import std::json.value using Value as JSONValue;
import std::number.parse using ParseError as NumberParseError;
import std::simd using u8x32 as Bytes;
import std::simd.bytes using { ByteBlock, load_block as load_byte_block };
import std::simd.floats using { FloatBlock, load_block as load_float_block };
```

Each callable name selects one entity before argument checking. Aliases preserve
the original callable or type identity and do not introduce overload resolution.

## Default names and explicit imports

The following names are available without an import. Each retains its owning
module and can also be selected explicitly or given a local import alias.

| Owning module | Names available by default |
| --- | --- |
| `std::core` | `bool`, `char`, `void`, `str`, `String`; `i8`, `i16`, `i32`, `i64`, `isize`; `u8`, `u16`, `u32`, `u64`, `usize`; `f32`, `f64`; `Vector`, `Range`; `assert`, `addressof` |
| `std::io` | `print`, `println`, `eprint`, `eprintln` |
| `std::testing` | `check`, `require`, `fail` |

Bind each default name weakly to its owning export. Explicit imports select the
same export identity. Default availability applies to these individual names;
it creates no module import declaration. Track usage for written import
declarations.

Explicit imports of default names are legal:

```carven
import std::io using print;

print("ready");
```

Select the explicit binding and record its use. Allow explicit imports to state
an API's origin, override a wildcard candidate, or establish an alias. Default
names remain available when their explicit import is omitted.

Specialized names require imports, including in standard source modules:

```carven
import std::async using { Operation, when_all as join };
import std::simd using u8x16 as Bytes;
```

Bind only the names selected by an import. Permit inferred async and SIMD values
and their operations without binding the result type's name. Check the semantic
contract of each operation independently of name availability.

Enumerate the default set separately from module exports. Treat additions to the
default set as changes to language name selection. Adding an export to a module,
including `std::core` or `std::io`, does not make that new name available by
default unless the default set is explicitly extended. Apply one fixed default
set.

## Import renaming

Extend single and list selections with `as`:

```carven
import std::io using print as write;
import .model using { Value as ModelValue, validate };
import std::async using { close as finish_child, when_all as join };
```

The name before `as` selects an export under its defining module's visibility
rules. The name after `as` is the sole local name introduced by that selection.
An unrenamed selection uses its export name as the local name. Both names must
be identifiers admitted in these positions.

Renaming preserves symbol identity, type identity, call admission, ownership,
failure behavior, static-execution support, and associated operations. For
example, `Bytes::splat` selects the same operation as the explicitly imported
`u8x16::splat`; `await finish_child(...)` checks the existing child-close contract.
Renaming `when_all` does not make it a first-class callable when its current
contract only admits direct calls.

Source and compiler-provided module selections use exactly these rules:

- Distinct local names may select the same export, including in one list.
- Selecting one local name twice is an error, even when both selections refer
  to the same export. Defaults do not count as duplicate explicit selections.
- An explicit selection may not collide with a module declaration. Choose an
  alias when both entities are needed.
- Renaming introduces no additional unrenamed binding. After `print as write`,
  `print` may still be available through its independent default binding.
- Wildcards select visible exported or craft-visible declarations under their
  original names. Wildcards have no rename clause. Repeated wildcard imports of
  the same target retain the current duplicate-import error.
- Imports and aliases are not re-exported by importing source modules. Provided
  modules expose only their declared builtin exports. There is no module alias
  or Carven module-qualified expression syntax.

Apply `as` to Carven module selections. Keep native header selection, namespace
opening, conflict rules, and qualified lookup unchanged.

## Selection and shadowing

Resolve the binding before checking whether it is a type, value, constructor, or
callable. An incompatible selected binding produces a diagnostic; it never causes
fallback to a same-name default API. Selection depends on symbol identity after
this point, including for all compiler-provided operations.

Expression lookup retains these ordered sources:

1. The nearest admissible lexical binding, subject to lambda capture boundaries.
2. Module declarations and Carven imports under the rules below.
3. Explicit native selections, where no existing Carven conflict was diagnosed.
4. Default standard bindings.
5. Opened C++ namespaces as fallback.

Within the module-name layer:

- A module declaration suppresses same-name wildcard candidates. An explicit
  selection with that name is a declaration-time conflict.
- An explicit Carven selection suppresses same-name wildcard candidates.
- Several wildcard candidates are ambiguous when the name is used. They prevent
  default fallback; equal entity identity does not erase the import ambiguity.
- Explicit native selections retain their current conflict with actual Carven
  module declarations and import candidates. A default binding alone does not
  cause such a conflict and can be shadowed by the native selection.

Keep one module namespace for source functions, constants, nominal types, and
classes. Remove the additional reservation of named builtin exports for module
declarations and type parameters. Keywords retain their grammar restrictions;
`ptr` retains its contextual type-syntax reservation described below.

Type positions first select an in-scope type parameter, then module bindings,
explicit native selections, default bindings, and native namespace fallback.
Runtime locals and runtime parameters do not occupy type positions. A selected
module function named `String`, however, blocks the default type `String` and
produces a wrong-kind diagnostic in a type annotation.

Apply expression shadowing to type-associated qualification, including
`String::from_str`. Use an import alias when a local value occupies the type's
public name:

```carven
import std::core using String as Text;

fn make(String: i32) -> Text {
    let _ = String;
    return Text::from_str("value");
}
```

A nearer local binding shadows an explicit import in expressions. A runtime
parameter named `String` leaves `-> String` available as a type annotation and
blocks `String::from_str` in the body. Apply these position-specific rules to
source and provided types.

## Language syntax and canonical identity

Keep structural syntax outside module binding: arrays, slices, tuples, callable
types, `ptr<T>`, `ptr<&T>`, ranges, literals, `async`, `await`, and `cancel`.
Parse `ptr` as a contextual structural type form; keep it outside import
selection and renaming.

The spelling `ptr` remains reserved for module declaration names, type parameters,
and Carven import local names. Runtime locals, parameters, and members retain
their existing identifier rules.

Resolve `Vector`, `Range`, and `Operation` through binding selection. Form types
from their selected identities using their finite type and failure-set arguments.

Canonical builtin types exist independently of the names bound in a module.
Two aliases of `String` denote one canonical type; aliases of `Vector` build the
same canonical `Vector<T>`; imports do not manufacture nominal declarations.
Literal suffixes such as `7i32`, inferred numeric types, range expressions, and
operation results continue to establish their canonical types through language
rules. Shadowing the identifier `i32` changes that name's lookup, not the
meaning of the suffix `i32`.

## Unified module resolution

Retain the existing separation between `resolve_import_path` and module target
resolution. All Carven module imports follow the same sequence:

1. Normalize the authored module reference using the existing domain-root,
   relative, and craft-qualified path rules. `std::core` and `carven::std.core`
   both identify `crafts.carven.std.core`.
2. Resolve that exact canonical path against supplied source module identities
   and the finite set of compiler-provided module identities. Return a tagged
   source or provided target. An absent target is an unresolved-module error;
   a supplied source at an exact provided-module path is a collision error.
3. Obtain visible exports from source declarations or provided metadata according
   to the target's origin. Source modules retain their visibility and self-import
   checks; all listed provided exports are public.
4. Apply the shared explicit, alias, wildcard, conflict, ambiguity, and import-use
   rules. Once an entity is selected, check its kind and semantic contract.

The provided-module identity check occurs after canonical normalization. Do not
choose an independent resolver from the written `std::` prefix or recognize
operations by import spelling at call sites. A deeper standard source path such
as `std::simd.bytes` resolves normally. Other paths beneath `crafts.carven.std`
are not implicitly provided modules, reserved interface paths, or hidden modules.
Equivalent canonical spellings select the same target and export identities.

The compiler's finite module/export description contains canonical paths, export
names, default-binding policy, and provided type, constructor, or operation
identities. Source declaration catalogs remain the authority for source exports;
there is no table mapping ordinary source APIs to virtual-module exports. Adding
or removing an ordinary source export changes what can be imported through its
source module without editing this metadata.

Keep specialized signatures, inference, ownership, failure checking, static
execution, and lowering with their semantic owners. A provided API can expose
variable-arity printing or composition-specific inference without a source body
or a public parameter-pack or higher-order generic mechanism. Its inputs and
results still participate in the shared type and ownership models.

Ship compatible compiler, runtime, and standard-source versions together.
Release coordination does not require duplicating ordinary source export lists
in the compiler. Compiler knowledge of specific source representations remains
limited to records required by a provided operation's checked contract.

### Time dependencies

The provided `std::time` module exports only `timeout`. Keep its existing source
record requirements and their original nominal identities:

| Source import path and declaration | Existing checked representation |
| --- | --- |
| `std::time.duration`, `Duration` | Exported struct with `nanoseconds: i64` |
| `std::time.duration`, `RangeError` | Empty exported struct |
| `std::time.timeout`, `Timeout` | Empty exported struct |

These are internal contract dependencies of `timeout`, not exports forwarded by
`std::time`. Resolve their canonical source module/declaration keys from the
supplied batch and reuse declaration completion and record-shape checks. They do
not depend on the caller's local names or aliases.

Merely importing `timeout`, including through a wildcard or an alias, does not
require these source records. A call that instead selects a shadowing local
entity does not activate the provided timeout contract. A use that selects the
provided operation and checks its contract requires the records and diagnoses
missing or incompatible source input. Ordinary checking of every supplied source
module still applies independently.

Importing `timeout` does not bind these type names in the caller. A caller names
them through their source modules, for example:

```carven
import std::time using timeout;
import std::time.duration using { Duration, RangeError as TimeRangeError };
import std::time.timeout using Timeout;
```

Typed failures can flow through inferred contracts and wildcard recovery without
requiring every failure type's name in local scope.

Assemble installed source crafts through the driver. In explicit-input mode,
source imports and contract dependencies resolve only against caller-supplied
modules. Analyze every supplied module, including its static effects, tests, and
declarations. A wildcard selects the target module's visible exports, not other
modules below its path.

Workspace navigation traverses ordinary source imports and includes configured
source modules needed by provided contracts. Share the finite contract-dependency
keys with semantic analysis rather than discovering them through a second set of
operation-name checks. Dependency discovery can include available prerequisite
modules conservatively; absence or incompatible record shape is diagnosed only
when the provided contract is checked. It does not add names to the caller or
manufacture source files.

## Model ownership

Keep module origin, local binding, entity identity, and native ownership distinct:

| Fact | Authority | Effect |
| --- | --- | --- |
| Module identity and origin | Canonical path resolution and source/provided target | Locates the export provider |
| Export name and entity | Source declaration catalog or provided export metadata | Visible selections and semantic identity |
| Local spelling and binding origin | Analysis selection and import-use state | Shadowing, ambiguity, alias references, and unused imports |
| Definition origin | Actual source declaration or provided export metadata | Definition navigation and source reporting |
| Native implementation ownership | Actual source module or existing runtime support | Linkage, artifact paths, and build dependencies |

Carry the selected entity and binding origin in each binding. Reuse source
catalog symbols and represent provided targets with finite catalog forms. Publish
presentation facts alongside semantic identities for downstream consumers.

Frontend and workspace module discovery need canonical paths and opaque provided
module/export keys. Keep these descriptions independent of SemIR types,
declaration completion, and backend operation representations. Narrow source
contract dependencies such as time record keys are shared discovery data; their
representation checks belong to semantic analysis.

One module-selection policy supplies source, imported, and default candidates to
type, expression, constant, and qualifier analysis. Lexical scopes retain their
current owners and admission rules. A consumer selects the identity before
checking its kind and interpreting its contract. The source/provided distinction
belongs to module resolution, export enumeration, and semantic dispatch; it does
not create separate precedence or alias rules.

Consolidate selection as follows:

- Remove callable spelling recognition from ordinary calls, static-callable
  selection, constant contexts, and the awaited observer precheck.
- Remove spelling-first primitive and constructor selection and the separate
  reservation of named builtin types. Keep structural pointer parsing.
- Use one precedence, ambiguity, wrong-kind, and import-use policy for the shared
  module layer. Selection results also supply source observations.
- Retain the finite standard-record contract resolver and ordinary source
  declaration completion; its dependency keys do not form an export registry.
- Keep public import topology out of native artifact ownership. The backend
  continues deriving dependencies from actual typed references.

Add provided-module metadata, tagged import targets and origins, and two name
spans for aliased selections. Retain canonical type formation, operation checks,
ownership, async semantics, static execution, and lowering. Assess complexity by
checking that consumers share selection rules and no longer repeat spelling
fallbacks.

### Presentation and aliases

Use source declaration names and provided exports' primary names as stable
entity presentation facts. A local alias does not establish a new type identity.
At source use sites, include the local alias when it explains a diagnostic; add
module ownership when distinct entities have the same displayed name. For
example, distinguish `Text` (the `String` from `std::core`) from a user-declared
`String` instead of reporting an unexplained `String` versus `String` mismatch.
Structural output and failure reports retain canonical entity names and enough
origin information to distinguish collisions.

Navigate source definitions to their real declarations. Navigate an aliased
local name to its import alias and use resolved entity identity for hover and
reference grouping. Present provided definitions as read-only module information
without inventing editable source declarations. Rename local aliases within their
binding's references. Publish these observations through source analysis.

## Coordination with carven-xmake-repo

Resolve source and provided module identities in the compiler. Build native
objects from the installed source library. Keep the rule's explicit-input
invocation and artifact-manifest contract. Provided-module metadata ships in the
compiler executable; source export lists remain in source declarations.

| Rule component | Coordination contract |
| --- | --- |
| `packages/c/carven/rules/carven/xmake.lua`: source collection and invocation | Continue collecting real installed and project sources and supplying their original canonical module identities. `--library-module` lists actual source modules, not virtual modules such as `std::core` or `std::time`. |
| `xmake.lua`: native source registration | Keep implementation paths and per-file native settings attached to original `.cv` inputs. Local import aliases do not change this registration. |
| `stdlib.lua`: producer selection | Share installed-library objects across compatible native configurations and prerequisite targets. |
| `generation.lua`: artifact validation and invalidation | Keep checking registered implementations against the actual manifest and hashing the compiler, rule files, source batch, and outputs. Provided metadata changes are covered by the compiler signature; source export changes by source content. Provided imports add neither fictitious inputs nor registered implementations. |
| Packaging and documentation | Ship source modules and native support with the matching compiler. Describe Crafts as source packages and distinguish source modules from compiler-provided modules. |

Keep manifest schema, source-library linkage domains, generated implementation
paths, and source provenance intact. `Duration` imported from `std::time.duration`
remains owned by `crafts.carven.std.time.duration`. A generated native interface
header is an output artifact, independent of the source or provided origin of an
importable module.

Retain complete-batch generation and the source producer's nonempty-source
requirement. Update builtin imports and validate producer/consumer builds with
the matching package rule. Derive rule behavior from source inventory, native
configuration, and artifact facts.

## Implementation

The essential change is selection before semantic dispatch. Preserve existing
operation representations and lowering; extend the import and analysis boundary
to describe where a module and binding come from.

| Area | Required change |
| --- | --- |
| Provided-module metadata | Add a finite description of canonical module paths, exported names, provided identities, and the explicit default set. Describe only the source contract dependencies needed by provided operations; keep ordinary source exports in the source catalog and semantic contracts in their existing owners. |
| `frontend/ast/decl.cppm`, `frontend/parse/decl.cpp` | Represent each selected import item with an export-name span and optional alias-name span. Use the same item record for single and list selections. Preserve separate export and local names. |
| `frontend/ast/topology.cppm`, `frontend/dump/ast/decl.cpp`, formatter | Visit, report, and format both spans and the `as` clause. Preserve import diagnostics at the relevant export or alias token. |
| `frontend/program/program.cppm`, `parse.cpp`, `verify.cpp` | Retain `resolve_import_path`. Extend target lookup and the resolved import target from only `ProgramModuleID` to source-or-provided. Check exact identity collisions and missing targets after normalization. Keep source provenance and graph traversal tied to real source modules. |
| `semantic/analysis/catalog/catalog.cppm`, `catalog.cpp` | Admit provided export forms alongside existing source symbols. Enumerate exports according to module origin, select by export name, and install candidates by local name. Apply shared conflict and import-use rules; weak defaults must not trigger native-import conflicts. |
| Shared module selection | Give body, type, qualifier, and constant analysis one candidate-selection and import-use rule. Keep lexical scope handling with the analysis context that owns it. Defaults refer to the original provided export and carry no synthetic import declaration. |
| `semantic/analysis/types/types.cpp`, `decl/generic.cpp` | Resolve named primitives and constructors from selected symbol identities. Remove spelling-first branches and named builtin reservations, retaining pointer syntax and keyword restrictions. Preserve current type formation, interning, and generic admission. |
| `semantic/analysis/body/names.cpp`, `construction.cpp`, `call.cpp`, `decl/resolve.cpp`, `decl/constant.cpp` | Dispatch provided symbols into existing `BuiltinSelection`, async intrinsic, composition, and timeout selections. Remove duplicate string fallbacks. Apply the same selection to static roots and type-associated qualifiers. |
| `semantic/analysis/body/expr_site.cpp` | Select an awaited call's callee once, then detect wait-ready or child-close by selected operation kind. |
| `semantic/analysis/async/async.cpp`, `types/standard.cpp` | Replace callable spelling discovery with selected provided identities. Preserve lazy time record checks, original source nominal identities, and source declaration completion; share their dependency keys with workspace discovery. |
| `semantic/analysis/expr/call.cppm`, `expr/member.cppm`, body patterns and delegation | Audit associated factories, generic applications, enum/pattern qualification, and expected-signature callable adaptation for consistent selected identities. Preserve restrictions on direct-only operations. |
| `semantic/analysis/source/source.cppm`, type/contract display, workspace analysis and semantic queries | Record source or provided definition origins and local alias bindings. Reuse canonical target resolution and selection evidence for navigation, references, hover, and diagnostics. Traverse real source dependencies; provide read-only metadata for provided modules. |
| Backend and static execution | Consume canonical types and typed operations. Publish stable entity presentation facts and disambiguate names when needed. Attach generated native linkage to the actual source or runtime implementation. |
| Source library and build integration | Preserve source-module identities, implementation paths, library-module selections, and artifact manifests. Supply real files and emitted artifacts to the rule. |

Delivery dependencies are:

1. Introduce provided-module metadata and tagged import targets while retaining
   shared path normalization and real source provenance.
2. Implement selection items, aliases, origins, weak default bindings, and shared
   candidate selection.
3. Route every named builtin type and operation entry point through selected
   identities, including static execution and awaited observer calls.
4. Update workspace queries, source consumers, diagnostics, and documentation;
   migrate builtin names and imports, and remove legacy spelling fallbacks.
5. Validate installed-library producer and consumer builds with the matching
   package rule.

### Source changes

Add explicit imports to async, SIMD, and timeout clients, including standard
source, examples, language fixtures, internal test source strings, and tutorials.
Change uses of the provided `AsyncOperation` name to imported `Operation`. Keep
source API declarations and their module paths unchanged. Source imports may use
local aliases where names conflict; ordinary programs keep their default names.

Builtin-name module and type-parameter reservation fixtures change to validate
shadowing. The existing `tests/language/text/lookup.cv` case that intentionally
bypasses a runtime parameter named `String` changes to use a type import alias;
a rejected case verifies that the unaliased qualifier is blocked.

Update the current language, compiler, toolchain, and craft documents only with
implementation. State module ownership, default availability, and operation
semantics separately. Explain provided modules within unified import resolution
and preserve the source-package model and its source-batch requirements.

## Validation

Validate observable selection and behavior:

- Source and provided modules accept the same single, list, alias, wildcard,
  conflict, and unused-import rules. Source declaration visibility is preserved;
  provided exports are public. A used explicit default-name import counts as
  used; a default-only reference has no import warning.
- Only the enumerated default names are available without imports. Adding a
  module export without extending that set leaves the new name import-only,
  including in modules that already own default names.
- Equivalent paths such as `std::core` and `carven::std.core` select the same
  provided module. Equivalent paths to standard source modules select the same
  source module. Duplicate imports, exact source/provided path collisions, and
  missing targets are diagnosed independently of written path spelling.
- Deeper standard source imports remain available under ordinary visibility.
  A provided module does not absorb descendants' exports: `std::simd` has no
  `ByteBlock`, and `std::time` has no `Duration`. Direct source imports select
  those declarations. A source directory alone supplies no module identity.
- Adding or removing an ordinary source export changes import selection without
  editing compiler metadata. Aliases preserve source callable and nominal
  identity, bodies, associated operations, and definition navigation.
- Distinct aliases of one export work. Duplicate explicit local names and
  declaration collisions fail. Explicit selections beat wildcards; ambiguous
  wildcards and wrong-kind bindings prevent default fallback. Native explicit
  selections can shadow defaults while existing Carven conflicts remain errors.
- Named type aliases preserve canonical equality and associated factories.
  Module declarations and type parameters can shadow named exports. Runtime
  locals affect expression qualifiers, not type annotations. Literal suffixes
  and inferred builtin results keep their canonical meanings.
- Missing imports reject uses of specialized provided names. Inference and
  language syntax still work without importing a result type's name. Imports in
  one source module do not supply imported names to another module.
- Renamed `when_all`, `when_any`, `first_successful`, and `timeout` preserve
  result shape, failure sets, access checks, and evaluation order. Renamed
  `wait_ready` and `close` retain required awaited-call handling and child
  lifetime behavior. Shadowing them selects the user entity.
- Renamed print, assertion, and test operations retain expected-signature
  adaptation, lazy direct-call message evaluation, static admission, source
  reporting, and active-test requirements.
- Time dependencies use original source nominal records under arbitrary caller
  aliases. An unused timeout import or a call to a shadowing entity does not
  require its source records. Checking the provided timeout contract diagnoses
  missing or incompatible records. Importing the operation never binds those
  record names in the caller.
- Byte and float helpers can be imported from their source modules under distinct
  aliases. Each name selects exactly one target before argument checking; no
  overload resolution or compiler-owned source renaming is involved.
- Type diagnostics distinguish user-defined and provided types with the same
  name, including an aliased provided `String`. Structural output and failure
  reports retain stable identities; aliases do not create new canonical types.
- Provided imports add no fictitious source modules or artifact owners. Source
  crafts and source-library manifests retain their analysis and linkage
  contracts. Explicit-input builds resolve only supplied source dependencies.
- A package-rule consumer combines default APIs, imported provided APIs, and
  directly imported standard source declarations. It links to the installed
  source producer without virtual-module implementation entries. Per-file native
  settings remain attached to original source paths.
- Compatible consumers share the existing native producer; differing native
  settings retain separate producers. An unchanged build skips generation;
  compiler or source content changes invalidate it, and artifact promotion
  preserves unchanged files and removes stale outputs.
- Compiler and workspace analysis agree on canonical module targets, export
  selection, and contract dependencies. Formatting preserves aliases; hover
  reports definition ownership; alias navigation and rename affect the local
  binding without fabricating editable provided declarations.

Build with `./xmakew build` before testing implementation changes. Run the
applicable test groups for each slice and the full suite for the coordinated
change. Check document-only changes for factual and design consistency.

## References

- [Design principles: language mechanisms and library composition](../docs/development/principles.md#language-mechanisms-and-library-composition)
- [Current module and visibility rules](../docs/language/modules.md)
- [Current named type rules](../docs/language/types.md)
- [Current pointer syntax](../docs/language/pointers.md)
- [Current printing and testing contracts](../docs/language/execution.md)
- [Current async contracts](../docs/language/async.md)
- [Compiler pipeline and publication](../docs/compiler/README.md)
- [Toolchain and source libraries](../docs/toolchain/cli.md)
- [Current source standard modules](../crafts/carven/std/README.md)
- [Source import graph](../src/frontend/program/program.cppm)
- [Analysis catalog](../src/semantic/analysis/catalog/catalog.cppm)
- [Named type resolution](../src/semantic/analysis/types/types.cpp)
- [Expression selection](../src/semantic/analysis/body/names.cpp)
- [Async operation selection](../src/semantic/analysis/async/async.cpp)
- [Source standard-record dependencies](../src/semantic/analysis/types/standard.cpp)
- [Source analysis observations](../src/semantic/analysis/source/source.cppm)
- [Source-library emission](../src/backend/generation/pipeline/generate.cpp)
- [Artifact manifest facts](../src/compiler/manifest.cpp)
- [Workspace dependency selection](../tools/workspace/src/analysis/analysis.cpp)
