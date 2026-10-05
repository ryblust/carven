# C++ artifacts

Artifact planning selects names, interfaces, and definition schedules. Target-unit
construction owns syntax, dependencies, verification, and emission.

## Names and interfaces

`TargetNamePlan` owns linkage-domain namespaces, module names, nominal names,
callable and closure names, enum payload names, generated test names, query aliases,
constant backing names, and nominal display helper names. Function references
use their declaration's `CallableID`. `generation.names` owns identifier encoding
and naming roles; `TargetNameAllocator` reserves callable-local names and allocates
locals, temporaries, and labels. The plan owns the immutable module reservations
borrowed by these local allocators. `ModuleLowering::field_identifier` derives
nominal member names from their semantic identity and planned enclosing name.
Source encoding is injective: reserved C++ names and source spellings in its escape
domain are encoded. The plan also owns encoded public namespace and function names
shared by API headers and export façades. Artifact paths retain canonical source names.
Closure types are numbered by their discovery order within the owner module, so other
modules cannot renumber them.

Semantic visibility and C++ definition requirements determine interface
artifacts. Declaration-only dependencies use forward declarations. Complete
requirements form interface edges; strongly connected components share an
interface. Function declaration return types, including Outcome and arrays,
require only declarations of their component types. Object storage and Read
traits require complete definitions. Body-only calls do not merge interfaces.
Private nominals needed by an interface layout receive definitions in their
owner's interface; declaration-only references receive forward declarations.
This placement does not change Carven source visibility.

Schedules own interface definitions, C++ façades, private nominal and closure
ordering, and selected tests. Module lowering starts with externally visible
functions, process and C++ export entries, interface closures, and enabled runtime
tests. Realizing native function references and closure types requests their local
definitions through `ModuleLowering`. Each callable is lowered once; its references
can request further definitions. Unrequested private functions and closures have
no native declarations or definitions. Static execution retains its SemIR bodies
independently of this native selection.

Lowering also records providers of emitted names and types. These transient
provider sets are consumed into include directives.

### Staged bodies

A function with static parameters contributes its possible runtime dependencies
to its owner's surface. Semantic publication records the body's types and
callables; artifact planning reads these facts with the signature. Types and
closures follow the ordinary nominal and closure rules, and each same-module
function reached joins the surface. A reached private function keeps external
linkage and is declared in its owner's interface; a reached staged function
contributes its published surface facts. Semantic collection covers every runtime
arm and excludes static conditions, ranges, initializers, and arguments.
Everything else private stays internal to its implementation. The selection
depends only on the owner's source, never on its callers; the backend does not
traverse source templates.

Each artifact collects requested definitions through one work queue. Every module
has one artifact-owned lowering context, including the artifact's own module.
An instance is an `inline` function in its owner's module namespace. Definitions
follow emitted call edges in callee-first order; recursion cycles introduce the
necessary forward declarations. Ordinary private callables remain in an anonymous
namespace unless the interface surface requires external linkage.

Instance names append readable scalar static values, or a digest of typed
canonical content for composite or long values. Name planning compares complete
content and assigns distinct spellings when preferred names coincide; digests do
not identify cached semantic values. Content keys encode a local node graph with
bounded-stack traversal; repeated dependencies reuse their node definitions.
Slice backing arrays are
module-owned `inline constexpr` objects named by content. Their initializers use
the same module context as their references. Structural display uses runtime
scalar, sequence, and range emitters, plus a content-named helper per nominal
type. Module lowering caches emitter types separately from expressions referring
to their `stateless_value` instances. Helper records precede their function
definitions so instance constraints see complete types. Display calls pass depth
by value; the runtime writer owns the output budget.

Each runtime test becomes a function. Static tests have already executed during
analysis and receive no target function name or runner entry. Module runners call
tests in source order; the runner header calls modules in canonical order. The default entry calls
that same runner. Default test-entry mode disables the program entry wrapper in
the module schedule; external-runner mode retains it for the consuming build.
Explicit C++ fragments preserve their bytes and source order.

## External names and operations

Semantic name resolution expands explicit C++ selections into globally rooted
paths while retaining the provider module's header environment. External names
lower through one shared path for values, named types, and type queries. Global
lookup produces a root-qualified C++ path. Opened namespace lookup prefixes the
path with the context module's generated namespace. Both record a declaration
environment requirement; opened namespace lookup also requires the using environment.

Environment requirements are independent of semantic lookup modes and merge
idempotently, with using requirements including declarations. Lowering owns
their materialization into ordered includes and using declarations.
Nested types and result queries carry their environments into consuming
artifacts; dependencies are not inferred by matching symbols to headers.

## External query aliases

The name plan names external query aliases by complete semantic content. Each
module context caches their emitted target types by canonical query TypeID. Alias
definitions use globally qualified type references and precede their dependent declarations,
after required nominal definitions. Implementation support is placed in the same
module namespace before callable definitions. References retain the module's
qualified alias name across translation units; exact references and
cv-qualification remain in the alias definition. Local `decltype` expressions
retain their local scope.

The [external result type contract](representation.md#external-result-types)
defines the exact query type and normalization before alias placement.

## Target syntax and emission

Generated references to standard-library, runtime, and resolved global symbols
use leading `::` so enclosing declarations cannot redirect lookup. Namespace
definitions, local names, and member access retain their own syntax. Native
source fragments retain author-specified lookup.

Expressions, statements, and items are move-only recursive values.
`TargetBlockStmt` always denotes an actual C++ block; plain sequences are
composed before publication. Only types are interned. A construction-time index
selects candidates for structural equality; type storage and IDs retain insertion
order. Target type IDs belong to one unit. Source attribution and function forms
use exact variants.

`TargetTemplateNameExpr` represents a template-id naming a function or variable,
including an explicit empty argument list. `TargetCallExpr` contains its callee
and runtime arguments. Function-template calls compose these nodes; variable
templates use the name expression directly. Traversal, verification, dependency
collection, and rendering consume the same argument representation. Entity
classification reads the primary expression through `template_primary_expression`.

Type construction accepts only children already present in the same unit;
append-only insertion establishes acyclicity. Finishing validates occurrence type
references and local control transfers.
Target verification does not recheck Carven evaluation order, object lifetime
semantics, or C++ overload and constructor feasibility. Realization preserves
those input contracts through operand use, frame ownership, result destinations
and failure dispatch, with local invariants and generated-program tests.
Owned syntax nodes define occurrence identity. `TreeValue` uses an explicit work
stack to clear descendants, including partially moved trees, statement bodies,
and namespace items. Child cleanup also bounds the stack depth of variant
replacement. Traversal preserves scope order through enter/leave events.
Dependency collection visits the finished tree and referenced types to derive
the required standard and runtime headers.

The renderer builds completed layout tables in postorder for expressions,
statements, items and the type dependency DAG, including decltype expressions.
Consumers apply precedence to completed expression layouts. These tables live
for emission. The renderer serializes nodes, directives, source mapping, and
whitespace. Layout alternatives inspect pending commands up to the next line
boundary; command links and explicit choice frames share the remaining work. Binary rendering
preserves the expression tree using C++ precedence and associativity. Nested
comparisons on either side receive explicit parentheses to make their grouping
visible. An `else` body that contains only a generated conditional renders as
`else if`; realization composes two-way conditionals. Semantic inference and
target syntax construction finish before rendering. Artifact collection checks
logical paths, uniqueness, and prefix safety.

Continuation indentation is bounded by half the configured line width, keeping
whitespace proportional to syntax size. Child-before-parent width summaries
avoid repeated measurements of single-line preferred layouts. Width addition
checks overflow; line breaks, raw bytes, source directives, and indentation
changes retain their layout operations.
