# SIMD

Import byte helpers from `std::simd.bytes` and floating helpers from
`std::simd.floats`. The compiler supplies fixed logical vector and mask types.
Operations have the same lane semantics in static execution and native code.

Vector types and their primitive operations are built into the language and do
not require an import. Import the craft when using its block and algorithm
helpers. Generated SIMD operations and runtime UTF-8 validation use the native
support header. Text support therefore also depends on its selected backend.

The runtime supplies native value representations and primitive operations;
the compiler executor supplies their static-execution equivalents. Library
algorithms and block traversal belong to this craft.

| Vector | Element | Lanes | Comparison mask | Packed mask bits |
| --- | --- | --- | --- | --- |
| `u8x16` | `u8` | 16 | `mask16` | `u16` |
| `u8x32` | `u8` | 32 | `mask32` | `u32` |
| `f32x4` | `f32` | 4 | `mask4` | `u8` (low four bits) |
| `f32x8` | `f32` | 8 | `mask8` | `u8` |

The runtime has a C++20 portable backend that requires no SIMD instruction-set
options. On Clang and GCC, its arithmetic and comparisons use internal 16-byte
compiler vector expressions; other compilers evaluate the same operations per
lane. Its stored values remain byte arrays, and ordinary C++ optimization may
use machine vector instructions.
Each translation unit selects one backend at compile time: AArch64 NEON when
available, x86 AVX2 when the consumer enables it, and otherwise a portable lane
implementation. All backends share the same lane contracts. There is no runtime
dispatch. On x86 with AVX2, wide values use native 256-bit
registers; narrow operations may use 128-bit registers. On NEON, wide values use
two 128-bit registers. Logical widths are independent of register width.
Static execution uses owned lanes and is independent of the compiler host's
instruction set.

The C++ runtime uses compiler-specific forced inlining for intrinsic wrappers
and MSVC/Clang `vectorcall` on Windows x86. Its vector and mask carriers are transparent,
zero-initialized aggregates so MSVC and clang-cl can pass and return vectors in
SIMD registers. These support-level choices do not expose native register storage
or calling-convention attributes to Carven source.

Consumers select their target's instruction flags. The compiler and consumer
build rule never enable AVX2; an x86 consumer opts in with `-mavx2`, `/arch:AVX2`,
or Xmake's `add_vectorexts("avx2")` for its target. Translation units that use
SIMD or runtime text support must select the same backend. Public runtime types live in backend-specific
inline namespaces within `carven::runtime::simd`. These namespaces give the types
distinct identities, so symbols that encode them can detect some backend
mismatches at link time. Return types alone and enclosing structs may not expose
the mismatch in their symbols.

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

## Primitive surface

In this table, `Vector` is any vector above, `Mask` its mask, `T` its element,
and `N` its lane count.

| Operation | Contract |
| --- | --- |
| `Vector::splat(value)` | Broadcast one `T`. |
| `Vector::from_array(values)` / `value.to_array()` | Copy to/from `[T; N]`. |
| `Vector::load(values, offset)` | Read exactly `N` elements, without an alignment precondition. |
| `Vector::load_partial(values, offset, fill)` | Read at most `N` elements and fill the rest; `offset <= len`. |
| `value.lane(index)` / `value.with_lane(index, element)` | Read a lane or return an updated copy; `index < N`. |
| `left.extract(right, offset)` | Take `N` elements from the concatenation at `offset`, in `0..=N`; `offset` is static. |
| `Mask::from_bits(bits)` | Expand packed bits; ignore bits above `N`. |
| `Mask::prefix(count)` | First `count` lanes true; `count <= N`. |
| `mask.bits()` / `.count()` | Packed bits / `usize` count. |
| `mask.select(yes, no)` | Per-lane selection of matching vectors. |

Byte vectors expose `.sum() -> usize`, the exact mathematical sum of all lanes.
The maximum is `N * 255`; the reduction does not wrap. They also expose
`.lookup(indices)` with a matching byte vector of indices: every index `>= N` yields zero. Lookup uses the entire logical table,
including indices crossing the 128-bit hardware boundary. This differs from
AVX2's lane-local byte shuffle instruction. `.shift_left(count)` and
`.shift_right(count)` shift every byte independently by `0..7` bits, zero-fill,
and discard shifted-out bits. Extract offsets and shift counts are static inputs;
the compiler checks their bounds and emits them as native template arguments.

Invalid dynamic bounds terminate; static execution diagnoses them.
Partial loads never rely on padding or read outside the supplied slice, including
empty input. Float offsets count elements rather than bytes.

## Craft helpers

`std::simd.bytes` uses 32-byte blocks (`u8x32`, `mask32`). `std::simd.floats`
uses eight-float blocks (`f32x8`, `mask8`). Both provide `block_count`, `load_block`,
`store`, `store_partial`, static `extract`, and static `swizzle`.

`block_count` divides without overflowing a rounded-up length. `load_block`
returns an owning block with `value`, `active`, `offset`, and `len`. Tail lanes
are zero-filled and inactive; include `active` whenever padding could match.
An offset equal to the input length produces an empty block. Store helpers use
writable fixed arrays of the block's width; read-only slices do not grant write
access. `store_partial` changes only its first `count` elements.

```carven
import std::simd.bytes using { block_count, load_block };

const fn count_spaces(bytes: [u8]) -> usize {
    var count: usize = 0;
    for index in 0..block_count(bytes) {
        let block = load_block(bytes, index * 32);
        count += ((block.value == 0x20) & block.active).count();
    }
    return count;
}
```

Byte helpers also provide `count_byte`, `find_byte` (input length when absent),
`first_or`, `ascii_digit`, JSON's four `ascii_whitespace` bytes, and `ascii_lower`.
`ascii_prefix` checks 64-byte groups, then one 32-byte block and a bounded scalar
tail. The [UTF craft](../utf/README.md) shares these vector operations.

Static helper controls are ordinary `const` parameters. `extract` accepts
`0..=N`; byte `shift_in(previous, current, const count)` prepends the last
`count` previous bytes, in `0..=32`. Byte shift counts are below eight.
`swizzle` requires a constant `[u8; N]` whose entries are below `N`. Controls
may be computed by `const fn`, explicit constants, or `const for` indices.
Ordinary `let` bindings and `for` indices are not promoted; wrappers retain `const`.

Extraction and byte shifts encode their controls as native template arguments.
Lane access accepts dynamic indices and checks bounds. Ordinary functions live
in separate C++ translation units; cross-module inlining depends on consumer
build settings. Static specializations have shared inline definitions.

## Byte-set scanning

`std::simd.scan` supplies byte-set queries. `one_of([u8])` constructs a `ByteSet`;
`byte_range(lower, upper)` includes both endpoints and requires `lower <= upper`.
The set stores four `u64` words: bit `n` identifies byte `n`, including NUL and FF.
`set_union(left, right)` combines two sets; duplicates have no effect. A text's
`.bytes` denotes encoded bytes, not Unicode characters.

`match_set(value, const set)` classifies a raw `u8x32`. `count_where`, `find_where`,
and `prefix_where` query an entire byte slice. `find_where` returns
`ByteSearch::Found(input_offset)` or `Missing`; `prefix_where` returns the leading
matching run length, including the input length when every byte matches. Empty
input has count and prefix length zero, and no first match.

```carven
import std::simd.scan using { one_of, count_where, find_where };

const punctuation = one_of("{}[]:,".bytes);
const fn count_punctuation(text: str) -> usize => count_where(text.bytes, punctuation);
const fn first_punctuation(text: str) => find_where(text.bytes, punctuation);
```

All operations are `const fn`. Sets with at most four contiguous runs select
direct byte or range comparisons; other sets use nibble lookup tables.
Empty and complete sets produce constant masks. Matching plans are derived from
the set during compilation. Whole-slice queries read complete 32-byte blocks and
check the remaining bytes individually; input padding is unnecessary.
`count_where` accumulates matching lanes across blocks and takes an exact vector
sum before the byte accumulators can wrap.

## Floating execution

Floating arithmetic follows scalar `f32` execution per lane. Signed zeros compare
equal, NaN compares unequal to every value, and ordered comparisons with NaN are
false. Constant identity preserves floating bit patterns, including zero signs
and NaN representations. Static execution uses compiler-host arithmetic;
ordinary runtime arithmetic retains the target's floating environment. Identical
results across differing floating environments, NaN payload propagation, and
nondefault rounding modes are not promised. Fused operations, approximations,
min/max, and floating horizontal reductions are not provided.
