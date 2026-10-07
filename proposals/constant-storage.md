# Constant storage and retained results

- **Status:** Exploration
- **Implementation:** Not started for the extensions described here
- **Scope:** Storage-operation admission, retained library values and sequences, and retained reference graphs

## Summary

This proposal explores which storage operations may execute in constant contexts
and which completed results may become published static data. Library containers
and retained reference graphs have separate admission and retention contracts.

## Current foundation

Constant functions, fixed-array and struct execution, and frozen constant slices
are implemented, as are integer wrapping arithmetic and text byte views and
iteration during constant execution. String construction can grow and freeze to
`str`; admitted arrays and records preserve their types. Class operations marked
`const fn` execute inside constant-function bodies, constant blocks, and static
tests under ordinary access rules. Direct initializer and extent calls retain the
[class-operation admission limits](../docs/language/aggregates.md#ordinary-value-classes).

This proposal concerns additional storage-operation admission and retained results
for library containers and reference graphs. Non-null local pointers cannot
currently become published constants. Temporary execution storage, extraction of
a completed result, and backend static storage remain distinct owners and boundaries.

`Sequence<T>` already provides runtime owning storage, growth, indexing, iteration,
copying, and Take for admitted element types. It has no constant or interpreted
execution support, contiguous slice view, or retained-result contract. Constant
sequence construction and a source-defined container are separate extensions.

## Library integration

A source-defined container belongs to a standard or user craft. Its operations
determine the generic declarations, encapsulated storage, access, lifetime,
mutation, and failure contracts it needs. Ordinary class encapsulation and
parameterized class fields are implemented; parameterized operations remain
unsupported. Constant execution needs admission for the selected construction,
access, mutation, calls, and cleanup.

A source-defined storage implementation may need construction-destination,
partial-initialization, and native lifetime contracts. Existing Sequence storage
already supplies its runtime behavior. Evaluate any new storage operations for
both runtime and constant execution; define admission from the selected source
behavior.

Semantic analysis resolves an operation's contract, constant execution implements
its admitted behavior, and lowering selects native support. Craft-specific
implementation stays with the craft; shared storage primitives belong to the
owning language/runtime facility.

Intermediate values keep their source types. Each owner needs an explicit
retained-result contract preserving element eligibility and backing lifetimes.
The existing String-to-str freeze rule applies to String results; nested fields
preserve their source types.

Executing an operation does not make all its intermediate storage eligible for
retention. A borrowed text constructor must preserve the original bytes, range,
and lifetime rather than copy the bytes to evade its borrow contract. For class
operations, construction, calls, and result retention each need admission.

For each storage candidate:

- Define constant-execution behavior for empty, partially initialized, live, and
  consumed destinations, including identity and cleanup.
- Validate reads and completion assertions against modeled initialization state.
  Keep host uninitialized memory inaccessible to evaluation.
- Test admission and unsupported-operation diagnostics.
- Decide whether raw addresses or unfinished storage are eligible retained results.
  Require eligible types and valid backing for completed retained contents.

The evaluator representation remains open.

## Growable library containers

- **Design:** Exploration
- **Implementation:** Not started
- **Activation:** A consumer needs constant sequence construction, retained data,
  or a source-defined storage policy beyond the runtime Sequence contract

Choose whether the consumer needs static admission for existing `Sequence<T>` or
a source-defined container with different storage operations. Static admission
must preserve Sequence's ordinary element, mutation, and failure rules; it does
not itself require generic class operations or expose native storage primitives.
A distinct craft container must justify its API, growth policy, and failure
contract. No additional public container name is selected.

The proposed work is:

1. Select the existing Sequence contract or define the additional container's
   construction, append, indexing, length, copying, Take, and optional view
   operations, including borrow invalidation and state on failure. A source-defined
   owner identifies the generic and encapsulation facilities its operations need.
2. Give the selected operations bounded constant-execution behavior. Reuse existing
   runtime semantics and add analysis, lowering, or native support only for new
   source operations. Craft-specific storage stays with the craft; shared language
   storage primitives belong to runtime.
3. Define the retained-result boundary. A completed sequence retained as static
   `[T]` is a candidate that reuses the current slice representation. Select the
   source operation or conversion explicitly. Element types and valid
   backing must survive freezing. An ordinary runtime view continues to borrow
   its owner.
4. Build a table whose entries are appended conditionally, so execution determines
   its length. Exercise empty and nonempty results, supported struct elements,
   cross-module static views, and ordinary runtime calls. Check invalid borrows,
   use after Take, unsupported retained contents, and resource limits. Inspect
   generated static data and runtime calls, then measure relevant workloads.

The selected experiment is complete when its container works through ordinary
and required constant execution under those contracts, without a caller-supplied
compile-time capacity. Define separate result contracts for retaining an owning
container value and retaining `[T]`.
Retaining the sequence preserves element type, order, and backing guarantees;
capacity and allocation history are not part of that result.

## Compact retained collections

An explicit compact builder/reader is a candidate consumer of the
[frozen collection layout experiment](data-representation.md#frozen-compact-collections).
This section owns retention and execution requirements; that experiment owns
physical encoding, projections, and native realization. No compact-container
syntax or retained owning type is selected yet.

Admit logical construction, typed failure handling, and cleanup under the shared
executor before selecting a target representation. Source `const async`
composition may produce an eligible completed value under its existing rules;
pending operations and scheduler state cannot become retained collection data.
SIMD-assisted construction retains logical lane results, not native register
bytes. Source value and borrow validity remain independent of layout selection.

The first experiment should use acyclic records with supported scalar/text
contents and an explicit immutable reader. Keep a source-to-retained-type
conversion visible when the result has different access or mutation operations;
ordinary nested fields must not silently change type during freezing. Shared or
cyclic graphs still require the separate graph contract below.

Retention validates the completed reachable values and backing. Layout then
assigns storage and any offsets, and native realization must establish legal
C++20 static data and reader operations. Execution object coordinates and host
addresses are never emitted as offsets or pointers. An encoding requiring
runtime pointer-to-integer initialization cannot silently turn a static result
into a dynamic initializer or give constants a different ABI from runtime values
of the same type.

Compare a compile-time-built lookup table or record collection with its runtime
builder using the same reader and expected values. Check empty data, failure
during construction, escaping backing, offset overflow, cross-module storage,
and unsupported native resources. Measure static bytes and native compile cost
alongside access cost; compact retained storage is not a persistent file-format
contract.

## Retained reference graphs

### DEFER-01 — Shared or cyclic static graphs

- **Design:** Deferred
- **Implementation:** Not started
- **Reason deferred:** Retained alias and cycle identity and permission policy
  remains open.
- **Reactivation condition:** A static-data consumer needs retained aliases or cycles and can state the intended root, relationships, and access permissions.

Develop the graph's observable contract independently of a growable container.
A candidate retains the root-reachable closure of eligible objects, preserving
shared node identity and valid subobject projections. Define which targets can be
promoted, whether cycles are admitted, and whether retained references must be
read-only. Check external resources and escaping mutable aliases. Static promotion
cannot repair a pointer that was already dangling during execution.

The existing recursive constant representation cannot express cycles by adding
only a pointer alternative. A graph candidate needs node identities, staged
construction of nodes and edges, and publication checks for a closed graph with
valid typed references. Analysis remains responsible for canonical publication;
the executor does not write published constant tables. Backend symbol references
and relocations must denote target storage and must never contain compiler host
addresses or execution-local coordinates.

Observable graph relationships require a source identity and address-observation
policy. The execution model owns that policy; graph retention consumes it.

Validation needs shared and separate equal-valued nodes, valid projections,
cycles if admitted, closed and escaping roots, dangling targets, Write-permission
rejection or the selected alternative policy, and resource limits. Check graph
publication invariants directly and compile and execute C++20 target data across
modules. Freezing eligibility does not establish target static-initialization
eligibility: inspect every reconstruction path, including payload-enum factories,
and do not silently add runtime initialization. Observe promised alias relations
and backing lifetimes in the generated program.

## Integration with generics and capabilities

Constant execution uses resolved operations and canonical type identities.
Generic bodies remain checked at definition site; successful execution of one
instance cannot validate an unconstrained operation. A generic API's evaluation
or freezing requirements, if needed, belong to the static capability model.

Additional field and reference categories require eligible retained contents and
valid backing.

## Deferred operation consumers

Constant scalar traversal through `.chars`, any additional execution admission
needed by UTF conversion, and user-library constant-context diagnostics remain
operation candidates.

Typed memory access requires concrete conversion and lifetime contracts. Activate
each candidate around a real algorithm; define ordinary and constant behavior and
validate results, failures, ownership, and resource limits.

## References

- [Uninitialized storage](uninitialized-storage.md): construction destinations,
  partial initialization, and native lifetime rules for storage operations.
- [Semantic execution](semantic-execution.md): observable identity and address policy
  for retained graphs.
- [Generic extensions](generic-extensions.md): definition-site checking and static capabilities for
  generic container operations.
