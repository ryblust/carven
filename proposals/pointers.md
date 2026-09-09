# Pointer values

- **Status:** Implemented
- **Scope:** Nullable addresses, indirect access, and C++ interoperation

## Model

A `ptr` is an ordinary, non-owning address value. Its type records the target
and the access available through that address. Binding access controls the
storage containing the address.

| Form | Contract |
| --- | --- |
| `ptr<T>` | Read access to the target. |
| `ptr<&T>` | Read and Write access to the target. |
| `let p` | The address slot cannot be reassigned. |
| `var p` | The address slot can be reassigned. |
| Read parameter `p` | An address snapshot, preserving target access. |
| Write parameter `&p` | Access to the caller's address slot. |
| Take parameter `&&p` | Transfer of the address value; the source owner becomes unavailable. |

Targets use ordinary type resolution: Carven types, external types, and nested
ptr types follow the same rules. Carven does not classify the underlying type
of a native alias. A pointer does not contain its target, so pointer fields can
form recursive structures. `ptr<void>` can be stored and passed, but cannot be
dereferenced as an object.

## Access and conversion

`*p` accesses the target as a place. `p->member` abbreviates `(*p).member` and
uses the same checks. The address must be available and locally proven non-null.
Value initialization from `*p` follows the target's ordinary copy rules. `&*p`
requires Write target access. `&&*p` and `ptr<&&T>` are invalid.

```carven
fn inspect(p: ptr<i32>) -> i32 {
    if p == nullptr { return 0; }
    return *p;
}

fn replace(&slot: ptr<&i32>, next: ptr<&i32>) {
    slot = next;
}

fn increment(p: ptr<&i32>) {
    if p != nullptr { *p += 1; }
}
```

For an identical target type, `ptr<&T>` can convert to `ptr<T>` in value
initialization, assignment, field or element construction, Read arguments,
constant initialization, and return. The reverse conversion is invalid. Write
and Take parameters require the complete ptr type to match.

Copies preserve the complete type. Reading a structure, array, enum payload,
or value capture preserves the target access of its ptr values. Each nested
ptr layer follows this rule independently: reading an inner address through
an outer Read pointer preserves the inner address's target access. Pointer
conversion does not provide aggregate, array, nested-target, or callable
covariance. Mixed pointer modes in context-free array or branch results need
an explicit expected type.

Take transfers only the address value. It does not release the target, clear
other aliases, or prove unique ownership. Existing rules require a complete
owner for Take. Pointer boundaries stop owned-content and tracked-borrow
queries; indirect access does not establish a new tracked lifetime.

## Null values and local checks

`nullptr` requires a concrete ptr type context, including in an explicitly
typed `const`. Pointers support `==` and `!=` with `nullptr` and with pointers
to an identical target type. Null addresses may be passed to APIs without a
dereference proof.

The checker traverses each function and closure's structured SemIR separately:

- Local names, fixed Carven field paths, and constant array indices retain
  null, non-null, or unknown facts.
- Comparisons with `nullptr`, negation, short-circuit conditions, and early
  exits refine facts. Branch joins retain common facts.
- Copies and permission narrowing carry the current fact to the destination
  without retaining equality between slots. Assignment replaces facts; Take
  removes the source fact.
- Write calls invalidate overlapping storage. Passed Write storage may escape
  through a callee; later calls invalidate its facts. Write parameters and
  Write captures are also treated as potentially aliased storage.
- Loops first clear potentially written facts, then analyze the condition and
  body and merge normal exits. There is no cross-iteration fixed point.

Native projections, dynamic indices, and storage reached through pointers do
not retain facts across expressions. Save the address in a local ptr and check
that snapshot. A bool helper, API success code, or assertion supplies no proof.
An unproven dereference reports `CV-PTR-NONNULL`; no runtime trap is inserted.

## C++ boundary

Read and Write ptr targets lower to `const T*` and `T*`, composed by layer.
Read arguments pass address snapshots. Write arguments use the corresponding
pointer reference. Dereference selects its address before later operands can
replace the slot. Typed null constants retain their pointer type for native
overload resolution. Saving or passing a pointer requires a target declaration;
forming its type expression follows the target representation's own rules.

An explicit ptr type adopts a compatible native address. C++ checks native
conversions, target eligibility, member operations, overloads, and copyability.
Unannotated native results retain their delegated native type. There is no
runtime handle wrapper or automatic release.

Native output protocols such as `T**`, buffer traversal, and address arithmetic
use C++ adapters. `&p` accesses a pointer slot by reference; it does not compute
`T**`. Public `import(cpp)` and `export(cpp)` signatures retain their scalar
boundary. Interface declarations belong in headers; `#[cpp]` supplies source
definitions.

The language provides no pointer arithmetic, direct pointer indexing, address
ordering, implicit boolean conversion, integer/address conversion, or Carven
address-of operator. External owners, adapters, and callers determine target
lifetime, release behavior, and other provider protocols. Non-nullness proves
only an address fact.

## References

- [Language semantics](../docs/semantics.md#pointer-values)
- [Grammar](../docs/grammar.md#4-types)
- [Compiler boundaries](../docs/compiler.md)
- [C++ generation](../docs/backend.md)
- [Diagnostic contracts](../tests/internal/compiler/diagnostics/pointers.cpp)
- [Language behavior](../tests/language/types_and_values/pointers.cv)
- [Compiled interoperation cases](../tests/interop/pointers/native.cv)
