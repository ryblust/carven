# Builtin realization

These implementations use common preparation, operand sequencing, storage, and
representation contracts. Format plans and their budgets
belong to preparation; this reference describes runtime and target operations
that consume those plans.

## Owning text realization

Text byte and scalar queries use `runtime::text_bytes` and `runtime::text_chars`.
C++ overload resolution selects the `std::string_view` or `const String&` adapter;
both return views of the input storage. Semantic loans and generated cleanup
scopes retain the backing owner.

Builtin String lowers to the owner in `string.hpp`, with private `std::string`
storage. The shared Read storage policy preserves caller aliasing for named owners and
projected fields/elements across sequencing and failure barriers. Text operations
select runtime factories and members through target syntax and record their
support-header dependencies. Write receivers remain places; range projections
borrow the receiver's text storage.

Native byte storage enters through `String::from_utf8`, which validates UTF-8
before adopting it.

All formatting forms complete their hole operands in source order under the
Read storage policy before constructing or appending text. Scalar snapshots,
String aliases, failures, and temporary backing follow ordinary operand
construction. Discarding an owning result retains String construction.

An owning `SemFormat` with `PreparedFormatText` lowers to `String::from_str` with
an explicit byte-length literal. Mixed preparation maps retained operands to the
selected format; folded operands retain their execution obligations. String
contents are observed after all holes complete.

`PreparedWriterFormat` lowers through the shared statement builder in
`realization.format` to `runtime::Writer` in `writer.hpp`. Formatted append emits
ordinary statements after the expression builder completes its inputs and flushes
pending earlier work. A direct owning return with the native return ABI creates a
local String, writes its fields, and returns it by name, permitting C++ NRVO.
Nested owning expressions, initialization, and failure-transport returns retain an
expression lambda to preserve their construction and delivery boundary. Its
parameters use the existing Read storage policy.

Both forms append literal text and call
`integer<base, uppercase, zero_pad>(value, width)` or
`integer_dynamic_width<base, uppercase, zero_pad>(value, width)` in order, copy
text fields, select boolean text, and encode Unicode scalars directly. Floating fields call
`floating(value)`, `fixed<precision>(value)`, `scientific<precision>(value)`, or
`general<precision>(value)`. These small runtime entries use `std::to_chars` with
a bounded stack buffer and append converted bytes directly to the destination.
Writer realization consumes completed operands before emitting reservation and
field writes.

The prepared minimum and maximum byte counts for fragments with known bounds
reach the writer as ordinary arguments, followed by an initializer list of
explicit dynamic text byte lengths. Carven emits each `.size()` query after all
holes complete. Runtime
adds these lengths to both bounds with checked arithmetic; it does not
rediscover field types or parse format policies.

When the minimum exceeds available capacity, runtime reserves for the maximum, capped at the
destination's size limit. Otherwise normal storage growth applies. Equal bounds
reserve an exact size for the counted fragments; dynamic-width fields are excluded.
An upper bound alone does not force allocation for a short result. Runtime checks
size arithmetic and performs integer conversion
with `std::to_chars`, sign handling, and padding. Writer integer inputs use the
same `runtime::Integer` domain as arithmetic; boolean and character fields use
their separate operations.

The writer borrows private String storage and requires valid text and disjoint
inputs. Direct conversions need no format parsing or output validation scan.
Unsupported fields retain the complete selected general format call. Writer
paths have no `<format>` dependency; fully precomputed contents use direct text
construction or append.

Reservation requests do not prescribe the native String's exact capacity or
allocation alignment; the native String implementation selects both.

Other owning `SemFormat` operations pass their selected argument pack. The
prepared `PreparedDelegatedFormat::encoding` selects `format_valid_utf8` for
`ValidUTF8` and `format` for `Unproven`. Realization embeds the prepared bytes as a
compile-time `std::string_view` with an explicit byte length. Inside that call, String
aliases provide text views and `char` values encode to UTF-8 Strings. C++ checks
`std::format_string` and formatter availability; source directives attribute those diagnostics to the
interpolation. Both entries are `noexcept` and use the same argument adapters
and `std::format` call. The general entry passes the completed buffer through
`String::from_utf8`; the proved entry adopts it without scanning it again.
`StringFormatAccess` privately adopts that buffer using the shared String move
constructor, without an additional byte copy or allocation. Entry selection
consumes the preparation-owned encoding proof.

An append `SemFormat` places its Write receiver before the hole operands in target
construction. Realization selects the receiver once, then completes all holes using
the same Read, failure, and temporary-backing rules. Preparation operand indices
exclude the receiver. Known contents lower to `receiver.append(static_text)` after
required hole execution. Prepared builtin fields use the writer above. Otherwise,
`PreparedDelegatedFormat::encoding` selects `append_format_valid_utf8`
or `append_format`, with the receiver followed by the selected format and argument pack.

The append entries reuse their corresponding owning formatter, then append the
completed valid text. The proved entry formats once, adopts that buffer, and avoids
a UTF-8 validation scan. Standard formatting owns any partial byte writes until
completion in a temporary native buffer. Known contents lower to direct append.
`Writer::append` requires each supplied fragment to be complete valid text.

Both entries are `noexcept` and require the source operation's destination/input
separation, without rollback after formatting begins. Capacity growth and native
formatter allocations remain runtime concerns.

Ownership analysis establishes borrowing validity before lowering. Runtime views
carry no owner metadata. String owners, pending operands, retained range sources,
closure captures, and Outcome payloads use ordinary construction and cleanup frames.

Unchecked character construction lowers to a C++ cast to the character
representation. Unchecked UTF-8 construction calls `utf8_text` from `text.hpp`
to create a view over the input bytes. Neither operation validates content;
semantic ownership analysis preserves the text's input backing before lowering.
The backend supplies runtime includes without a source-level header import.

`utf.hpp` validates borrowed, contiguous UTF-8 through `utf8_is_valid`.
Constant evaluation, inputs shorter than 32 bytes, and targets selecting the
portable SIMD backend use its scalar scanner. Other runtime inputs use the
existing SIMD component to check complete 32-byte blocks. Three preceding bytes
carry continuation requirements across blocks; the scalar scanner revisits the
last scalar and checks the remaining tail, including truncation at an exact block
boundary. Loads copy only proven readable bytes and require no report site.
The scan returns a Boolean; `checked_utf8` alone attaches the ingress site's
failure report. Single-scalar encoding and decoding remain scalar.

SIMD selection follows the consumer's compilation target and
`CARVEN_SIMD_FORCE_PORTABLE`. `simd::uses_native_backend` reports whether NEON or
AVX2 intrinsics were selected. The portable backend may also compile to machine
vector instructions. Translation units using these inline runtime headers select
the same SIMD backend. Runtime does not depend on the `std::utf` craft,
whose source implementation owns streaming state and precise encoding errors.

## Builtin calls and reports

`SemPrint` lowers to runtime printing calls; `SemReport` shares condition
observation and failure reporting between assertions and tests. Its message is
lowered inside the failure branch, including construction, effects, and cleanup.
`Assert` calls the nonreturning runtime assertion reporter; `Require` and `Fail`
use test-stop transport.

Test-stop transport uses the function's result carrier and performs normal scope
cleanup during propagation.

Prepared scalar print operands pass their known text to the ordinary runtime
printing entry. Preparation retains the original operand's execution under its
normal access and sequencing rules. The fallback writes the prepared bytes directly;
when library feature detection admits `std::print`, it prints the text through that
facility. Earlier values and separators remain observable if a later value's
formatting or output fails. An inner formatted String still completes, including
its owning construction, before
subsequent print arguments execute. Allocation counts and incidental buffers
inside scalar output conversion are not source guarantees; source String
construction and operand completion retain their boundaries.

Structural print operands use a synchronous borrowing wrapper. The compiler
selects an emitter type from published semantic types; scalar, sequence, and
range emitters compose child types, while `realization.display` generates nominal
field accesses and enum selection. An enum's closed case set makes the final
alternative unconditional; a single-case enum needs no selection. Module lowering shares a content-named
nominal helper across its use sites. Type-selected emitters use the shared
`stateless_value` instance.

Emitters take `(writer, value, depth)`. Each displayed root starts at depth zero;
children receive `depth + 1`. Indentation uses that absolute depth. The writer
checks the depth and element limits and owns the bounded output buffer. Passing
depth by value keeps sibling observations independent. Semantic execution uses
the same depth convention with its own semantic value reader.

The wrapper borrows both value and emitter, preserving the emitter's constness.
Temporary arguments live through the complete expression that synchronously
consumes the wrapper. Runtime print, comparison, and entry-failure reporting use
the same invocation protocol. `DisplayWriter` handles scalar conversion, nested
text escaping, and bounded sequence and range display. Callable and unsupported
native leaves render opaquely. Completed text uses the ordinary printing entry.
The wrapper routes top-level C strings to the text printing entry. Nested C strings
use the writer's quoting and escaping rules; null C strings render as `nullptr`.

Known successful conditions retain their execution effects and need no report
or explanation storage. Known failures need no report guard. Dynamic conditions
form the guard directly; fatal reports terminate the lowering continuation.

Comparison explanations observe the condition after operand sequencing. The
observer in `report.hpp` compares once and renders operands on failure, before
message evaluation can change their values. Short-circuit explanations use
ordinary expression construction: selected branches observe the
right Boolean result, and a skipped failing branch records `<not evaluated>`.
Known left operands select their branch during realization. Explanation
storage is local to the report operation and is completed before message evaluation;
`TestFailure` borrows it only during the reporter callback.

`trap.hpp` owns `SourceSite`, the active test context, the report layout, and
trap termination. A `SourceSite` names a Carven file, line, and column in two
machine words on 64-bit targets: a null-terminated file literal and two 32-bit numbers.
The site is a required argument of every operation that can report: checked
division, remainder, shifts, array and slice indexing, slice ranges, SIMD lane
and memory checks, scalar conversion, test reports, and the entry wrapper. A
`char` that crosses an imported result or an exported parameter reports the
declaration of that function. A generated statement spans several physical
lines under one `#line` directive, so a C++ source location cannot supply the
column. A native boundary without a Carven position uses a native C++ site:
bytes adopted from a native producer, entry
arguments, a null function pointer, a subscript written in C++, and the test
runner protocol. An index check reports its subscript's position, which is the
position the executor reports. Trap functions are marked cold and non-inlined
where the C++ compiler supports it. After the checked wrapper is inlined,
native optimization can defer site materialization to the failure path; this
is not guaranteed by the site's size or the cold attribute alone.
`entry.hpp` uses the same layout for a failure that escapes the program entry;
that report does not terminate the process.

For SIMD `lane` and `with_lane`, preparation selects a template
overload when the control has a known value within that operation's bounds.
These overloads enforce the bound with `static_assert` and take no `SourceSite`.
Operand effects retain source order even when the control value becomes a
template argument. Dynamic controls and known out-of-range controls retain the
checked operation and its original report site. Known mask prefixes already fold
to complete constant masks. Memory operations still check the actual slice range;
a known lane index is not a memory-range proof.

`testing.hpp` owns test-stop transport and `TestContext`, which tracks the active
case and completed-case counts. `report.hpp` owns condition observation, failure
text, and fatal assertions. `check`, `require`, and `fail` call
`report_test_failure`, which reaches the runner of the active test and traps at
the report's site when no test is active.
The default layout groups test identity, condition, operands, and message under
each failure location and identifies test stop or execution abort. It borrows
the active case's module and name for every report. The runner emits a summary
when it completes. Custom test reporters supply their own output; fatal
assertions use the assertion reporter.

Runtime `print.hpp` selects C++23 `std::print` using library feature detection and
otherwise supplies the C++20 `std::format`/`fwrite` implementation. This selection
belongs to consumer compilation and preserves the user's selected C++ standard.
Values, structural display text, separators, and newlines share this output policy.
The C++20 fallback writes UTF-8 bytes; Windows console Unicode rendering depends
on the console configuration. `std::print` supplies native Unicode terminal handling.
