# Pointer values

[Language](README.md)

This page defines pointer values, target access, local non-null checks, and
the lifetime responsibilities of native integrations.

`ptr<T>` stores an address that grants Read access to T; `ptr<&T>` grants Write
access. Both are ordinary nullable, copyable address values. `let` and `var`
control reassignment of the address slot. For example, `let p: ptr<&T>` cannot
be reassigned but can modify its target. A Read aggregate preserves the complete
types of its pointer fields, including their target permissions.

Targets use ordinary type resolution and can be Carven types, external types,
or nested pointers. Carven does not classify the underlying kind of a native
alias. A pointer does not contain or own its target, so pointer fields permit
recursive structures. `ptr<void>` can be stored and passed but cannot be
dereferenced. Native aliases and operations remain subject to C++ legality.

`addressof(place)` obtains `ptr<T>` for an addressable place with Read access;
`addressof(&place)` obtains `ptr<&T>` when that place permits Write access.
The builtin follows ordinary name lookup and shadowing. It evaluates its place
once. It does not accept a temporary, a
local constant, or Take access. The result is non-null at its
creation. Assignment to a live owner preserves its address; Take ends that
owner's identity, so later assignment does not revive old pointers. An address
returned from a Read array parameter may still refer to its caller's storage.
If the caller supplied a temporary array, that storage lasts through the
containing full expression; saving the address does not extend its lifetime.

`*p` accesses the target as a place; `p->member` is `(*p).member`. The address
must be available and locally proven non-null. Target access comes from the
pointer type, independently of the slot's binding access. `let value = *p`
performs ordinary value initialization; C++ checks native copyability. `&*p`
passes a writable target to an ordinary Write parameter. `&&*p` is rejected:
the target is not an owned Carven binding. Existing callable-borrow boundaries
remain in force; an indirect address does not establish a tracked borrow. Reading
an existing callable view copies its target description. Adapting an indirectly
read capturing closure into a new view still requires tracked backing storage.
An indirect call does not reconstruct capture relationships or establish the
lifetimes of objects reached through those captures.

For the same target type, `ptr<&T>` can become `ptr<T>` in a value context:
initialization, assignment, field or element construction, Read arguments,
constant initialization, and return. The reverse conversion is invalid. This
does not introduce array, aggregate, nested-target, or function-type covariance.
Each pointer layer has its own target/access pair. Copying an inner pointer
through an outer Read pointer retains the inner pointer's target permissions.
Context-free mixed pointer modes in array or branch results require a type
annotation. A type inferred from a sibling can type `nullptr`, but cannot
authorize narrowing inside nested arrays or branches. Inferred same-type copies
retain their full type.

Read pointer arguments save an address snapshot. Write parameters alias the
caller's slot and require the complete pointer type to match. Take parameters
also require an exact type, transfer the address value, and make its source
owner unavailable. Take does not zero other aliases or release the target.

`nullptr` needs a concrete pointer type context, including in an explicitly
typed `const`. There is no independent null type. Pointers support `==` and
`!=` with `nullptr` and with pointers to the same target type. They have no
implicit boolean conversion, ordering, arithmetic, direct indexing, integer
conversion. `addressof` is the Carven address-of operation for an addressable
place.

## Erasure, comparison, and display

Explicit `as ptr<void>` erases a Read target type. A Write pointer may erase to
`ptr<&void>` or narrow to `ptr<void>`; Read access cannot become Write access.
Erasure preserves the address and target lifetime without reading or owning the
target. Carven callable values are objects, so `ptr<fn() -> i32>` follows the
same object-pointer erasure rule. Conversion from `ptr<void>` to a typed
pointer is not supported. The compiler-known external `const char*` type of a C
string can erase to `ptr<void>`.

Pointer equality compares addresses. An address retains its storage origin and
selected subobject during semantic execution; those access coordinates alone do
not determine address equality. A standard-layout struct and its first field
have the same address, including nested first fields. Static execution
establishes that layout for Carven structs whose fields recursively consist of
numeric, bool, char, and pointer types or such structs. A comparison requiring
address information unavailable during execution produces a diagnostic.
Array-element access alone does not establish whether the native array wrapper
shares its first element's address. A text byte view likewise retains access to
its backing without establishing its address relationship to the owning String
object.

Erased pointers accept an empty format specification or `p`. Static execution
formats non-null pointers as `const@nonnull` and null pointers as `const@null`.
These fixed strings carry no object identity and can participate in ordinary
text computation and constant publication. Interpreted runtime uses
execution-local address labels such as `interp@object#N`; equal addresses have
the same label. Interpreted address display requires an established address
key; an unavailable key produces an execution diagnostic. Native runtime uses
the C++ pointer formatter. Formatting neither extends storage lifetime nor
establishes that a target can be dereferenced.

## Local non-null checks

Non-null checking is local to each function and closure. It tracks null,
non-null, and unknown facts for local names, fixed Carven field paths, and constant
array indices. Tests against `nullptr` refine branches,
including negation, short-circuit expressions, and early returns. Branch joins
keep only common facts. A bool helper, API success code, or test assertion is
not a proof. Functions and closures establish their own conditions.

Address copies and permission narrowing carry the current fact to the new
slot without creating a lasting equality relationship. Assignment replaces a
fact; Take removes the source fact. Write calls clear facts for overlapping
storage. A slot passed to a Write parameter may escape through that callee.
Later calls clear facts for such slots, Write parameters, and Write captures.
Loops clear potentially written facts before checking the condition and body;
the checker merges normal exits without computing cross-iteration relations.

Native projections, dynamic indices, and memory reached through a pointer do
not retain cross-expression facts. Save such a pointer in a local handle and
check that handle. An unproven dereference reports `CV-PTR-NONNULL`; the compiler
does not insert a runtime trap. Passing a nullable address to an API is allowed
without a dereference proof. Non-nullness never proves that a target is alive.
Static execution checks liveness when it dereferences a local
address, including byte addresses whose text backing has expired. Ordinary runtime
pointer operations do not add a general borrow or liveness checker; external
addresses retain their provider's lifetime contract.

## Native representation and responsibility

Read and Write target access lower to `const T*` and `T*`, composed by layer.
Read parameters pass the address by value; Write uses the corresponding pointer
reference. Generated code selects the dereference address before evaluating
later operands that could replace its slot. Saving or passing a pointer needs
only the target's declaration, so incomplete native types remain usable.
Forming a native target's C++ type expression still follows its own completeness
requirements. A pure Carven callable signature inside a pointer does not impose
that requirement on its parameter type: `struct Node { callback: ptr<fn(Node) -> void> }`
is valid, including when exported through a generated C++ interface.

Copying, passing, and taking a pointer perform no allocation, reference counting,
or automatic release. Owners and adapters implement the external resource protocol.
Native `T**` output protocols and buffer traversal remain in `#[cpp]`; `&p` passes a
pointer slot by reference and does not implicitly compute a `T**`. Declared
`import(cpp)` and `export(cpp)` signatures preserve these same pointer and access
rules.
