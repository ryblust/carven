# Constant execution for library storage

- **Status:** Exploration
- **Implementation:** Not started for the library-storage extension
- **Scope:** Admitted library storage operations and retained results
- **Depends on:** The generic and encapsulated declarations used by the selected container

## Current foundation

Constant functions, fixed-array and struct execution, and frozen constant slices
are implemented. String construction can grow during execution and freeze to
`str`; admitted arrays and records preserve their types. This proposal concerns
admission and retained results for future library containers and class operations.

## Library integration

Public containers belong to standard or user crafts. They need generic type
declarations, encapsulated storage, and defined access, lifetime, mutation, and
failure behavior. [Generics](generics.md) owns parameterized declarations; ordinary
[class encapsulation](../docs/semantics.md#ordinary-value-classes) is implemented.
Generic classes and admission of class operations to constant execution remain
required work for a container that uses them.

Semantic analysis resolves an operation's contract, constant execution implements
its admitted behavior, and lowering selects native support. Craft-specific
implementation stays with the craft; shared storage primitives belong to the
owning language/runtime facility. A native call alone does not provide constant
execution.

Intermediate values keep their source types. Each owner needs an explicit
retained-result contract preserving element eligibility and backing lifetimes.
The existing String-to-str freeze rule does not recursively replace fields
inside nominal containers.

## Follow-up: growable library containers

- **Design:** Exploration
- **Implementation:** Not started
- **Activation:** A concrete container consumer, with source support for generic
  encapsulated storage and the access and lifetime contracts its operations need

`Vector<T>` is a working name for a growable owning container in standard crafts.
Its public API, growth policy, and allocation-failure behavior remain open.
Constant execution admits its resolved operations under ordinary rules.

The proposed work is:

1. Define the ordinary container contract: construction, append, indexing, length,
   read-only views, copying and Take, including borrow invalidation and state on
   failure. Identify the generic and encapsulation facilities needed to express
   the owner; the first generic functions and structs alone do not establish them.
2. Supply the required storage operations through semantic analysis, lowering,
   and native support. Craft-specific implementation stays with the craft;
   shared language storage primitives belong to runtime. The same admitted
   operations need bounded constant-execution behavior.
3. Define the retained-result boundary. A completed sequence retained as static
   `[T]` is a candidate that reuses the current slice representation. Select the
   source operation or conversion explicitly; existing array freezing does not
   imply a conversion for arbitrary nominal owners. Element types and valid
   backing must survive freezing. An ordinary runtime view continues to borrow
   its owner.
4. Build a table whose entries are appended conditionally, so execution determines
   its length. Exercise empty and nonempty results, supported struct elements,
   cross-module static views, and ordinary runtime calls. Check invalid borrows,
   use after Take, unsupported retained contents, and resource limits. Inspect
   generated static data and runtime calls, then measure relevant workloads.

This experiment is complete when one source-defined container works through
ordinary execution and required constant execution under those contracts,
without a caller-supplied compile-time capacity. Fixed-array construction alone
does not satisfy that criterion. Reflection and dynamic polymorphism are not
required by this consumer; retaining an owning Vector value is a separate
result contract from retaining `[T]`.

## Integration with generics and capabilities

Constant execution uses resolved operations and canonical type identities.
Generic bodies remain checked at definition site; successful execution of one
instance cannot validate an unconstrained operation. A generic API's evaluation
or freezing requirements, if needed, belong to the static capability model.

Additional field and reference categories require eligible retained contents and
valid backing. Reflection, type computation, and declaration generation need
separate source contracts.

## Deferred operation consumers

Byte/scalar traversal for escaping and UTF conversion, explicit modular
arithmetic for hashing, and user-library constant-context diagnostics remain
separate operation candidates. Typed memory access requires concrete conversion
and lifetime contracts. Activate each candidate around a real algorithm; define
ordinary and constant behavior and validate results, failures, ownership, and
resource limits. Type/field queries and heterogeneous expansion require their
own generic or reflection contracts.
