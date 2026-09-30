# Body and operation preparation

Preparation derives implementation choices and operand demands from a published
semantic body. [Realization](realization.md) consumes those choices without
changing the published evaluation and lifetime contracts.

## Body summaries and storage observations

`BodyPreparation` borrows semantic expression occurrences, stores subtree execution
and storage-observation summaries, and prepares operand demands and operation plans
on request. Each fragment owns its operation preparation. SemIR owns types,
lifetimes, origins, constants, effects, patterns, and structured control flow.
Conditional-evaluation summaries constrain intermediate storage that may cross
branches. Realization also accounts for a result's final construction position
within its cleanup frame when choosing ordinary or deferred storage.
Propagation markers select their operand operation. Summary queries require an
occurrence from the prepared body. The published program outlives realization.

Read parameters whose types neither borrow storage nor contain native values
own immutable copies, so later operand execution needs no snapshot of them.
A stable slice descriptor does not make its elements stable; indexing still
observes backing storage. Owners and Take parameters can expose native `T&&`;
captures can change when a callback replaces the enclosing closure. These bindings,
Write parameters, borrowed aggregates, and native values retain storage-observation
obligations across later execution.

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

C++ checks native format specifications and formatter availability, performs object
layout and ordinary optimization, and owns instruction selection. Carven selects
implementations from published source facts and local operation contracts.

## Native construction operands

`PreparedNativeConstruction` maps each native construction argument to a retained
operand index or a borrowed constant argument from its result query. Operand demands and final argument
delivery consume this mapping.

The [native construction result contract](representation.md#native-construction-results)
retains type, access, and known scalar facts for both queries and execution.

## Callable adaptation plans

`PreparedCallableAdaptation` retains the shared semantic adaptation classification
and the array delivery form. [Callable adaptation](realization.md#callable-adaptation)
uses that plan with ordinary operand storage and backing retention.
