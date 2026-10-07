# Body and operation preparation

Preparation derives implementation choices and operand demands from a published
semantic body. Realization consumes those choices without
changing the published evaluation and lifetime contracts.

## Body summaries and storage observations

`BodyPreparation` borrows semantic expression occurrences, stores operation and
subtree execution requirements and storage-observation summaries, and prepares
operand demands and operation plans on request. Each fragment owns its operation
preparation. SemIR owns types, lifetimes, origins, constants, effects, patterns,
and structured control flow.
Realization chooses ordinary or deferred storage from the C++ scope that owns
the source cleanup.
Propagation markers select their operand operation. Summary queries require an
occurrence from the prepared body. The published program outlives realization.

Preparation classifies each occurrence once and stores operation execution and
subtree execution separately. Later requests reuse the operation fact. Operand
calls retain their execution requirements when a known arithmetic result replaces
the arithmetic operation.

Preparation proves stability for bindings whose type has Read value-snapshot
semantics. Read parameters own immutable copies. Owners and Take parameters are
also stable when the body neither changes nor exposes their storage. Assignment,
Write or Take operands, writable addresses, and Write captures expose a binding.
Exposure through direct fields and fixed-array projections is attributed to the
containing binding. The proof is body-wide: exposure excludes an owner or Take
binding throughout the body. Write iteration bindings alias mutable elements and
retain storage observations; Read iteration uses the resolved-type snapshot policy.

Captures can change when a callback replaces the enclosing closure. They, Write
parameters, native values, and types with borrowed or owned backing retain their
storage-observation obligations. A stable slice descriptor does not make its
elements stable; indexing still observes backing storage. Stable reads need no
operand snapshot. Calls and checked operations retain their execution requirements;
source constant admission remains separate.

## Async preparation

`BodyPreparation` records whether an operation or region requires its owning
coroutine and indexes lexical children by lifetime. Await, escaping completion,
and child closure retain that context even when their value is discarded.

`backend.preparation.async` selects candidate producers from published bodies.
`PreparedAwaitProducer` borrows the producer body and optional transparent factory
route, with their semantic-node expansion cost. Eligibility requires scalar Read
inputs, a scalar or void result, no outward nominal failure or cancelled
completion, no children, and no test stop. A transparent synchronous factory
returns only a cold call whose arguments are parameter reads or frozen constants;
residual static selection may leave unconditional structural wrappers.

These queries depend only on the immutable semantic program. Realization decides
whether module ownership, the active callable chain, and the caller's remaining
expansion budget permit the candidate. Other operations retain their cold owner
and ordinary completion protocol.

`PreparedTailAwaitLoop` borrows exact self-await occurrences in return position.
Selection requires runtime scalar Read parameters, a scalar result other
than `char`, scalar local bindings, no captures or children, and no outward
failure, cancelled completion, or test stop. Source calls use resolved Read
scalar signatures. Address formation and opaque native expressions exclude the
body; native imports can observe by-value scalar snapshots. Tail actuals contain
no nested await or operation construction. Operation initialization and cleanup
remain with ordinary realization. Returns nested in ordinary operands or in
structured control with pending operation evaluation retain their activation.
Explicit yields remain eligible.

## Operation preparation

`backend/preparation` consumes immutable semantic operations and their known
normal-completion values. It owns operation plans, prepared bytes, residual
operand mappings, size bounds, and UTF-8 proofs. It never interns into semantic
stores. `PreparedOperation` optionally owns a plan through realization.
Operations without preparation and print calls without known scalar text hold no
plan payload.

`PreparedUnary` and `PreparedBinary` select runtime calls or native operators and
result-type restoration after C++ promotion. The binary plan also specifies
whether the implementation fixes operand types; shift counts retain independent
types. Fragment construction and operation realization consume the same plan.

Integer preparation uses known divisors and shift counts to select native
operators where C++ preserves Carven's value and width. Unsigned arithmetic that
avoids signed integer promotion uses C++ wrapping. Signed overflow and narrow
multiplication retain runtime implementations. Operand literal types and explicit
result conversions preserve promotion, overload, and deduction behavior.

`PreparedSIMDLane` selects a compile-time lane index
only when a published integer fact satisfies the operation's bound. The selected
runtime overload has no reporting path or site parameter. The original operand
still executes for its effects; unknown and out-of-range controls retain checked
runtime calls. This does not infer slice bounds or move checks across effects.

## Format and print plans

`PreparedFormatText` owns the complete text. `PreparedWriterFormat` owns literal
segments, builtin fields, reservation bounds, and retained operand indices.
Fields consume the retained operands in order: one value, followed by a width
when the integer field has no literal width. Retained operand indices map this
pack to the original source expressions.
`PreparedDelegatedFormat` owns native format bytes in `format_string`, retained
operand indices, and an encoding guarantee. `PreparedPrint` owns optional scalar
text for each source operand. Published semantics retain the source operation and
its value facts.

Preparation adapts published constant facts for `semantic.format.builtin`.
Optional text materialization has a preparation-owned 64 KiB budget. Direct
writer selection consumes prepared text and fields without native serialization.
Delegated residual formatting additionally budgets escaped braces and field
spellings through the bounded `semantic.format` serializer. Over-budget or
unsupported work retains runtime formatting. Serialization of the source
fallback is outside the optional materialization budget. Supported, known dynamic
integer widths and floating widths/precisions resolve to literal specification text
within the preparation budget. Writer classification accepts literal integer
specifications and dynamic widths of type `i8`, `u8`, `i16`, `u16`, or `i32`,
with optional zero padding and an optional `b`, `B`, `o`, `d`, `x`, or `X`
presentation.
These width types fit the runtime entry's signed 32-bit parameter; negative
widths terminate. Wider width types and unsupported specifications use general
formatting.

Writer also accepts default floating output, fixed/scientific/general floating
presentations with precision up to 256, and default text, boolean, and character
fields. Larger precisions and decorated floating fields retain the standard
formatter. Reservation bounds exclude dynamic text bytes and dynamic-width
fields. Text lengths contribute after operand completion; dynamic-width fields
grow storage as needed. Native/custom formatters retain all original arguments
because they can inspect the argument pack.

Preparation translates each selection into a generic input demand: value or
execution effects. Every original operand remains in source order. Realization
uses these demands to preserve effects, failures, scalar snapshots, storage reads,
and temporary backing, while passing only demanded values to the native operation.
The sequencing and storage machinery does not classify format plans.

Builtin writer inputs use the same value-read demand as Carven parameters:
scalars and text descriptors are copied values; owning strings retain Read
borrowing until their bytes are written. Delegated formatters retain their
native operand access. Repeated writer use therefore needs no scalar backing
beyond its evaluation scope, while actual native references retain theirs.

C++ checks native format specifications and formatter availability, performs object
layout and ordinary optimization, and owns instruction selection. Carven selects
implementations from published source facts and local operation contracts.

## Native construction operands

`PreparedNativeConstruction` maps each native construction argument to a retained
operand index or a borrowed constant argument from its result query. Operand demands and
final argument delivery consume this mapping.

Native construction retains type, access, and known scalar facts for both queries
and execution.

## Callable adaptation plans

`PreparedCallableAdaptation` retains the shared semantic adaptation classification
and the array delivery form. Realization uses that plan with ordinary operand
storage and backing retention.
