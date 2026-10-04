# Constant execution for library storage

- **Status:** Exploration
- **Implementation:** Not started for the library-storage extension
- **Scope:** Admitted library storage operations and retained results
- **Depends on:** The generic and encapsulated declarations used by the selected container

## Current foundation

Constant functions, fixed-array and struct execution, and frozen constant slices
are implemented, as are integer wrapping arithmetic and text byte views and
iteration during constant execution. String construction can grow and freeze to
`str`; admitted arrays and records preserve their types. This proposal concerns
admission and retained results for future library containers and class operations.

## Library integration

Public containers belong to standard or user crafts. They need generic type
declarations, encapsulated storage, and defined access, lifetime, mutation, and
failure behavior. Ordinary class encapsulation is implemented.
Generic classes remain unimplemented. A selected container also needs its
construction, access, mutation, calls, and cleanup admitted to constant execution.

Construction-destination, partial-initialization, and native lifetime contracts
establish the ordinary behavior to admit into constant execution. Evaluate storage
candidates for both runtime and constant execution; define admission from the
selected source behavior.

Semantic analysis resolves an operation's contract, constant execution implements
its admitted behavior, and lowering selects native support. Craft-specific
implementation stays with the craft; shared storage primitives belong to the
owning language/runtime facility.

Intermediate values keep their source types. Each owner needs an explicit
retained-result contract preserving element eligibility and backing lifetimes.
The existing String-to-str freeze rule does not recursively replace fields
inside nominal containers.

For each storage candidate:

- Define constant-execution behavior for empty, partially initialized, live, and
  consumed destinations, including identity and cleanup.
- Validate reads and completion assertions against modeled initialization state.
  Keep host uninitialized memory inaccessible to evaluation.
- Test admission and unsupported-operation diagnostics.
- Decide whether raw addresses or unfinished storage are eligible retained results.
  Require eligible types and valid backing for completed retained contents.

The evaluator representation remains open.

## Follow-up: growable library containers

- **Design:** Exploration
- **Implementation:** Not started
- **Activation:** A concrete container consumer, with source support for generic
  encapsulated storage and the access and lifetime contracts its operations need

`Vector<T>` is a working name for a growable owning container in standard crafts.
Its public API, growth policy, and allocation-failure behavior remain open.
The design requires constant execution of its resolved operations under ordinary
rules.

The proposed work is:

1. Define the ordinary container contract: construction, append, indexing, length,
   read-only views, copying and Take, including borrow invalidation and state on
   failure. Identify the generic and encapsulation facilities needed to express
   the owner.
2. Supply the required storage operations through semantic analysis, lowering,
   and native support. Craft-specific implementation stays with the craft;
   shared language storage primitives belong to runtime. The same admitted
   operations need bounded constant-execution behavior.
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

This experiment is complete when one source-defined container works through
ordinary execution and required constant execution under those contracts,
without a caller-supplied compile-time capacity. Define separate result contracts
for retaining an owning Vector value and retaining `[T]`.

## Integration with generics and capabilities

Constant execution uses resolved operations and canonical type identities.
Generic bodies remain checked at definition site; successful execution of one
instance cannot validate an unconstrained operation. A generic API's evaluation
or freezing requirements, if needed, belong to the static capability model.

Additional field and reference categories require eligible retained contents and
valid backing. Reflection, type computation, and declaration generation need
separate source contracts.

## Deferred operation consumers

Constant scalar traversal through `.chars`, any additional execution admission
needed by UTF conversion, and user-library constant-context diagnostics remain
operation candidates.

Typed memory access requires concrete conversion and lifetime contracts. Activate
each candidate around a real algorithm; define ordinary and constant behavior and
validate results, failures, ownership, and resource limits. Type/field queries and
heterogeneous expansion require their own generic or reflection contracts.
