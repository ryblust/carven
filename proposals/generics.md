# 泛型与静态约束

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** Generic instances, static capabilities, coherence, and target-realization freedom
- **Depends on:** None for the accepted source semantics; generic
  `import(cpp)`/`export(cpp)` participation depends on the implemented concrete
  C++ boundary and a finite generic publication contract

## Summary

本文定义 Carven 泛型与静态约束的 source semantics。Type-generic syntax、
definition-site checking、local inference、`concept`/`impl`、associated type、
coherence、impl locality 与 generic instance identity 已经裁定，但尚未实现。

实现前仍需关闭两项设计：显式 C++ boundary function 如何进入 closed instance graph，以及
如何从语言级无限 instance expansion 中区分合法递归和 compiler resource limit。
C++ concrete declarations、templates 或混合表示继续是 lowering 选择，不构成 source
语义。

| Slice | Maturity | Current frontier |
| --- | --- | --- |
| Parametric generic core | Accepted | 实现仍等待 `OPEN-01`、`OPEN-02` |
| Static capability and evidence | Accepted | 等待 parametric core |
| Associated types and coherence | Accepted | 等待 capability implementation |
| Closed instance graph | Exploration | `OPEN-01` 因 generic import/export publication contract 尚未定义而 blocked |
| Target realization | Accepted freedom | 由完整 semantic facts 驱动 |
| Advanced generic facilities | Deferred | `DEFER-01` 至 `DEFER-11` |

## Context

### 当前实现

当前仓库没有 Carven generic declaration 或 static constraint：

- keyword 集合中没有 `concept`、`impl`、`Self` 或 `where`；它们目前是普通
  identifiers；
- top-level grammar 只有现有 concrete declarations 与 C++ source fragments，
  declaration name、named type 与 call 均没有 generic clause；
- `<`、`>` 与 `>>` 只属于当前 expression grammar；
- SemIRProgram nominal type 只记录 resolved declaration identity，call 也没有 type
  parameter、constraint、witness 或 instance representation；
- dependency graph 只包含 concrete declaration dependency；
- 当前 unit-local Target lowering 已能为 array、failure transport 与 runtime helper 生成 C++ template
  application，但那是 compiler-selected target mechanism，不是 source 泛型；
- compilation 是显式的 closed module batch，并已有 canonical module path、
  module-domain visibility 与 direct lookup 边界。

因此没有旧 generic source surface 需要兼容。实现应以完整 vertical slice 加入
grammar、AST、SemIRProgram、instance graph、diagnostics 与 lowering，而不是先放置未使用的
scaffolding。

### 问题与 semantic authority

泛型需要把一组 static type arguments 映射为确定的 Carven declaration 或 type：

```text
generic declaration + normalized arguments + required static facts
                              |
                              v
                  one resolved Carven instance
```

Generic body 的 name、call、member、operator、constraint、access 与 failure meaning
必须在 Carven analysis 中关闭，不能等待 C++ substitution、overload resolution、
ADL 或 SFINAE。反过来，语言也不应为了避免 C++ template 而承诺一种固定
monomorphization table、cache、artifact layout 或 generated spelling。

静态约束只在 body 需要非普遍 operation 时出现。仅保存、传递或返回 `T` 的代码
不应等待完整 capability system。

### 既有边界

- Ordinary Carven semantics 在 target lowering 前完成。
- Generic support 不自动引入 runtime descriptor、registry、erased carrier、
  allocation、initialization 或 indirect dispatch。
- Compile-time-only type facts在所有受影响 operation 与 representation 确定后可以擦除。
- Ordinary concrete、inspectable C++ 是默认 target 方向，但不是稳定 ABI 或
  generated spelling contract。

## Goals and non-goals

### Goals

- 表达 type-parameterized function、transparent struct 与 enum。
- 在 definition site 完整检查 generic body。
- 以 bounded local inference 得到唯一 type substitution。
- 使用一套 static capability/evidence model 表达额外 operation guarantees。
- 在 closed compilation 中形成有限、确定、canonical 的 concrete instance graph。
- 保持 C++ target mechanism 可替换且不参与 source meaning。

### Non-goals

- Const/value generics、generic lambda 或 type-level execution。
- Dynamic interface value、runtime type erasure 或自动 reflection。
- Package resolution、serialized generic body、binary-only distribution、
  stable ABI 或 persistent instance cache。
- C++ specialization、SFINAE、ADL、implicit candidate ranking 或第二套 template
  language。

## Design

### Definition-site contract

**Maturity:** Accepted semantics.

Generic declaration 只依赖 type parameter 的普遍规则与显式 capability 检查一次。
Body 中每个 operation 在 generic SemIRProgram 中已有确定 meaning；unused invalid body 也产生
diagnostic，错误不取决于第一个 caller。

Generic SemIRProgram 可以保存 parameterized types、resolved requirement operations 与 symbolic
associated projections，但不能保存 unresolved body 等待 application site 或 C++
substitution 重新解释。Application site 只完成 inference、argument validation、
constraint satisfaction、唯一 evidence selection 与 concrete instance formation。

### Parametric generic core

**Maturity:** Accepted semantics.

第一阶段 type parameters 覆盖 generic functions、transparent structs 与 enums：

```carven
fn identity<T>(value: T) -> T {
    return value;
}

struct Box<T> {
    value: T,
}
```

这类 declaration 仍需遵守 access、copy/Take、storage cycle、construction、
visibility 与 published audience closure。Unconstrained body 只能使用对任意合法 `T`
成立的 core operations；closed compilation 知道当前所有 applications，不会使以下
declaration 合法：

```carven
fn equal<T>(left: T, right: T) -> bool {
    return left == right;
}
```

如果 equality 不是 universal capability，body 必须声明对应 guarantee。`concept`/
`impl` 是 parametric core 上的 static capability layer，而不是 `Box<T>` 的前置条件。

### Type-generic syntax

**Maturity:** Accepted syntax and parsing contract.

Declaration 与 type application 都使用 name-adjacent `<...>`：

```carven
fn wrap<T>(value: T) -> Box<T> { ... }
struct Box<T> { value: T }

let box: Box<i32> = ...;
let inferred = wrap(1);
let explicit = wrap<i32>(1);
```

第一阶段 clause 非空，每一项都是 type，parameter names 唯一；comma-separated list
允许 trailing comma。Explicit function application 必须提供全部 arguments；省略
clause 才推断全部 arguments。Partial、placeholder、named arguments 与 const/value
parameters 不在本阶段。

Expression parser 仅按 tokens 决定：callee 后完整 `<...>` 紧接 `(...)` 固定为
generic-call shape，否则 `<` 进入 comparison grammar。Parser 不查询 symbol table，
semantic failure 后也不 fallback。因此 `a < b > (c)` 是 generic call；比较必须写成
`(a < b) > c`。

### Local type argument inference

**Maturity:** Accepted semantics.

省略 explicit clause 时，compiler 从 public signature、call arguments 与 expression
已有的 immediate expected result type 求唯一 substitution：

```carven
fn identity<T>(value: T) -> T { return value; }
fn make<T>() -> T { ... }

let first = identity(1);
let second: i64 = identity(1);
let third: i32 = make();
consume_i32(make());
```

Inference 不读取 body、已有 instances 或“哪个 type 恰好有 impl”，也不依赖 C++
deduction。Capability 在唯一 substitution 后只过滤 invalid application，不制造 type
candidates。缺少信息、冲突或不唯一时在 call site 诊断并要求 explicit arguments。

Solver 只需 bounded local unification，包括 `T = i32`、`Box<T> = Box<i32>`、
conflicting binding 与 recursive substitution。Expected type 可以沿 nested expression
的现有 local channel 传播；需要跨 declaration、statement、call site 或 instance graph
fixed point 的 source 必须增加 annotation。

### Unified static capability model

**Maturity:** Accepted semantics.

Generic body 使用非普遍 operation 时声明 static guarantee：

```carven
concept Comparable {
    fn less(left: Self, right: Self) -> bool;
}

fn choose<T: Comparable>(left: T, right: T) -> T { ... }
```

Carven 只有一套 capability requirement/evidence model。Compiler-derived property 与
source `impl` 共用 satisfaction path、diagnostics 与 resolved-operation representation；
区别只由 capability identity 规定谁能产生 evidence。Compiler-derived evidence 可以
视为 synthesized impl，但不建立第二套 solver 或 dispatch。

`concept` 是 compile-time contract，不是 runtime value、nominal value type、subtyping
或 vtable。Dynamic interface 保持独立 intent。

### Concept and impl declarations

**Maturity:** Accepted syntax and semantics.

`concept` 是具名 module declaration。`private concept`、bare `concept` 与
`export concept` 分别具有 Module、ModuleDomain 与 Compilation visibility。Identity
由 canonical module path 与 declaration identity 决定。Concept 只能进入 bounds、
impl head、projection 与 concept-qualified operation selection，不是 ordinary value type。

Requirements 是以 `;` 结束的 callable signatures；operation 与 associated names 在
一个 member space 内唯一且不 overload。Empty body 合法，表示 marker capability。

`impl` 是 anonymous semantic declaration，没有 source name、visibility modifier、
import、re-export 或 activation。合法 impl 一旦进入 compilation 就成为
compilation-wide canonical evidence。Body 必须恰好实现每项 requirement；missing、
extra、duplicate 与 substitution 后 signature mismatch 均诊断。

`concept` 与 `impl` 是 contextual keywords：parser 只在 top-level declaration position
及 `private`/`export` 后的 declaration position 识别，不加入 global keyword set，
也不查询 symbol table。

### Function-shaped concept operations

**Maturity:** Accepted semantics.

第一阶段 requirement 使用显式参数的 function shape，impl 精确实现但不注入 member
lookup：

```carven
concept Comparable {
    fn less(left: Self, right: Self) -> bool;
}

impl Comparable for Meter {
    fn less(left: Meter, right: Meter) -> bool { ... }
}

fn minimum<T: Comparable>(left: T, right: T) -> T {
    if Comparable::less(left, right) { ... }
}
```

Concept-qualified selection 以 resolved concept identity 为唯一 lookup anchor，不进行
ADL、free-function search，也不让 `left.less(right)` 在 generic 与 concrete context
获得不同 meaning。Parameter access、result 与 failure contract 都是 ordinary callable
facts，impl 在 `Self` substitution 后精确匹配。

### Contextual Self

**Maturity:** Accepted syntax and semantics.

`Self` 是 implementing type 的 contextual placeholder：

- 只在 concept 与对应 impl context 中有特殊 type meaning；
- concept requirement 中由 evidence target 决定；
- impl 中规范化为 target type；
- signature conformance 在 substitution 后检查；
- future value receiver 使用 `self`，与 type spelling `Self` 分工。

它不表示 runtime type、C++ pointer、implicit object address 或 nominal declaration，
也不加入 global keyword set。

### Conjunctive bounds

**Maturity:** Accepted syntax and semantics.

第一阶段 constraint 紧邻 type parameter，`+` 表示有限静态合取：

```carven
fn minimum<T: Equality + Comparable>(left: T, right: T) -> T { ... }

impl<T: Equality + Debug> Debug for Box<T> { ... }
```

Bound order 不影响 semantics，duplicate capability 诊断。Capability names 通过 ordinary
direct lookup 可见；local inference 先确定 substitution，再逐一验证 evidence。`+`
不建立 inheritance、subtyping、runtime composition 或 combined evidence identity。
第一阶段没有 `where`、disjunction、negation、refinement 或 general constraint logic。

### Minimal associated types

**Maturity:** Accepted semantics and syntax.

Associated type 表达由 implementing type 的唯一 canonical evidence 决定的 output：

```carven
concept Iterator {
    type Item;
}

impl<T> Iterator for VecIterator<T> {
    type Item = T;
}
```

Concept associated names 唯一；impl 必须恰好提供一份 definition。Definition 可引用
impl type parameters，但没有 associated parameters、default、bounds、const 或 general
equations。

Projection 在 application substitution 后、lowering 前函数式规范化。Cycle 或无法
normalization 是 Carven diagnostic，不能留给 C++ dependent types。外部唯一 normal form
是 `Concept::Associated<SelfType>`：

```carven
fn first<I: Iterator>(iterator: I) -> Option<Iterator::Item<I>> {
    return Iterator::next(&iterator);
}
```

不存在 `I::Item`、`<I as Concept>::Item` 或 `Concept<I>::Item` shorthand。

### Strong global coherence

**Maturity:** Accepted semantics.

一次 closed compilation 中，每组 resolved `(capability identity, concrete semantic
arguments)` 最多一份 applicable evidence。Compiler-derived 与 source evidence 在同一
coherence domain。

Conflicts 不因不同 modules、lexical imports、当前没有 application 或一份 unused 而合法。
可能同时匹配的 generic impl 也拒绝；第一阶段没有 specialization、priority 或
declaration-order tie-break。Capability satisfaction 不 import 或 activate impl。
需要多种策略的 API 应使用显式 strategy value/type，而不是 caller-selected hidden witness。

### Impl module domain and anchored head

**Maturity:** Accepted semantics.

Impl defining module 必须与 capability declaration，或 normalized target 的 outermost
nominal declaration，位于同一个 module domain；同时仍须通过 ordinary direct lookup
合法命名 external capability/type。

```carven
impl LocalCapability for ExternalType { ... } // capability domain: allowed
impl ExternalCapability for LocalType { ... } // nominal-head domain: allowed
impl ExternalCapability for ExternalType { ... } // neither: rejected
```

Parameterized target 的 head 是 normalized outer nominal constructor。Alias 不产生新
head；builtin、array 与 function 没有 source nominal head。

第一阶段 generic impl 必须有 concrete nominal head，可在 head arguments 中使用 bounded
type parameters：

```carven
impl<T: Debug> Debug for Box<T> { ... }
```

Bare parameter blanket impl 拒绝：

```carven
impl<T: Debug> Printable for T { ... }
```

Anchored head 让 candidates 先按 capability 与 nominal head 有限索引，再做 local
argument unification 与 bound validation。

### Generic instance identity

**Maturity:** Accepted semantics.

Concrete generic nominal identity 是：

```text
(generic declaration identity, normalized semantic arguments)
```

Declaration identity 包含 canonical module path；import alias、filesystem spelling、
generated C++ name 与 target representation 不改变 identity。Alias 先展开，associated
projection 先规范化，因此 projection 得到 `u8` 时，`Box<Projection>` 与 `Box<u8>`
是同一 semantic instance。

Callable application 使用同一 key。Canonical evidence 是 semantic/lowering dependency，
但不是 caller-selected hidden instance-identity argument。本规则不承诺 interning table、
hash、cache、single emitted text、template-id、mangled name 或 artifact location。

### Visibility and source composition

**Maturity:** Accepted boundary.

跨 module 使用 generic declaration 时，同一次 closed compilation 必须保留完成 checking
与 instance formation 所需的 resolved signature、constraints、body operations 或等价
semantic facts。本提案不选择其 private storage 或传输形式。

Witness 与 declaration identity 使用 canonical module path、module-domain visibility 与
direct lookup，不从 generated C++ 或 link result 恢复。

### Target realization freedom

**Maturity:** Accepted lowering freedom; the first concrete representation is
selected only after `OPEN-01` and `OPEN-02`.

Lowering 可以为实际 semantic instances 生成 ordinary concrete C++ declarations、使用
C++ templates 表示一组已完成 checking 的同构 instances、擦除已经不影响 operation 与
representation 的 compile-time distinctions，或混合这些方式控制 compile time、code size
与 artifact dependencies。

Generic parameters、normalized arguments、constraints、canonical evidence 与
instance selection 都是 Carven semantic inputs，不因此成为 target entities。
Concrete application 已经关闭后，这些结构可以只参与 checking、normalization 与
realization selection，随后在 generated C++ 中完全消失。没有 source requirement
规定每个 generic declaration、concept、impl 或 semantic instance 必须对应 C++
template、concept、witness object、descriptor 或独立 emitted type。

Source 不观察 monomorphization，且不会因 backend 选择获得 overload、specialization、
runtime metadata、dispatch 或 ABI guarantee。如果 generated C++ substitution 仍决定某个
Carven call 是否存在或选择哪个 implementation，semantic closure 尚未完成。

## Decision record

| ID | Decision | Design | Rationale |
| --- | --- | --- | --- |
| `GEN-01` | Generic bodies are checked at definition site. | [Definition-site contract](#definition-site-contract) | Keeps source meaning and diagnostics independent of callers and C++ substitution. |
| `GEN-02` | Type-parameterized functions, transparent structs, and enums form the parametric core. | [Parametric generic core](#parametric-generic-core) | Useful parametric code does not require capability dispatch. |
| `GEN-03` | Declarations and applications use non-empty `<...>` type clauses with token-only parsing. | [Type-generic syntax](#type-generic-syntax) | Gives one syntax without semantic parser fallback. |
| `GEN-04` | Omitted arguments use bounded local inference from signatures, arguments, and immediate expected type. | [Local type argument inference](#local-type-argument-inference) | Avoids global solving and acceptance instability. |
| `GEN-05` | Compiler and source evidence share one static capability model. | [Unified static capability model](#unified-static-capability-model) | Prevents duplicate constraint and dispatch systems. |
| `GEN-06` | `concept` is named; `impl` is anonymous compilation-wide canonical evidence. | [Concept and impl declarations](#concept-and-impl-declarations) | Separates visible contracts from non-activatable witnesses. |
| `GEN-07` | Initial concept operations are explicit function-shaped requirements selected through the concept. | [Function-shaped concept operations](#function-shaped-concept-operations) | Avoids ADL and context-dependent member lookup. |
| `GEN-08` | `Self` is a contextual implementing-type placeholder. | [Contextual Self](#contextual-self) | Expresses recursive signatures without importing C++ object semantics. |
| `GEN-09` | `+` in bounds is finite conjunction only. | [Conjunctive bounds](#conjunctive-bounds) | Meets current composition needs without a constraint logic language. |
| `GEN-10` | Minimal associated types normalize through unique evidence before lowering. | [Minimal associated types](#minimal-associated-types) | Supports capability-determined outputs without caller ambiguity. |
| `GEN-11` | Evidence obeys strong compilation-wide coherence. | [Strong global coherence](#strong-global-coherence) | Gives every obligation one stable meaning. |
| `GEN-12` | Source impls obey module-domain locality and require an anchored nominal head. | [Impl module domain and anchored head](#impl-module-domain-and-anchored-head) | Keeps candidate search and overlap finite and deterministic. |
| `GEN-13` | Generic instance identity uses declaration identity plus normalized semantic arguments. | [Generic instance identity](#generic-instance-identity) | Decouples Carven type identity from target representation. |
| `GEN-14` | Cross-module generic use preserves the resolved semantic facts required for checking and instance formation. | [Visibility and source composition](#visibility-and-source-composition) | Generic meaning cannot be reconstructed from generated C++ or link results. |
| `GEN-15` | Concrete declarations, C++ templates, erasure, and mixed target forms remain observationally equivalent lowering choices. | [Target realization freedom](#target-realization-freedom) | Source semantics must not lock the compiler to monomorphization or target templates. |

## Open decisions

**Next discussion:** None

### OPEN-01 — How do C++ boundary functions participate in generics?

- **Status:** Blocked
- **Depends on:** `GEN-01`, `GEN-04`, `GEN-10` through `GEN-14`
- **Blocked by:** The implemented C++ boundary accepts only concrete scalar
  declarations; generic instance publication is not defined.
- **Activation condition:** Representative generic `import(cpp)` or
  `export(cpp)` use requires a finite instance and symbol contract.
- **Why it matters:** C++ boundary participation must not create unrecorded
  applications, instances, conversions, or evidence outside the closed
  semantic graph.
- **Constraints:** C++ names, deduction, overload resolution, and substitution
  failure cannot complete Carven inference or constraints; every generic value
  crossing the boundary has normalized concrete Carven types and a finite typed
  callable contract.
- **Options:** Explicit instance lists, closed compilation-derived instances,
  or separately named generic provider/façade forms.
- **Closure condition:** Specify typed inbound and outbound examples, prove that
  every resulting application and evidence enters the instance graph, and
  reject open uses that cannot be accounted for.

### OPEN-02 — What finite-instance expansion rule is source semantics?

- **Status:** Blocked
- **Depends on:** `OPEN-01`, `GEN-13`
- **Blocked by:** `OPEN-01`
- **Activation condition:** Every external generic entry point is represented in
  the closed graph.
- **Why it matters:** The compiler must distinguish valid recursion, invalid
  by-value storage cycles, infinite argument growth, and implementation
  resource exhaustion.
- **Constraints:** The rule cannot delegate to C++ template-depth failure;
  semantic invalidity needs a stable Carven diagnostic and source anchor;
  compiler budgets are not language limits.
- **Options:** Unknown. Candidate rules must handle expansion such as `F<T>`,
  `F<Box<T>>`, `F<Box<Box<T>>>` without rejecting ordinary recursive calls or
  indirect type references.
- **Closure condition:** Define a decidable semantic rule, diagnostic anchor,
  and separate implementation budget, then validate them against recursive
  call, recursive type, finite mutual recursion, and growing-instance examples.

## Deferred work

### DEFER-01 — Const and value generic parameters

- **Reason deferred:** The first core has only type parameters; value arguments
  add kinds, inference, identity, equality, and evaluation rules.
- **Depends on:** Implemented parametric generic core
- **Reactivation condition:** A concrete API requires a compile-time value that
  cannot remain an ordinary runtime argument or type-level distinction.

### DEFER-02 — Generic lambdas

- **Reason deferred:** Generic closures add capture, callable identity,
  inference, and lifetime questions independently from named declarations.
- **Depends on:** Implemented generics and an owning-callable design
- **Reactivation condition:** A real higher-order API requires a locally
  declared polymorphic callable.

### DEFER-03 — Blanket impl

- **Reason deferred:** An unanchored target turns every obligation into global
  rule search and introduces proof termination and non-local overlap.
- **Depends on:** Implemented canonical evidence and coherence
- **Reactivation condition:** A real library use case supplies finite
  candidate, overlap, termination, and evolution rules.

### DEFER-04 — Specialization and candidate ranking

- **Reason deferred:** Multiple applicable evidence would replace strong
  coherence with priority and compatibility rules not needed by the first model.
- **Depends on:** Implemented canonical evidence and a dedicated specialization proposal
- **Reactivation condition:** A concrete API cannot use explicit strategy
  types and can define stable ordering and evolution behavior.

### DEFER-05 — Advanced associated items

- **Reason deferred:** Associated consts, defaults, bounds, and generic
  associated types exceed the first functional projection model.
- **Depends on:** Implemented minimal associated types
- **Reactivation condition:** A real capability requires one such item and can
  define normalization, conformance, and cycle behavior.

### DEFER-06 — Richer concept relationships and constraint logic

- **Reason deferred:** Default operations, refinement, disjunction, negation,
  and general equations exceed finite conjunctive bounds.
- **Depends on:** Implemented concepts and conjunctive bounds
- **Reactivation condition:** Repeated APIs expose a relationship the minimal
  model cannot state locally.

### DEFER-07 — Static meta and source generation

- **Reason deferred:** Stable generics must first reveal a bounded query and
  generation need; arbitrary text generation would create a second template
  language.
- **Depends on:** Implemented generics and a dedicated static-meta proposal
- **Reactivation condition:** A concrete consumer defines bounded inputs,
  outputs, semantic ownership, and diagnostics.

### DEFER-08 — Runtime reflection

- **Reason deferred:** Static evidence creates no runtime descriptor, registry,
  ownership, or cost contract.
- **Depends on:** A dedicated reflection proposal and implemented type semantics
- **Reactivation condition:** A runtime use case justifies explicit metadata,
  lifetime, lookup, and cost.

### DEFER-09 — Static capability to dynamic-interface bridging

- **Reason deferred:** The two models have different identity, representation,
  ownership, and dispatch costs.
- **Depends on:** Implemented static capabilities and dynamic interfaces
- **Reactivation condition:** Repeated APIs need the same contract in both
  worlds and can make conversion and cost explicit.

### DEFER-10 — Persistent generic semantic reuse

- **Reason deferred:** The first design composes source in one closed
  invocation and does not need serialized bodies or persistent instance caches.
- **Depends on:** Implemented generics and measured incremental/reuse needs
- **Reactivation condition:** Compilation measurements justify semantic
  serialization, cache identity, invalidation, and compatibility rules.

### DEFER-11 — Stable generic ABI and binary distribution

- **Reason deferred:** Semantic instance identity does not define mangling,
  artifact placement, stable ABI, or binary-only composition.
- **Depends on:** Implemented generics and a supported separate-compilation use case
- **Reactivation condition:** A distribution requirement needs generic
  compatibility across independently built artifacts.

## Implementation

Implementation is blocked by `OPEN-01` and `OPEN-02`. Once closed, delivery
proceeds vertically:

1. add parametric declaration/application syntax and SyntaxProgram/SemIRProgram facts,
   definition-site checking, local inference, normalized instance identity, and
   finite graph production;
2. add `concept`/`impl` parsing, visibility, canonical evidence, module-domain
   and anchored-head validation, coherence, qualified operations, `Self`,
   conjunctive bounds, and associated normalization;
3. select unit-local TargetUnit/C++ realizations only from published semantic facts;
4. deliver diagnostics, tests, generated C++ checks, and permanent documentation
   with each slice.

The compiler should not add unused generic condition fields, registries, target
templates, caches, or ABI scaffolding before a delivered slice needs them.

## Validation

Validation must cover:

- generic parameter/application parsing and the comparison, shift, member, and
  call ambiguity boundaries;
- definition-site versus application-site diagnostics and source anchors;
- local inference, expected types, explicit arguments, arity, duplicate names,
  conflicts, and underconstrained calls;
- allowed unconstrained operations, capability satisfaction, qualified calls,
  exact impl conformance, and unique evidence;
- impl visibility, module-domain locality, anchored heads, generic overlap, and
  coherence across modules;
- associated projection normalization, aliases, cycles, and instance identity;
- access, Take, failure, constants, storage cycles, evaluation order, and
  visibility under instantiation;
- C++ boundary instances and stable rejection of open/untracked applications;
- finite instance production versus semantic infinite expansion and separate
  compiler resource limits;
- C++20/C++23 compile, link, and run without fixing concrete/template target
  shape or private generated names.

## References

- [Carven semantics](../docs/semantics.md)
- [Carven compiler model](../docs/compiler.md)
- [Carven backend](../docs/backend.md)
- [Carven design principles](../docs/principles.md)
- [Classes and dynamic polymorphism](classes.md)
- [Operator capabilities](operators.md)
- [C++ interoperation semantics](../docs/semantics.md#c-interoperation)
- [Proposal roadmap](roadmap.md)
