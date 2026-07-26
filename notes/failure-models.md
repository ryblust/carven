# Failure Models: Effects, Control, and Runtime Representation

Failure handling often collapses several different questions into one word:
what callers must know, which exits a body can take, how handlers receive
control, and how a target ABI transports a value. Keeping those questions
separate is the key to understanding both language designs and their
implementations.

This article supplies a comparative compiler model and research questions. It
does not select a failure model for any particular language or compiler.

## Four distinct layers

| Layer | Question |
| --- | --- |
| Source contract | Which failure behavior can a caller observe and must source acknowledge? |
| Semantic knowledge | Which failure alternatives, handlers, payload uses, and outward effects has the compiler proved? |
| Flow analysis | Which normal and failure paths affect ownership, availability, or reachability? |
| Runtime transport | Which result or payload must cross a callable, suspension, or interop boundary? |

A source construct may affect the first two layers and disappear before the
last. Conversely, a runtime result can remain unknown without requiring a
source-shaped object: a direct operation or control edge may be sufficient.

This gives three broad language models.

- A **control effect** says that evaluation can continue through a distinct
  exit. The error payload may travel along that edge without success and
  failure first becoming one sum value.
- A **sum value** makes success and failure alternatives ordinary data. It can
  be stored, passed, returned, and inspected later, so its identity remains
  part of source semantics even when optimization removes its physical
  storage.
- A **runtime exception mechanism** gives an open runtime responsibility for
  finding handlers, transporting exception objects, and unwinding state. The
  function type may retain little or no static information about that channel.

These models can offer similar surface conveniences while giving a compiler
very different knowledge and realization freedom.

## Runtime materialization boundaries

An action that depends on runtime input belongs in generated runtime behavior.
It does not follow that the compiler needs a persistent CFG or that the program
needs a source-shaped object representing the action. Structured control can
go directly to a handler, while a callable boundary can use one compact
carrier.

Runtime materialization becomes necessary when execution must preserve
something for later use. Common reasons include:

- crossing a call, public ABI, interoperation, suspension, or other surviving
  boundary;
- joining alternatives before the next consumer is selected;
- retaining a payload, identity, ownership obligation, or destruction
  lifetime;
- allowing later dynamic inspection, storage, or transfer;
- preserving state after the current stack activation can no longer hold it.

Materialization is component-wise. A handler may require a payload but not a
type tag because the incoming failure type is already known. Several failure
types may need a tag while none needs payload storage. Constructing a discarded
payload may still require evaluating its effectful inputs even when no aggregate
failure object remains.

Physical memory is not the criterion. A materialized value may be passed in a
register, while a direct operation may write memory. The criterion is
whether runtime execution must continue to carry the semantic distinction.

Nor is every generated C++ boundary authoritative. A published ABI or opaque
interop call is a real constraint. A private helper can potentially be
specialized, and an IIFE introduced only to spell a C++ value expression is a
lowering device. Such a device cannot, by itself, justify a source-shaped
carrier.

## Swift: an error result with control successors

Swift treats `throws` as part of a callable's effect rather than spelling the
source result as `Result`. Swift 6 typed throws can name one thrown error type
with `throws(E)`; code that needs several categories can use an enum or fall
back to the existential `any Error`.

Swift Intermediate Language makes the control interpretation explicit. A
throwing function type has a normal result and an error result. `try_apply` is
a terminator with separate normal and error destination blocks, and `throw`
transfers its operand to the caller's error destination. The error payload is
an SSA block argument before the target calling convention decides where it
physically lives.

At the ABI layer, Swift can use a dedicated error register on supported
platforms. Public ABI stability constrains published calls, while the compiler
retains freedom to choose conventions for internal calls. Typed and existential
throws may use different ABI forms because precise type knowledge changes the
available representation.

The important lesson is not Swift's particular register. It is that the IR
retains an error exit and payload without first requiring one general
success-or-failure object.

## Rust: recoverable failure as an ordinary value

Rust represents recoverable failure with the ordinary enum `Result<T, E>`.
The `?` operator uses Rust's `Try` protocol to produce the success output or
perform an early return after an allowed conversion. MIR is a control-flow
graph, and matching an enum generally becomes a discriminant test and successor
blocks. This is Rust's source-value model, not a compiler-stage requirement for
every language.

Optimization can scalarize a `Result`, eliminate a discriminant, inline a
callee, or forward a return value unchanged. Nevertheless, success-or-failure
is already a first-class source value. A program can store several results,
put them in a container, pass them without inspecting them, or match them much
later. The compiler must preserve those value semantics whenever they remain
observable.

Rust is therefore a useful model for a compact boundary carrier and visible
early propagation, but not evidence that every typed failure effect should be
defined as a carrier value.

Rust also separates recoverable `Result` failure from panic. Panic has its own
abort or unwind behavior and is not encoded in the `Result<T, E>` contract.

## C# and Kotlin: an open runtime exception world

C# exceptions are objects rooted at `System.Exception`. Callable types do not
declare a closed set of thrown types. The runtime searches catch clauses using
the exception's dynamic type and performs the required unwinding and `finally`
work. CoreCLR represents protected regions and handlers as exception-handling
metadata and runtime/JIT control structures rather than ordinary return
carriers.

Kotlin likewise treats exceptions as unchecked and rooted at `Throwable`.
Catch applicability depends on runtime type, while the physical mechanism is
platform-dependent. `@Throws` communicates an interoperation signature to
languages such as Java; it does not turn Kotlin exceptions into a closed
effect set. Kotlin's `Result<T>` is a separate library value that can contain
an arbitrary `Throwable`.

These systems can make the normal path independent of an explicit result tag,
but they gain that property through a managed runtime, exception objects,
unwind metadata, and an open dynamic channel. Without a static closed set, the
compiler has less authority to prove exhaustive handling or erase individual
failure alternatives.

Their async behavior exposes a useful boundary. A synchronous exception can
travel through the active call stack. Once asynchronous work outlives that
stack, C# records the exception in a faulted `Task` until observation. Kotlin
lowers suspendable functions through CPS and state machines whose continuation
objects preserve state needed after suspension. Temporal separation forces
state to persist even though the source still reads as sequential control.

## Zig: closed error sets with lightweight alternatives

Zig has error-set types, error unions, set merging, inferred error sets,
`catch`, and `try` propagation. This is structurally close to a closed typed
failure effect: the compiler knows which error names belong to a set, and a
subset can coerce to a superset.

Zig errors are lightweight named error codes rather than arbitrary
per-alternative payload values. That permits a compact representation but does
not directly answer how a source language should transport nominal failures
with different sizes, alignments, ownership, and destruction behavior.

Error-set inference also interacts with genericity and recursion policy. A
compiler that solves recursive failure sets as a least fixed point has a
different semantic and implementation foundation from one that restricts such
cycles.

## Static exceptions in C++ research

P0709, commonly associated with Herbception, separates recoverable failure
from today's dynamic exception machinery and explores a static alternate
return channel. P3166 later explores static exception specifications that
retain complete sets of possible exception types and leave room for register
or stack transport.

These proposals are valuable evidence that exception-like source control need
not imply today's C++ exception ABI. They are still C++ proposals with C++
compatibility constraints. They motivate source-to-C++ design questions but do
not define another language's semantics or require it to mirror a proposed C++
spelling.

## Realization choices for a source-to-C++ compiler

A closed failure set gives a compiler more information than an open exception
channel, but it does not select one C++ mechanism. Common choices include:

| Choice | Natural fit | Main cost |
| --- | --- | --- |
| Native C++ exceptions | Stack unwinding and exception-enabled ABI integration | Open runtime machinery and toolchain policy |
| Explicit return carrier | Portable, inspectable boundaries and no-exception builds | A result protocol at each surviving callable boundary |
| Direct local control plus boundary transport | Known handlers and statically selected local routes | More lowering logic and careful lifetime validation |
| Caller-provided result storage | Stable foreign or low-level ABI boundaries | Explicit initialization, destruction, and aliasing rules |

These choices may coexist at different boundaries. A compiler can use direct
control for a statically known local handler, a carrier for an ordinary call,
and a separately documented convention for a foreign ABI. The source model
constrains observable control and payload lifetime; it need not expose the
selected transport.

The implementation should preserve exact failure identity until every
consumer that needs it has made its decision. Path-sensitive analysis may use a
temporary graph, while generated control may remain structured. Neither choice
requires the analysis graph or a source-shaped failure object to persist.

## Failure across async boundaries

Suspension is a potential materialization boundary because the current stack
may disappear before execution resumes. State used only before the suspension
does not need to enter a frame. State whose value, ownership, or destruction is
needed after resumption must survive somewhere. The same applies to a failure
or cancellation completion delivered after the awaiting operation suspended.

This does not mean every high-level async construct requires a general task
object, heap allocation, or one universal outcome carrier. A synchronously
completed operation may continue through direct control. A fixed composition
may use a specialized frame. A genuinely suspended operation needs enough
persistent state to resume correctly, but the source contract need not expose
that representation.

A source language may define typed failure and cancellation as different
semantic channels even if a selected backend stores both in one completion
state. Backend co-location does not decide source identity.

## Evaluation questions

- Which function boundaries are externally fixed, and which private boundaries
  can be specialized?
- Can local catch dispatch preserve exact failure types without duplicating
  large handlers?
- Which payload uses require ownership transfer, destruction, or addressable
  storage?
- Which transport works in both exception-enabled and no-exception builds?
- Can identical propagation forward an existing representation safely?
- Which debug information is required when a semantic failure object is
  scalarized or eliminated?
- How do move-only or managed failure values change transport and lifetime
  planning?

## Sources

- Swift: [SE-0413 Typed Throws](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0413-typed-throws.md),
  [SIL instruction reference](https://github.com/swiftlang/swift/blob/main/docs/SIL/Instructions.md),
  and the [ABI stability manifesto](https://github.com/swiftlang/swift/blob/main/docs/ABIStabilityManifesto.md).
- Rust: the [`?` operator reference](https://doc.rust-lang.org/stable/reference/expressions/operator-expr.html#the-question-mark-operator),
  [`Try`](https://doc.rust-lang.org/std/ops/trait.Try.html),
  [MIR construction](https://rustc-dev-guide.rust-lang.org/mir/construction.html),
  and [type layout](https://doc.rust-lang.org/reference/type-layout.html).
- C# and .NET: the [C# exception specification](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/exceptions),
  [CoreCLR exception-handling ABI](https://github.com/dotnet/runtime/blob/main/docs/design/coreclr/botr/clr-abi.md),
  and [async exception behavior](https://learn.microsoft.com/en-us/dotnet/csharp/asynchronous-programming/).
- Kotlin: the [exception specification](https://kotlinlang.org/spec/exceptions.html),
  [coroutine CPS and state-machine specification](https://kotlinlang.org/spec/asynchronous-programming-with-coroutines.html),
  [Java checked-exception interoperation](https://kotlinlang.org/docs/java-to-kotlin-interop.html#checked-exceptions),
  and [`Result`](https://kotlinlang.org/api/core/kotlin-stdlib/kotlin/-result/).
- Zig: the language reference sections on
  [error sets and error unions](https://ziglang.org/documentation/master/#Errors).
- WG21: [P0709R4 Zero-overhead deterministic exceptions](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0709r4.pdf)
  and [P3166R0 Static exception specifications](https://www9.open-std.org/JTC1/SC22/WG21/docs/papers/2024/p3166r0.html).
