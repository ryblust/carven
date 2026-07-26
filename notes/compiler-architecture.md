# Modern Compiler Architecture: Semantics, Analysis, and Lowering

Compiler architecture is not primarily a choice between trees, SSA, graphs, or
passes. It is the design of a sequence of program meanings: what each
representation knows, which questions are already closed, which distinctions
remain observable, and who is allowed to choose the runtime mechanism.

This article surveys production compilers and programming-languages research
with that question in mind. It is research context, not a recommendation to
copy another compiler's stage count.

## The recurring shape

Modern compilers differ substantially, but several patterns recur:

1. A source-faithful representation supports parsing, diagnostics, tooling, and
   language rules.
2. A resolved semantic representation gives every operation a precise type,
   identity, evaluation order, and control meaning.
3. One or more lowering representations make runtime control, values,
   ownership, and storage progressively explicit.
4. Target representations choose calling conventions, layouts, runtime
   protocols, instructions, and artifacts.
5. Each transition removes distinctions only after its producer has extracted
   the facts needed by later stages.

The important boundary is therefore not “high IR versus low IR.” It is:

> Which semantic distinctions are still useful, and which runtime obligations
> have become unavoidable?

An IR is successful when it is the right authority for the questions asked of
it. Keeping more information is not automatically better. Erasing it too early
forces a later stage to reconstruct meaning from a poorer representation;
retaining it without a consumer creates duplicate truths and broad interfaces.

## Production compiler comparison

| Compiler family | Relevant program forms | Architectural lesson | Important qualification |
| --- | --- | --- | --- |
| Swift | Type-checked AST, raw SIL, canonical SIL, lowered SIL, LLVM IR | Stage-specific SIL invariants make effects, ownership, values, and CFG edges mechanically checkable before those facts are lowered away | SIL solves Swift's ownership, ABI, and optimization problems; its exact instruction set is not a universal template |
| Rust | AST, HIR, temporary THIR, MIR, LLVM IR | Different downstream questions justify different representations; code generation consumes MIR rather than reinterpreting HIR | rustc's query system and incremental model address scale and tooling independently of MIR's semantic role |
| Kotlin | PSI, phased FIR, backend IR | Resolution phases establish ordered invariants, and stable symbols preserve declaration identity while representations are rebuilt | FIR is also designed for IDE and multiplatform workloads; those product needs are not universal |
| Roslyn | Full-fidelity syntax, symbols and semantic model, internal bound/lowered forms, IL | Source tooling and executable lowering need not share one representation or lifetime policy | Roslyn deliberately retains immutable compilations for a public compiler platform; that is a product requirement, not a default compiler rule |
| Carbon | Parse tree, SemIR, LLVM IR | Typed IDs, compact vector stores, canonical entities, verifiers, and deterministic IR dumps make semantic construction inspectable | Carbon remains experimental; its stage structure should be evaluated against its own language and backend goals |
| ClangIR and Flang HLFIR | AST, high-level dialects, progressively lower dialects, LLVM IR | A dedicated high-level IR can close the semantic gap that otherwise makes lowering contextual and causes premature temporary creation | Both are shaped by LLVM/MLIR and their source languages; the useful result is delayed loss of meaning, not mandatory MLIR adoption |
| Lean, GHC, and OCaml/Flambda | Typed/core forms, erased or closure-converted forms, explicit runtime IRs | Proofs, types, closures, and high-level calls can disappear or specialize before runtime machinery is introduced | Their functional cores and runtimes differ sharply from imperative native-language toolchains |

### Swift: different legality within one IR family

Swift's documented pipeline distinguishes raw SIL from canonical SIL and later
lowered forms. SIL retains Swift-level types and semantics while expressing
basic blocks, block arguments, calls, throwing calls, ownership, and lifetimes
explicitly. Mandatory passes establish canonical invariants before the normal
optimization pipeline.

Two details are especially instructive:

- `try_apply` is a terminator with separate normal and error successors. The
  error path is program control, not an annotation attached to an ordinary
  call.
- Ownership SSA makes borrow and ownership laws verifiable. Later ownership
  lowering deliberately removes that ability once the corresponding runtime
  operations have been selected.

Swift also represents suspension with explicit resume and error successors and
verifies lifetime restrictions around continuations. This illustrates a broad
principle: a non-local outcome should become an explicit program edge while the
compiler still has the semantic facts needed to validate it.

Primary sources:

- [Swift Intermediate Language](https://github.com/swiftlang/swift/blob/main/docs/SIL/SIL.md)
- [SIL ownership model](https://github.com/swiftlang/swift/blob/main/docs/SIL/Ownership.md)
- [SIL instruction set](https://github.com/swiftlang/swift/blob/main/docs/SIL/Instructions.md)

### Rust: use the representation suited to the next question

rustc lowers HIR bodies to THIR after type checking. THIR is fully typed and
desugared, supports checks such as exhaustiveness and unsafety, and can be
dropped body by body after MIR construction. MIR then becomes the explicit CFG
used by borrow checking, optimizations, monomorphization discovery, and code
generation. Code generation does not fall back to HIR to recover a missing
decision.

This is a useful counterexample to the idea that one rich IR should accumulate
every fact. Some information belongs to a short-lived producer representation;
the result needed downstream belongs to the next program.

rustc's memoized query architecture is a separate concern. It determines how
facts are requested, cached, and invalidated. It does not remove the need for
HIR, THIR, and MIR to have distinct semantic responsibilities.

Primary sources:

- [rustc overview](https://rustc-dev-guide.rust-lang.org/overview.html)
- [THIR](https://rustc-dev-guide.rust-lang.org/thir.html)
- [MIR construction](https://rustc-dev-guide.rust-lang.org/mir/construction.html)
- [rustc queries](https://rustc-dev-guide.rust-lang.org/query.html)

### Kotlin and Roslyn: frontend persistence follows product needs

Kotlin FIR is built through explicit resolution phases. A later phase can rely
on the invariants established by earlier phases, while symbols maintain stable
declaration identity when nodes are replaced. Diagnostics are produced from the
resolved representation rather than being interleaved indiscriminately with
every transformation.

Roslyn exposes full-fidelity immutable syntax trees, compilations, symbols, and
semantic models because IDEs, refactorings, analyzers, and incremental editing
are first-class consumers. Its architecture demonstrates why syntax and
semantic APIs can justifiably remain alive long after a batch compiler would
discard them.

The transferable lesson is conditional: persist a representation when a real
consumer owns that requirement. Batch code generation alone does not justify a
permanent source tree, semantic program, side graph, and lowering plan all being
available at once.

Primary sources:

- [Kotlin FIR basics](https://github.com/JetBrains/kotlin/blob/master/docs/fir/fir-basics.md)
- [Roslyn overview](https://github.com/dotnet/roslyn/blob/main/docs/wiki/Roslyn-Overview.md)

### Carbon: strong identity and inspectable semantic construction

Carbon's toolchain constructs SemIR while checking the program. Its
implementation uses compact vectorized stores and strongly typed IDs, separates
source locations from canonical entities, and treats formatted and raw SemIR
dumps as normal testing tools. Canonical types are identified once rather than
reconstructed by downstream consumers.

Its SemIR fidelity work also records a subtle risk: if a source rewrite has
semantic significance, immediately replacing it with a lower-level equivalent
can discard information needed for diagnostics, generic behavior, or later
transformations.

Primary sources:

- [Carbon toolchain architecture](https://docs.carbon-lang.dev/toolchain/docs/)
- [Carbon semantic checking and SemIR](https://docs.carbon-lang.dev/toolchain/docs/check/)
- [Carbon lowering](https://docs.carbon-lang.dev/toolchain/docs/lower.html)
- [SemIR fidelity proposal](https://docs.carbon-lang.dev/proposals/p003833-semir-fidelity-when-representing-rewrite-semantics.html)

### ClangIR and HLFIR: bridge semantic gaps deliberately

ClangIR introduces a high-level C/C++ representation because lowering directly
from Clang AST to LLVM IR loses source-language structure too abruptly. It can
preserve scopes and language-specific operations until dedicated lowering makes
them unnecessary.

Flang's HLFIR documents an even closer motivation: lowering high-level Fortran
expressions directly into FIR made translation contextual and encouraged early
temporary materialization. HLFIR represents variables and expressions
distinctly, delays bufferization and argument association, and later eliminates
its high-level operations through mandatory lowering.

The general result is not “every compiler needs another IR.” It is that a wide
semantic gap manifests as context-heavy lowering, premature storage, and
repeated source reasoning. A representation is warranted when it gives those
decisions a single, verifiable owner.

Primary sources:

- [ClangIR motivation](https://llvm.github.io/clangir/Development/motivation.html)
- [ClangIR pipeline](https://llvm.github.io/clangir/Development/pipeline.html)
- [Flang High-Level FIR](https://flang.llvm.org/docs/HighLevelFIR.html)

### MLIR: legality is more valuable than dialect machinery

MLIR models operations, values, blocks, and nested regions at multiple levels of
abstraction. Dialect conversion declares which operations are legal, illegal,
or dynamically legal at a destination, and conversion succeeds only when the
illegal source forms have been eliminated. Type conversion can map one source
type to zero, one, or several destination components.

These ideas transfer cleanly to a small compiler:

- define the legal operation set at every frozen boundary;
- make zero-to-many value and type projection explicit inside a conversion;
- publish only the destination program after complete legalization; and
- verify region signatures, successors, and ownership.

The general dialect registry, rewrite engine, and rollback machinery do not
follow from those principles. MLIR's own documentation notes that rollback mode
adds hidden state and makes debugging harder. A small statically typed compiler
can use explicit ordered builders and still obtain the legality discipline.

Primary sources:

- [MLIR language reference](https://mlir.llvm.org/docs/LangRef/)
- [MLIR dialect conversion](https://mlir.llvm.org/docs/DialectConversion/)
- [MLIR paper](https://arxiv.org/abs/2002.11054)

## Research themes

### Partial evaluation does not justify a persistent compiler stage

Partial evaluation specializes a program with respect to information already
known. Computation dependent only on static inputs is performed by the
specializer; only the remaining dynamic computation is generated.

This gives a precise basis for one optional optimization, but it must not be
confused with semantic closure:

- language-required type and constant computation may be evaluated;
- proof or constraint evidence may be erased after checking;
- static dispatch and pattern decisions may be committed;
- dynamic control, state, identity, and lifetime obligations must survive; and
- target lowering still owns concrete runtime mechanisms.

An erased source construct therefore does not imply an optimized-away runtime
operation. Some constructs existed only to establish a fact and never denoted a
runtime entity in the first place. A compiler needs a persistent specialization
stage only when a downstream consumer requires that distinct authority.

Primary sources:

- [Partial Evaluation and Automatic Program Generation](https://www.itu.dk/~sestoft/pebook/pebook.html)
- [Partial Evaluation, Whole-Program Compilation](https://arxiv.org/abs/2411.10559)

### Effects: semantic facts can select and erase protocols

Effect-handler compilation research demonstrates that one surface control
abstraction can be refined through several representations: explicit
continuations, evidence or capability passing, selective CPS, regions, and
ordinary target calls. Type-and-effect facts decide which paths need a general
mechanism and which can collapse to direct control.

The architectural lesson applies even when a language implements only typed
failure: effect inference is not itself a runtime carrier. It is evidence used
to construct exact control and boundary obligations. Once those decisions are
committed, the evidence can disappear.

Primary sources:

- [Type Directed Compilation of Row-Typed Algebraic Effects](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/12/algeff.pdf)
- [Generalized Evidence Passing for Effect Handlers](https://www.microsoft.com/en-us/research/wp-content/uploads/2021/08/genev-icfp21.pdf)
- [From Capabilities to Regions](https://doi.org/10.1145/3622831)
- [Zero-Overhead Lexical Effect Handlers](https://doi.org/10.1145/3763177)

### Proof erasure, ownership, and late runtime machinery

Lean erases propositions and other compile-time content before converting to
lower IRs, then makes allocation, reference counting, reuse, and runtime calls
explicit at later stages. Perceus begins from an explicit-control functional
core and inserts precise reference-count operations before reuse analysis.

Production functional compilers make related phase choices for other reasons.
GHC progressively lowers typed Core through STG to Cmm while exposing dumps and
sanity checks around those forms. OCaml lowers typed trees through Lambda and
closure-converted Flambda forms, where call-site information can drive
specialization before lower runtime code is selected.

Together with ownership-aware production IRs, this supports three distinctions:

- **reification:** a semantic entity must survive as runtime data or control;
- **materialization:** a reified distinction must persist as state across a
  boundary, join, lifetime, or later observation.
- **place formation:** materialized state needs an addressable logical location
  with mutation, identity, lifetime, or foreign-access obligations.

A reified action can remain a direct operation or control edge without
materialized state. Materialized state may remain an SSA value or block
argument rather than memory. Addressable **place formation** is a still narrower
decision: an addressable source binding does not prove that a C++ object must be
allocated, because analysis may represent it with values until address,
mutation identity, escape, lifetime, or foreign observability requires a place.

Primary sources:

- [The Lean 4 Theorem Prover and Programming Language](https://lean-lang.org/papers/lean4.pdf)
- [Perceus: Garbage Free Reference Counting with Reuse](https://www.microsoft.com/en-us/research/publication/perceus-garbage-free-reference-counting-with-reuse)
- [GHC debugging and intermediate-form dumps](https://ghc.gitlab.haskell.org/ghc/doc/users_guide/debugging.html)
- [OCaml compiler backend](https://ocaml.org/docs/compiler-backend)

### Verification is an architectural feature

CompCert structures compilation as a composition of passes with explicit source
and destination languages and semantic-preservation results. Alive2 takes a
different industrial approach: translation validation checks concrete LLVM IR
transformations and has exposed both compiler defects and specification
ambiguities.

Formal verification is not required to use the underlying lesson. Precise
stage semantics enable cheaper safeguards:

- structural and legality verifiers at every boundary;
- total accounting for each transformed operation;
- deterministic before/after dumps;
- negative verifier tests; and
- differential and property-based tests for high-risk legalizations.

Primary sources:

- [CompCert passes](https://compcert.org/doc/html/compcert.driver.Compiler.html)
- [Alive2: Bounded Translation Validation for LLVM](https://web.ist.utl.pt/nuno.lopes/pubs.php?id=alive2-pldi21)

### Small passes, small public surface

The nanopass approach demonstrates the clarity gained by giving each
transformation a narrow input and output grammar. Production compilers likewise
use mandatory legalization sequences inside broader public stages.

The two lessons should not be confused. Internally, a lowerer may use many
small transformations and temporary analyses. Externally, exposing every
temporary grammar as a compiler-wide program owner can make the compiler harder
to understand. A small public stage surface with explicit private legalizations
gets both benefits.

Primary source:

- [A Nanopass Framework for Compiler Education](https://www.cambridge.org/core/journals/journal-of-functional-programming/article/educational-pearl-a-nanopass-framework-for-compiler-education/1E378B9B451270AF6A155FA0C21C04A3)

## What the evidence does not establish

The surveyed systems do not support a universal prescription for:

- one exact number of IRs;
- SSA in every representation;
- a query-driven or pass-driven compiler;
- one operation class shared by all stages;
- target neutrality;
- persistent compiler state; or
- a general rewrite framework.

Those are workload and product decisions. The stronger common evidence is for
phase-specific meaning, explicit control and lifetime where needed, delayed
loss of useful semantics, mechanically enforced legalization, and downstream
independence from discarded program forms.

## Applying the evidence

A proposed representation should identify its downstream consumer, the facts
it makes authoritative, the distinctions it may erase, and the invariants that
can be verified without replaying its producer. The answers vary by language
and product. A representation earns its place by closing a distinct set of
questions, not by matching another compiler's pipeline.
