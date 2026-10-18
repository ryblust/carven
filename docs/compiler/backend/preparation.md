# Body and operation preparation

This reference defines body summaries, operand demands, and operation plans for
C++ generation.

## Body summaries and storage observations

`BodyPreparation` borrows a published body's semantic expression occurrences,
stores operation and subtree execution requirements and storage-observation
summaries, and prepares operand demands and operation plans on request.
Realization consumes these plans without changing the published evaluation and
lifetime contracts. Each fragment owns its operation preparation. SemIR owns
types, lifetimes, origins, constants, effects, patterns, and structured control flow.
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

Read traversal of a temporary array can borrow stable local owners containing
Carven storage. Preparation uses the same body-wide writable-exposure facts and
excludes native values, borrowed views, callable contents, and operation owners.
Loop bodies requiring a coroutine context or containing closures or operation
owners retain element storage. Address formation, source calls, native operations,
and delegated formatting also retain it. Known-text and builtin writer format
plans consume values; their operand effects remain checked. Identity observation
is checked separately from execution and snapshot requirements. Eligible traversal
uses a C++ array of const element pointers and binds each element through a const
reference.

Captures can change when a callback replaces the enclosing closure. They, Write
parameters, native values, and types with borrowed or owned backing retain their
storage-observation obligations. A stable slice descriptor does not make its
elements stable; indexing still observes backing storage. Stable reads need no
operand snapshot. Calls and checked operations retain their execution requirements;
source constant admission remains separate.

## Async preparation

`BodyPreparation` records coroutine-context requirements and indexes lexical
children by lifetime in one traversal. Entry visits record children in source
order; exit visits summarize their enclosing operations and regions. Await,
escaping completion, and child closure require the
owning coroutine even when their value is discarded.

`PreparedAwaitProducer` borrows an eligible producer body and an optional
transparent synchronous factory, together with their semantic-node expansion
cost. Producers have scalar Read inputs, scalar or void success, and no outward
failure, cancelled completion, children, or test stop. Factories return one cold
call using parameter reads and frozen constants. Residual static selection may
leave unconditional region wrappers. Realization supplies module visibility,
active-callable checks, and the remaining expansion budget.

`PreparedTailAwaitLoop` identifies self awaits in return position. Eligibility
requires scalar Read inputs and locals, a scalar result other than `char`, and no
captures, children, outward failure, cancellation, or test stop. Address formation
and opaque native operations exclude the body. Tail arguments and enclosing
conditions must finish without retained operation storage or suspension; this
preserves invocation lifetimes when one native loop reuses the parameter slots.
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
runtime calls. Lane selection supplies no slice-bound proof; checks retain their
source execution position.

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

C++ checks native format specifications and formatter availability. Preparation
selects implementations from published facts and local operation contracts.

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
