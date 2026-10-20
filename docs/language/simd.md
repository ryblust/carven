# SIMD

This page defines SIMD vector and mask types, operations, and source helpers.

## Types and modules

Vector and mask types are available by default and belong to `std::simd`.
Explicit imports and aliases select the same types. Import block helpers from
`std::simd.bytes` or `std::simd.floats`, and byte-set queries from `std::simd.scan`.
Source helpers compose the builtin primitives under ordinary ownership and
execution rules.

| Vector | Element | Lanes | Comparison mask | Packed mask bits |
| --- | --- | --- | --- | --- |
| `u8x16` | `u8` | 16 | `mask16` | `u16` |
| `u8x32` | `u8` | 32 | `mask32` | `u32` |
| `f32x4` | `f32` | 4 | `mask4` | `u8` (low four bits) |
| `f32x8` | `f32` | 8 | `mask8` | `u8` |

Logical lane widths and operations are independent of native register widths.

## Operators and masks

Byte vectors default to zero. `+` and `-` wrap per lane; `&`, `|`, `^`, and `~`
operate on bits. Float vectors default to positive zero and support `+ - * /`
and unary `-`. Corresponding compound assignments are supported. An exact
scalar element type broadcasts in either operand position; literals receive
that type's context. Other scalar types require explicit conversion.

All six comparisons return the vector's mask type. Masks default to false and
combine with `&`, `|`, `^`, and `~`. They never implicitly convert to scalar
bools. Use `.any()`, `.all()`, `.count()`, `.first_or(fallback)`, or `.bits()`.
Bit `i` denotes lane `i`, independent of memory byte order. `first_or` returns
the lowest true lane or the supplied `usize` fallback. `.select(yes, no)` takes
two matching vectors and eagerly evaluates both arguments in source order.
Widths and mask types do not implicitly convert to each other.

SIMD values own their lanes without exposing addressable lanes or native register
layout. They do not provide scalar structural equality for enclosing aggregates;
compare vector lanes explicitly.

## Construction and loading

In this table, `Vector` is any vector above, `Mask` its mask, `T` its element,
and `N` its lane count.

| Operation | Contract |
| --- | --- |
| `Vector::splat(value)` | Broadcast one `T`. |
| `Vector::from_array(values)` | Copy from `[T; N]`. |
| `Vector::load(values, offset)` | Read exactly `N` elements, without an alignment precondition. |
| `Vector::load_partial(values, offset, fill)` | Read at most `N` elements and fill the rest; `offset <= length`. |
| `Mask::from_bits(bits)` | Expand packed bits; ignore bits above `N`. |
| `Mask::prefix(count)` | First `count` lanes true; `count <= N`. |

Associated functions use `Type::function(...)`; aliases of the type provide the
same operations. Value methods use `value.method(...)`.

## Vector and mask methods

| Operation | Contract |
| --- | --- |
| `value.to_array()` | Copy to `[T; N]`. |
| `value.lane(index)` / `value.with_lane(index, element)` | Read a lane or return an updated copy; `index < N`. |
| `value.swizzle(indices)` | Select lanes using a static `[u8; N]`; every index is below `N`. |
| `left.extract(right, offset)` | Take `N` elements from the concatenation at `offset`, in `0..=N`; `offset` is static. |
| `mask.bits()` / `.count()` | Packed bits / `usize` count. |
| `mask.select(yes, no)` | Per-lane selection of matching vectors. |

Byte vectors expose `.sum() -> usize`, the exact mathematical sum of all lanes.
The maximum is `N * 255`; the reduction does not wrap. They also expose
`.lookup(indices)` with either byte-vector width. The table width determines
the valid indices; an index at or above that width yields zero. The indices
vector determines the result width. All four combinations of 16- and 32-byte
tables and indices are supported. Lookup uses the entire logical table,
including indices crossing native register boundaries. `.shift_left(count)` and
`.shift_right(count)` shift every byte independently by `0..7` bits, zero-fill,
and discard shifted-out bits. Extract offsets and shift counts are static inputs;
the compiler checks their bounds.

Swizzle permits duplicate indices and returns the receiver's vector shape.
Its controls can come from `const fn`, explicit constants, or `const for` indices
and require a constant source independent of runtime locals.

Invalid dynamic bounds terminate; static execution diagnoses them.
Partial loads never rely on padding or read outside the supplied slice, including
empty input. Float offsets count elements rather than bytes.

## Floating methods

`f32x4` and `f32x8` provide these Read methods in static execution,
interpretation, and native execution:

| Method | Result and contract |
| --- | --- |
| `value.is_finite()` | `mask4` or `mask8`; each lane is true for finite values. |
| `value.sqrt()` | Preserve signed zero and +infinity; negative nonzero values, -infinity, and NaN produce NaN. |
| `value.abs()` | Positive magnitude per lane; either zero becomes +0; NaN remains NaN. |
| `value.min(other)` / `value.max(other)` | Propagate either NaN; equal values retain the receiver, including its zero sign. |
| `value.clamp(lower, upper)` | Check bounds per lane; preserve a NaN value and return the selected bound or value. |

`other`, `lower`, and `upper` each accept either the receiver's vector type or
an `f32` scalar broadcast to every lane. Unsuffixed literals receive `f32`
context. Other scalar types require an explicit conversion to `f32`; vector
arguments must match the receiver's shape.

Numeric results retain the receiver's vector shape. Clamp requires
`lower <= upper` in every lane; a reversed or NaN bound in any lane causes an
assertion failure, even when the result is discarded. The receiver and arguments
evaluate once in source order before the operation.

```carven
let samples = f32x8::splat(-4.0);
let magnitudes = samples.abs().sqrt();
let bounded = magnitudes.clamp(0.0, 1.0);
assert(bounded.is_finite().all());
```

## Block and byte helpers

`ByteBlock` contains `u8x32` and `mask32` values; `FloatBlock` contains `f32x8`
and `mask8` values. `std::simd.bytes` and `std::simd.floats` each provide `block_count` and
`load_block`. `block_count` returns the number of complete or partial blocks.
`load_block(values, offset)` returns a block with `value`, `active`,
`offset`, and `length`. Tail lanes are zero-filled and inactive; include `active`
whenever padding could match. An offset equal to the input length produces an
empty block.

```carven
import std::simd.bytes using { block_count, load_block }

const fn count_spaces(bytes: [u8]) -> usize {
    var count: usize = 0;
    for index in 0..block_count(bytes) {
        let block = load_block(bytes, index * 32);
        count += ((block.value == 0x20) & block.active).count();
    }
    return count;
}
```

Byte helpers include `count_byte`, `find_byte` (input length when absent),
`ascii_digit`, `ascii_lower`, and `ascii_prefix` (first non-ASCII byte offset).
They operate on encoded bytes. Use `value.to_array()` to obtain an owning fixed
array; ordinary assignment writes it to a mutable destination.

## Byte-set scanning

`std::simd.scan` supplies byte-set queries. `one_of([u8])` constructs a `ByteSet`;
`byte_range(lower, upper)` includes both endpoints and requires `lower <= upper`.
Membership includes all byte values, including NUL and FF.
`set_union(left, right)` combines two sets; duplicates have no effect. A text's
`.bytes` denotes encoded bytes, not Unicode characters.

`match_set(value, const set)` classifies a raw `u8x32`. `count_where`, `find_where`,
and `prefix_where` query an entire byte slice. `find_where` returns the first
matching byte offset, or the input length when absent. `prefix_where` returns the
leading matching run length, including the input length when every byte matches. Empty
input returns zero for all three queries.

```carven
import std::simd.scan using { one_of, count_where, find_where }

const punctuation = one_of("{}[]:,".bytes);
const fn count_punctuation(text: str) -> usize => count_where(text.bytes, punctuation);
const fn first_punctuation(text: str) => find_where(text.bytes, punctuation);
```

All query functions are `const fn`. A static set specializes the matching work;
queries preserve the same membership and positions for all inputs. Whole-slice
queries handle partial tails without reading outside the supplied slice.

`find_from(bytes, start, set)` searches from `start` and returns a position in
the supplied input, or its length when absent. `prefix_end(bytes, start, set)`
returns the end of the matching run starting there. Both accept
`start <= bytes.length()`, including an
empty suffix. `find_where` and `prefix_where` use the same scans from zero.

## Floating execution

Floating arithmetic follows scalar `f32` execution per lane. Signed zeros compare
equal, NaN compares unequal to every value, and ordered comparisons with NaN are
false. Constant identity preserves floating bit patterns, including zero signs
and NaN representations. Static execution uses compiler-host arithmetic;
ordinary runtime arithmetic retains the target's floating environment. Identical
results across differing floating environments, NaN payload propagation, and
nondefault rounding modes are unspecified. Fused operations, approximations,
and floating horizontal reductions are not provided.
