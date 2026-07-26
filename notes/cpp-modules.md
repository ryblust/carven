# C++ Modules: From Textual Inclusion to Semantic Ownership

C++ modules are easy to misread as headers with a different spelling. That
comparison explains `import`, but it hides the deeper change. A header answers
where text is copied. A named module answers who owns a declaration, which
translation units may use it, and which semantic dependencies must be present
when it is used.

That shift—from textual inclusion to semantic ownership—is the useful mental
model. Faster builds can follow from it, but speed is not the language rule and
is never automatic.

## Semantic ownership

With headers, the preprocessor copies declarations into every translation unit
that includes them. The compiler repeatedly reconstructs meaning from that
text. Include order, macro state, and conditional compilation can change what
the copied text means.

A module unit is still a translation unit, but its declarations are attached
either to the global module or to a named module. Other translation units gain
access through `import`. A partition such as `graph:node` is not a nested
module; it is one unit of the named module `graph`.

Compilers normally serialize an importable unit into a binary module interface
(BMI), also called a CMI or IFC by some toolchains. The name and format of that
artifact are implementation details. Two consequences matter more:

- `import graph;` names one module, but the compiler may need several BMIs from
  its interface dependency graph.
- An interface unit can also emit executable definitions and initialization.
  Its object file usually still has to be linked; a BMI is not an object file.

The module graph therefore carries several kinds of information that a textual
include graph does not express cleanly: declaration ownership, importability,
semantic reachability, and code-generation ownership.

## Module unit kinds

Two binary choices—interface or implementation, whole module or partition—give
the four forms worth memorizing.

| | Whole module | Partition |
| --- | --- | --- |
| Interface | `export module M;` | `export module M:P;` |
| Implementation | `module M;` | `module M:P;` |

The primary interface, `export module M;`, is what an external `import M;`
names. Every named module has exactly one. A larger public surface can be
assembled from interface partitions:

```cpp
export module library;

export import :containers;
export import :algorithms;
```

An interface partition is importable only inside its own named module. Its
exported declarations reach external importers only when the primary interface
re-exports it, directly or through another interface partition. Every
interface partition must be included in that re-export graph.

A module implementation unit, `module M;`, implicitly imports the primary
interface of `M`. That is convenient for a small module, but it also gives the
unit a coarse dependency on the whole public interface.

An implementation partition, `module M:P;`, does not implicitly import the
primary interface. It is importable by units of the same named module, cannot
be exported, and makes fine-grained internal dependencies possible. It is often
the better home for shared implementation declarations, private templates,
out-of-line definitions, and white-box tests.

Neither filenames nor punctuation add another semantic layer. `.cppm`, `.ixx`,
`.mpp`, and `.cpp` are build conventions. Dots in `M:parser.expr.impl` do not
create hierarchy. Names such as `api`, `fwd`, `internal`, and `impl` communicate
intent to humans; the module declaration remains the authority.

## Export and reachability

Four related terms answer four different questions.

| Term | The question it answers |
| --- | --- |
| Export | May a translation unit outside this named module use the declaration? |
| Visibility | Can name lookup find the declaration here? |
| Reachability | Which declarations may contribute semantic properties here? |
| Linkage | Can declarations in different places denote the same entity? |

`export` is an external interface boundary, not a universal privacy marker. A
non-exported declaration can be shared by units of the same named module when
its owning partition is imported. Merely belonging to the same named module
does not make every declaration visible everywhere.

Names attached to a named module may have module linkage. This allows
declarations in different units of that module to denote the same entity
without publishing the entity to external importers.

That does not make module scope the right home for every helper. If only one
translation unit needs a function or object, keep it translation-unit-local.
An anonymous namespace or `static` says something more precise than merely
leaving the declaration unexported.

## Dependency ownership

Reachability is where a modules build can appear healthy while the source graph
is semantically fragile. A directly imported unit is necessarily reachable,
and interface dependencies propagate according to the language rules. The
standard permits other units to be reachable in unspecified circumstances, so
successfully finding a transitive BMI does not make every transitive
implementation dependency portable.

Consider three implementation partitions:

```cpp
// model.cppm
module library:model;

struct Model {
    int value;
};

// internal.cppm
module library:internal;

import :model;

void consume(const Model&);

// algorithm.cpp
module library:algorithm.impl;

import :internal;

int read(const Model& model) {
    return model.value;
}
```

`algorithm.cpp` uses the complete definition of `Model`, so it should directly
`import :model;`. Depending on that definition to arrive through the
implementation partition `:internal` relies on unspecified reachability. A
compiler accepting the program does not turn the hidden dependency into a
portable one.

A durable rule follows:

> Import the owning unit directly whenever the current translation unit uses a
> declaration's semantic properties.

Complete class definitions, templates, default arguments, inline definitions,
and constant-evaluated definitions make this especially important. The build
graph and the semantic dependency graph should tell the same story.

An interface unit may syntactically import an implementation partition of the
same module, but this is usually a warning about dependency direction. If an
external importer needs that material to understand the interface, it belongs
on an interface path. If the importer does not need it, move the dependent
definition out of the interface.

## Cyclic dependencies

Modules do not make cyclic imports buildable. The import graph still needs an
acyclic compilation order. They do, however, provide better tools for saying
which declarations share ownership.

A declaration-only partition can break a cycle when incomplete types are
enough:

```cpp
// fwd.cppm
module graph:fwd;

struct Node;
struct Edge;

// node.cppm
module graph:node;

import :fwd;

struct Node {
    Edge* outgoing;
};

// edge.cppm
module graph:edge;

import :fwd;

struct Edge {
    Node* destination;
};
```

The declarations and definitions are all attached to `graph`, so matching
declarations can denote the same entities. A declaration attached to one named
module cannot be completed by a definition attached to a different named
module merely because their spelling matches.

An `internal` partition solves a different problem. It replaces the role of a
private header shared by implementation units:

```cpp
// internal.cppm
module parser:internal;

class Parser {
public:
    void parse_expression();
    void parse_statement();
};

// expression.cpp
module parser:expression.impl;

import :internal;

void Parser::parse_expression() {
    // ...
}
```

`fwd` should remain a minimal ownership and cycle-breaking layer. `internal`
or `detail` is a collaboration boundary and may contain complete definitions
and shared templates. Keeping the names distinct makes the dependency graph
easier to read even though C++ assigns no special meaning to either suffix.

## Header boundaries

A module unit can begin with a global module fragment (GMF):

```cpp
module;

#include <legacy/header.hpp>

export module adapter;
```

Declarations introduced there remain attached to the global module. This is a
natural boundary for legacy headers and configuration that still depends on
the preprocessor.

It is possible to textually include a header after the module declaration, but
the declarations it produces are generally attached to the current named
module. That can change entity identity and create conflicts with the same
header included elsewhere. Moving an include below `export module` is therefore
an ownership and ABI decision, not harmless formatting.

Named-module imports do not transport macros. If consumers must observe a
macro, the library still needs a textual surface or a different configuration
mechanism. Header units are a separate facility and can expose macros, but
their scanning and toolchain behavior should not be confused with named-module
semantics.

C++23 standardizes the library modules `std` and `std.compat`. They are useful
when the compiler, standard library, and build system support them together;
they are not a prerequisite for designing a C++20 named-module graph.

## Interface stability and incremental builds

The cleanest module boundary keeps declarations that consumers need in
interface units and moves replaceable definitions into implementation units:

```cpp
// library.cppm
export module library;

export import :api;

// api.cppm
export module library:api;

export int calculate(int value);

// calculate.cpp
module library:calculate.impl;

import :api;

int calculate(int value) {
    return value * 2;
}
```

Changing the body of `calculate` no longer changes the interface source. The
dependency graph now permits a local rebuild, although the exact BMI hashing
and invalidation behavior remains a compiler and build-system choice.

Templates, `constexpr`, and `consteval` move the tradeoff in the other
direction. Their definitions must be reachable where instantiation or constant
evaluation occurs, so changing them is genuinely an interface dependency
change.

A member function defined inside a class in a named module is not implicitly
`inline` solely because it appears inside the class definition. Explicit
`inline`, template rules, and constant-evaluation requirements still apply.
Link-time optimization can provide cross-translation-unit optimization without
making every implementation body part of the source interface.

A private module fragment, `module :private;`, provides a single-file boundary.
A module using one cannot also be spread over other module units, so it is an
alternative to a partitioned design rather than an internal partition with a
different spelling.

## Scalable module topology

C++ does not prescribe one named module per file, directory, repository, or
library. A useful named-module boundary is normally a coherent ownership,
distribution, or ABI boundary. Partitions then split that boundary into units
small enough to build and reason about.

```text
library.cppm          primary interface and stable re-export list
api.cppm              public interface partition
fwd.cppm              minimal same-module forward declarations
internal.cppm         shared non-public declarations and templates
feature.cpp           out-of-line definitions in an implementation partition
feature_test.cpp      white-box tests in an implementation partition
```

Converting every former header into a separate named module is rarely a neutral
translation. It turns a library boundary into many file-level ownership
boundaries and makes tightly coupled types harder to organize. The opposite
extreme—one named module for independently distributed libraries—creates
coupling that the source architecture does not justify.

A small primary interface, explicit re-exports, direct owning imports, and
translation-unit-local helpers make the graph communicate the architecture
rather than merely satisfy the compiler.

## Testing module boundaries

A black-box test imports the primary interface exactly like an external user.
A white-box test can instead be an implementation partition of the module
under test, gaining access to shared non-exported declarations without adding
friend hooks or widening the public API.

Shared non-macro white-box support can also live in an implementation
partition. Test framework macros still need a textual path into each test unit,
normally through its GMF, because named-module imports do not carry them.

`main` must not be attached to a named module. A module unit can use an
`extern "C++"` language-linkage block to attach it to the global module:

```cpp
module application:main;

extern "C++" int main() {
    return 0;
}

```

This corner of the language is a good reminder that source-file placement and
module attachment are different concepts.

## Build-system responsibilities

A modules-aware build first discovers declarations and imports, constructs a
dependency graph, compiles importable units before their importers, and finally
links the object output of every required unit.

```text
scan declarations and imports
             |
             v
construct the module DAG
             |
             v
build importable units before importers
             |
             v
compile objects and link the program
```

Dependency-scanning formats, BMI paths, reduced BMIs, compiler command lines,
editor integration, and construction of standard-library modules live in the
toolchain layer. They are operationally essential but are not C++ language
semantics.

BMIs are usually coupled to compiler version, target, standard library,
language mode, macro configuration, and other flags. Source module units are a
portable distribution surface in a way that arbitrary prebuilt BMIs are not,
unless producer and consumer deliberately share a toolchain contract.

This separation helps diagnose claims about modules. “The declaration is not
reachable” is a language question. “The scanner did not discover the import”
is a build question. “Importers rebuilt after an implementation-only edit” is
an invalidation-policy question. Treating them as the same problem makes all
three harder to solve.

## Sources and further reading

The language model is specified by these C++ working-draft sections:
[`[module.unit]`](https://eel.is/c++draft/module.unit),
[`[module.import]`](https://eel.is/c++draft/module.import),
[`[module.reach]`](https://eel.is/c++draft/module.reach),
[`[module.global.frag]`](https://eel.is/c++draft/module.global.frag), and
[`[basic.link]`](https://eel.is/c++draft/basic.link).

The following sources connect those rules to real toolchains and codebases:

- Egor (holyblackcat), [*The compilation procedure for C++20
  modules*](https://holyblackcat.github.io/blog/2026/03/09/compiling-modules.html)
- Chuanqi Xu, [*C++20 Modules: Practical Insights, Status and
  TODOs*](https://chuanqixu9.github.io/c++/2025/08/14/C++20-Modules.en.html)
- Chuanqi Xu, [*C++20 Modules: Best Practices from a User's
  Perspective*](https://chuanqixu9.github.io/c++/2025/12/30/C++20-Modules-Best-Practices.en.html)
- Clang, [*Standard C++
  Modules*](https://clang.llvm.org/docs/StandardCPlusPlusModules.html)
- WG21 [P1689R5, *Format for describing dependencies of source
  files*](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p1689r5.html)
- [infiniflow/infinity](https://github.com/infiniflow/infinity), a large
  modules-native implementation specimen

Toolchain reports and working repositories demonstrate feasibility and expose
practical constraints. The standard remains the authority for language
semantics, and version-sensitive observations should be rechecked against the
compiler and build system actually in use.
