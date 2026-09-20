# Constant computation

Build text and tables during compilation using ordinary functions, local
variables, loops, and mutation. The same functions also run at runtime. This
example follows four forms of construction and retained storage in one program.

## Reading order

| File | What to follow |
| --- | --- |
| [text.cv](text.cv) | Direct interpolation, incremental String construction, and freezing completed text to `str` |
| [arrays.cv](arrays.cv) | Filling a fixed array and retaining its value and type |
| [slices.cv](slices.cv) | Reusing `squares` from `arrays.cv` and retaining its result as a static read-only slice |
| [records.cv](records.cv) | Constructing and mutating user records, then retaining a typed static table |
| [main.cv](main.cv) | Running the four demonstrations in this order |

Read each module's types and constants, then its construction functions and
`run_*` function. Each part compares a required constant call with an ordinary
runtime call.

## Text construction

`title` uses direct interpolation. `make_catalog` grows a local String through a
loop, `push`, and `append_format`; `frame` receives an owning interpolation result.
Intermediate values keep their String type and ownership. Each completed constant
initializer freezes text to immutable `str`; `title_bytes` reads the byte length
of the completed `title` constant.

The source supplies no fixed capacity or manually computed result length.
Carven knows the inputs, types, and selected operations, executes the admitted
computation, and determines its resulting bytes and length. Constant uses emit
static text with explicit byte lengths. The later `make_catalog(count)` call
still constructs an owning String through ordinary runtime operations.

## Array values and static slices

`squares` fills `[i32; 4]` through indexed assignment. In `arrays.cv`, `table`
retains that array type, and constant indexing produces `7`. `run_arrays` copies
the value into a local `stored` array; its slice borrows that local owner.

In `slices.cv`, the `[i32]` annotation instead retains the completed array as a
read-only slice with static backing. `table_view` can return this view without a
local array owner. Both parts retain ordinary runtime calls to `squares(offset)`.
Their identical values illustrate different storage and lifetime contracts.

## Record tables

`entries` constructs a fixed array of `Entry` records with names and explicit zero
codes, then assigns the codes in a loop. `catalog: [Entry]` retains the result in
static storage; selecting a field during compilation produces `11`.
`catalog_view` returns a view of those records.
The runtime call to `entries(offset)` produces a local array.

Freezing preserves nominal identity and field types. Supported records contain
admitted scalars, static text, fixed arrays, and other supported records. Owning
String fields are not replaced with `str` fields. These tables have fixed extents;
growable containers and class operations need separate admission and retention
contracts. Slice operations remain outside the `const fn` body subset; completed
arrays can become frozen slices at a constant initializer.

## Inspect the generated C++

From the repository root:

```sh
./xmakew build
./xmakew run carven compile --stdout examples/constant/main.cv examples/constant/text.cv examples/constant/arrays.cv examples/constant/slices.cv examples/constant/records.cv
```

`--stdout` prints artifact headings and contents without writing generated files.
Supply every application module shown above. The driver also collects the
toolchain and project Crafts; imports resolve within that combined source batch.

Semantic analysis resolves operations, evaluates admitted constant bodies, and
freezes completed values. The backend emits text literals and typed array or
record initializers. Frozen slices use named static backing and the existing
runtime slice representation. Constant table construction needs no runtime call
or loop. Ordinary function bodies retain their loops, mutation, ownership,
and checked array operations; output and iteration still execute normally.

The `const fn` qualifier permits required constant execution and does not request
automatic folding of runtime calls. Constant text construction uses the bounded
builtin formatting subset and a 1 MiB per-value limit, plus execution step,
call-depth, and cumulative work budgets. Unsupported required operations produce
source diagnostics.

## Run

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-constant
./xmakew run carven-example-constant
./xmakew test -g examples
```

In PowerShell, use `.\xmakew.ps1`. Expected output:

```text
Text
Carven build-0042
00,01,02,03
[Carven build-0042: 00,01,02,03]
Title bytes: 17
Runtime: 00,01,02
Array values
Selected: 7
Entries: 4
3
4
7
12
Runtime: 10 11 14 19
Static slices
Selected: 7
Entries: 4
3
4
7
12
Runtime: 10 11 14 19
Record tables
Selected: 11
import 10
const 11
struct 12
Runtime: const 21
```

Change `version`, the constant catalog count, or a constant table argument and
inspect the generated values. Change `count` or `offset` in a `run_*` function
to exercise its ordinary runtime call. Rebuild after changes, and restore the
documented inputs before running the output checks.
