# `struct`、`class` 形式与动态多态

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** Transparent data, ordinary classes, class forms, receivers, and dynamic abstraction
- **Depends on:** None for ordinary classes; dynamic generic operations depend
  on [Generics](generics.md), and cross-boundary calls depend on a typed
  `#[cpp]` contract

## Summary

本文在不把 C++ object model 当作 Carven semantics 的前提下，区分 transparent data
与 behavior-bearing abstraction。`struct`/ordinary `class` 的职责、class 封装与构造、
receiver access、static/dynamic boundary，以及 `class(form)` declaration axis 已经裁定。

Dynamic value ownership、conformance、erased type-use spelling 与第一个 concrete class
form 仍待讨论。C++ virtual-function 的限制不会自动排除 dynamic generic operation，
但 Carven 是否提供这项 source surface 仍是 open。Open-world plugin、stable ABI、
user-defined metaclass、inheritance、runtime reflection 与 managed object form 继续延后。

| Slice | Maturity | Current frontier |
| --- | --- | --- |
| `struct` and ordinary `class` | Accepted semantics | `OPEN-01` 打磨 operation visibility/helper spelling |
| Receiver access | Accepted semantics; working details | `OPEN-02` 处理 consuming decomposition |
| Static versus dynamic abstraction | Accepted boundary | Dynamic value design follows |
| `class(form)` declaration axis | Accepted syntax axis | `OPEN-04` 选择 concrete first form |
| Dynamic values and conformance | Exploration | `OPEN-03` 至 `OPEN-05` |
| Dynamic generic calls | Exploration; backend does not preclude them | `OPEN-06` |
| Open-world and metaclass capabilities | Deferred | `DEFER-01` 至 `DEFER-10` |
| Managed object class form | Deferred | `DEFER-11` |

## Context

### 问题

Carven 需要用不同 source intent 表达：

- 一组 published fields 组成的 named product；
- 由 operations 保护 representation 的 abstraction；
- caller 在运行期不知道 concrete type 的 value；
- 不隐藏 metaprogramming 或 runtime cost 的未来 constrained class transformation。

Static generics 与 `concept` 可以为静态已知 type 生成 direct call，但不能表示
heterogeneous collection、plugin、callback 或 service boundary 所需的 runtime-erased
value。

### Semantic constraints

Carven 把一个 closed source composition 转译成 C++，但 C++ inheritance、virtual
member、constructor、allocation 与 reference identity 只是 target mechanisms，不是
language defaults。Observable class semantics 必须先于这些 mechanism。

Read、Write、Take 已经描述 ordinary arguments 的 access。Receiver 必须复用同一套
ownership/availability rules。Opaque `#[cpp]` 不能通过偶然 generated identifier
制造未进入 semantic graph 的 dynamic generic call site。

下文标记 Accepted 的示例是尚未实现的 proposal decisions，不是当前实现保证。

## Goals and non-goals

### Goals

- 区分 transparent record 与 behavior-protected abstraction。
- 让 ordinary class 保持 value semantics，不隐含 allocation 或 virtual dispatch。
- 通过 Read、Write、Take 表达 receiver access。
- 区分 static capability satisfaction 与显式 runtime erasure。
- 为 compiler-defined class form 提供 declaration axis。
- 允许 backend 使用比 C++ virtual member 更丰富的表示，但不得改变 Carven behavior。

### Non-goals

- Concrete class inheritance、protected member 或固定 base layout。
- Implicit heap allocation、GC、shared ownership 或 nullable dynamic state。
- 默认 concept-to-dynamic conversion 或 dynamic-to-concrete downcast。
- 第一阶段的 user-defined class forms、arbitrary metaclass execution 或 runtime reflection。
- Stable plugin ABI、binary-only distribution 或 open-world discovery。
- 为 transparent `struct` 承诺 POD、trivial、standard layout 或 C ABI。

## Design

### Transparent `struct` and ordinary `class`

**Maturity:** Accepted semantics.

`struct` 是 transparent nominal product。Body 声明 stored fields，其 declaration
audience 下的 published surface 包含这些 fields：

- 没有 inherent method、private state、inheritance 或 class-form transformation；
- construction、field access 与 destruction 使用 aggregate rules；
- external `impl Concept for Struct` 不把 operation 加入 struct member namespace；
- copying、destruction、ownership 与 resource behavior 由 field semantics 递归组合，
  transparency 不承诺 trivial C++ representation。

Ordinary `class Name { ... }` 拥有 hidden representation、instance/associated
operations、encapsulation 与 invariant。它默认 static、non-inheriting，仍可作为 value；
声明 class 不隐含 heap allocation、reference identity、virtual dispatch 或 nullable state。

Ordinary class 与 struct 一样从 stored fields 递归组合 copyability、destruction、
ownership 与 resource behavior。第一阶段没有 user-defined copy/move hooks，也没有
class-specific lifetime system；encapsulation 不建立第二套 resource model。

因此差异是 semantic visibility 与 behavior，而不是“class 是 reference、struct 是 plain data”。

### Ordinary class encapsulation and construction

**Maturity:** Accepted semantics.

Class fields 只在 class body 内参与 lookup；同 module declaration 没有 privileged access。
Caller 看到 operations 与 observable type properties，而不是 representation。

外部 fieldwise aggregate construction 非法，也不生成 public all-fields constructor。
Class body 可用现有 `ClassName { field: value }` expression 组装 representation。
无 receiver 的 class function 是 `ClassName::name(...)` associated operation，并承担
named construction API：

```carven
class Money {
    cents: i64,

    fn from_cents(cents: i64) -> Money {
        return Money { cents: cents };
    }
}

let price = Money::from_cents(100);
```

Associated construction 是 ordinary call，复用 generics、constraints、failure、
evaluation 与 diagnostics。不引入 reserved `init`/`constructor` family；`Money()`
也不是 construction，因为 postfix parentheses 调用 value，而 `Money` 是 type。

```text
T { ... }          direct value construction
value(...)         callable invocation
Type::name(...)    associated operation selection and invocation
```

Backend 可以用 C++ constructor、factory function 或其他等价形式实现 associated
factory，不改变 source semantics。

### Receiver access

**Maturity:** Accepted Read/Write/Take contract and dot-call behavior;
operation visibility and consuming-decomposition details remain open.

Instance operation 有独立 receiver slot：

```carven
fn value(self) -> i64;             // Read receiver
fn increment(&self);               // Write receiver
fn into_value(&&self) -> i64;      // Take receiver
```

Dot call 提供 receiver，不重复 access marker：

```carven
counter.value();
counter.increment();
let value = counter.into_value();
```

Member resolution 后，compiler 检查 declared receiver contract。Write 要求 updatable
receiver place；Take 要求 complete owner 或 permitted temporary，并使原 binding
unavailable；Read 获取 read-only access。Static/dynamic operation 使用同一规则，不能
从 body 推断。

Consuming operation 支持 builder/resource-owner API：

```carven
class RequestBuilder {
    fn build(&&self) -> Request;
}

var builder = RequestBuilder::default();
let request = builder.build();
builder.set_url(url); // error: builder was Taken
```

Source 中不存在可继续调用的 moved-from class。需要取得 stored fields 的 consuming
operation 必须使用 `OPEN-02` 选择的 controlled whole-representation decomposition；
ordinary partial member Take 后仍可用的 class 被拒绝。

只有 receiver 省略 dot-call marker。其他 parameters、free functions 与 associated
functions 继续精确匹配：

```carven
fn merge(&self, &&other: Counter);
left.merge(&&right);

fn reset(&value: Counter);
reset(&counter);

Counter::combine(&left, &&right);
```

### Static and dynamic abstraction

**Maturity:** Accepted boundary.

`concept` 表达 compile-time constraint；dynamic interface value 是另一种 entity：

| Dimension | `concept` | Dynamic interface value |
| --- | --- | --- |
| Subject | Generic type parameter | Runtime value |
| Call | Definition-site operation，instance 后 direct | Erased runtime dispatch |
| Ordinary type | No | Yes，通过显式 holding form |
| Runtime representation | None | Data handle + dispatch information |
| Allocation and ownership | 不引入 | 必须显式设计 |
| Composition | Static conjunction | 独立 dynamic-contract design |

Concept 不会因出现在 value position 而成为 interface type。Concept `impl` 与 dynamic
conformance 是两个独立 facts；未来 bridge 必须显式，因为它改变 representation 与
cost。Static call 失败时不 fallback 到 runtime dispatch。

### Minimum dynamic contract

**Maturity:** Accepted constraints; concrete forms remain open.

任何 dynamic design 必须满足：

- contract 以 canonical module/declaration identity 形成 nominal identity；
- 明确列出 erased value 可调用的 operations；
- conformance 在 compile time 完整验证且 signatures 匹配；
- erased value 只暴露 contract，不暴露 unrelated concrete members；
- dynamic-call operands 各求值一次，并使用 contract failure signature；
- static resolution 不 fallback 到 vtable；
- allocation、ownership、mutability、nullability 与 lifetime 必须显式。

Backend 只有在 source ownership 固定后，才能选择 data pointer + operation table、
proxy-style storage 或其他等价表示。

### The `class(form)` declaration axis

**Maturity:** Accepted declaration axis; no concrete first form or erased
type-use spelling has been selected.

Ordinary class 使用最短 declaration；special class contract 在 declaration 上使用
parenthesized compiler-defined form：

```carven
class Money {
    cents: i64,
}

class(interface) Printer {
    fn print(text: str) -> void;
}
```

`interface` 只是示例，不是 accepted built-in form。`class(form)` 是 declaration
syntax，不是 call。Declaration identity 仍是 `Money`/`Printer`；borrow、ownership
或 erasure 是独立 type-use axis。

第一阶段只识别 compiler-defined form names。`class()` 非法；bare `class Name` 是
ordinary class，不是缺少 default argument。Parentheses 与 generic `<...>` 分开。

Declaration axis 不预选 `dyn`。未来设计可能写：

```carven
fn render(printer: dyn Printer, text: str) {
    printer.print(text);
}
```

该例只说明 axis 分离。`OPEN-03` 至 `OPEN-05` 分别拥有 dynamic value、conformance
与 type-use decisions。

### Dynamic generic operations

**Maturity:** Exploration. `CLS-07` 只记录 backend limitation 不会拒绝设计空间；
`OPEN-06` 决定是否加入任何 source surface。

Carven 不必把 dynamic dispatch 等同于 C++ virtual member。因此 C++ 不支持 virtual
function template，并不能替 Carven 决定未来 contract 能否包含 generic operation：

```carven
class(interface) Visitor {
    fn visit<T: Node>(value: T) -> void;
}
```

Spelling 未接受。如果未来选择 source contract，closed compilation 可以评估：

- 为 observed generic arguments 与 concrete receivers 生成 specialized thunks；
- argument static、receiver erased 时选择 per-argument table/slot；
- multiple erased axes 时使用 erased signature 或 finite dispatch matrix；
- 使用 proxy-style data handles 与 generated operation tables；
- receiver static 时 direct call、erased site indirect call；
- 不生成 global registry、reflection 或 unused instances。

这些只是 feasibility candidates，不是 selected lowering。任何 accepted design 都必须
让 signature、evaluation、failure、lifetime 与 diagnostics 独立于表示。Raw C++ 不能
通过 incidental generated names 添加 graph 之外的 generic dynamic calls；这类 call
需要 explicit finite typed adapter。

Open-world artifacts 需要独立 ABI/registration contract，继续 Deferred。

## Decision record

| ID | Decision | Design | Rationale |
| --- | --- | --- | --- |
| `CLS-01` | `struct` 是 transparent data；ordinary `class` 拥有 hidden representation 与 behavior，但不隐含 reference semantics。 | [Transparent `struct` and ordinary `class`](#transparent-struct-and-ordinary-class) | 让 representation intent 可见，不导入 C++ layout/allocation defaults。 |
| `CLS-02` | Class construction 使用 body 内 direct representation construction 与 receiverless associated operations；type name 不可调用。 | [Ordinary class encapsulation and construction](#ordinary-class-encapsulation-and-construction) | 保持 ordinary call/expression rules。 |
| `CLS-03` | Receiver 在 declaration 中携带 Read/Write/Take；dot call 不重复 marker。 | [Receiver access](#receiver-access) | 同时保留 explicit access 与 ergonomic call。 |
| `CLS-04` | Static concept 与 dynamic interface value 是独立 entities，不隐式 bridge/fallback。 | [Static and dynamic abstraction](#static-and-dynamic-abstraction) | 让 erasure、dispatch 与 ownership 成为显式选择。 |
| `CLS-05` | Dynamic contract nominal，且不能隐藏 allocation、ownership、nullability 或 lifetime。 | [Minimum dynamic contract](#minimum-dynamic-contract) | 防止 erasure 偷偷选择 runtime model。 |
| `CLS-06` | `class(form)` 选择 declaration contract，与 type-use ownership/erasure 分离。 | [The `class(form)` declaration axis](#the-classform-declaration-axis) | Ordinary class 保持简洁，use-site cost 独立可见。 |
| `CLS-07` | C++ virtual-member 限制不会自动排除 Carven dynamic generic operation 的设计空间。 | [Dynamic generic operations](#dynamic-generic-operations) | Source capability 由 Carven 用例决定；backend feasibility 不接受该 capability。 |

## Open decisions

**Next discussion:** `OPEN-01`

### OPEN-01 — What is the final operation visibility and helper surface?

- **Status:** Active
- **Depends on:** `CLS-01`, `CLS-02`, `CLS-03`
- **Blocked by:** None
- **Activation condition:** Active now
- **Why it matters:** Ordinary class 需要完整 source boundary，区分 public instance
  operation、associated operation 与 class-private helper。
- **Constraints:** Fields 保持 class-private；module peers 无 implicit privilege；
  receiver access 在 declaration 中保持显式。
- **Options:** Unknown；需要通过真实 invariant-preserving classes 确认最小 visibility。
- **Closure condition:** 用 construction、query、mutation 与 private helper APIs 比较
  candidates，并选择最小 syntax。

### OPEN-02 — How does a Take receiver decompose its whole representation?

- **Status:** Active
- **Depends on:** `CLS-03`
- **Blocked by:** None
- **Activation condition:** Active now
- **Why it matters:** Consuming builder/resource owner 需要受控访问 stored fields，又不能
  暴露可调用的 partially moved class。
- **Constraints:** `self` unavailable；ordinary member Take 不留下 usable partial
  object；field ownership/destruction deterministic。
- **Options:** Unknown；需要用 `build`、`finish`、`into_*` APIs 检查 pattern forms。
- **Closure condition:** 选择 whole-representation pattern，并定义所有 field paths 的
  availability、destruction 与 diagnostics。

### OPEN-03 — Which dynamic ownership and value forms exist?

- **Status:** Active
- **Depends on:** `CLS-04`, `CLS-05`
- **Blocked by:** None
- **Activation condition:** Active now
- **Why it matters:** Dynamic interface 是真实 value，不能从 type erasure 偷得 lifetime/storage；
  本文拥有 dynamic-value ownership/lifetime contract。
- **Constraints:** Allocation/sharing 显式；Read/Write/Take 与 forms 组合；nullable state
  不隐含。
- **Options:** Borrowed 与 owned 是已知用例 candidates；shared、nullable、mutable 与
  inline-storage 需要 evidence。
- **Closure condition:** 用 callback、heterogeneous-container 与 service APIs 选择最小
  holding forms 及 lifetime rules。

### OPEN-04 — What is the first concrete class form and conformance syntax?

- **Status:** Blocked
- **Depends on:** `CLS-06`, `OPEN-03`
- **Blocked by:** `OPEN-03`
- **Activation condition:** Dynamic values 已有 ownership/lifetime model。
- **Why it matters:** `class(form)` 只有在至少一个 form 有 observable contract 且 concrete
  type 可 conform 后才是完整能力。
- **Constraints:** 初始 form names compiler-defined；conformance 与 concept evidence
  分离；不隐含 allocation/inheritance。
- **Options:** `interface` 是 leading first-form candidate；conformance spelling 未知。
- **Closure condition:** 定义完整 contract declaration、concrete conformance、
  missing/extra/signature diagnostics 与一次 static-to-erased construction。

### OPEN-05 — How does a type use request an erased dynamic value?

- **Status:** Blocked
- **Depends on:** `OPEN-03`, `OPEN-04`
- **Blocked by:** `OPEN-03`, `OPEN-04`
- **Activation condition:** Holding forms 与 first dynamic contract 已存在。
- **Why it matters:** Erase、borrow 或 own 的 runtime cost/ownership 必须在 use-site 可见。
- **Constraints:** Spelling 不得把 concept 变成 ordinary type，也不合并 declaration form
  与 use-site ownership。
- **Options:** Dedicated `dyn Contract`、ownership-derived forms，或经真实 APIs 验证的
  其他 explicit spelling。
- **Closure condition:** 比较 parameter、result、local、field、container 中 borrowed/owned
  values，并选择 unambiguous model。

### OPEN-06 — Which generic dynamic-operation surface enters the first slice?

- **Status:** Blocked
- **Depends on:** `CLS-07`, `OPEN-04`, `OPEN-05`
- **Blocked by:** `OPEN-04`, `OPEN-05`
- **Activation condition:** Dynamic contract 与 erased type-use model 已选定。
- **Why it matters:** Backend feasibility 不足以构成 feature；source capability 需要
  signatures、diagnostics 与 finite instance accounting。
- **Constraints:** Generic body definition-site checked；每个 erased call 在 analyzed
  graph 中；observable behavior 独立于 dispatch representation。
- **Options:** A — 不提供 generic dynamic operations；B — 先支持 static generic
  argument + erased receiver；C — 只有真实用例才加入 multiple erased axes。
- **Closure condition:** 规定一个完整 generic contract operation、conformance、call、
  finite instance set、diagnostic matrix 与 backend-neutral behavior。

## Deferred work

### DEFER-01 — Interface composition

- **Reason deferred:** Minimal dynamic contract/ownership 尚未存在，composition 没有稳定
  identity 或 conflict rules。
- **Depends on:** `OPEN-03` through `OPEN-05`
- **Reactivation condition:** 真实 API 需要一个 erased value 暴露多个 contracts，并能定义
  nominal identity 与 operation conflicts。

### DEFER-02 — Default dynamic implementations

- **Reason deferred:** Default 需要 settled conformance/override model。
- **Depends on:** `OPEN-04`
- **Reactivation condition:** 多个 conformances 重复 behavior，且共享不会隐藏
  representation、failure 或 dispatch cost。

### DEFER-03 — Explicit dynamic downcast

- **Reason deferred:** Concrete recovery 需要 runtime identity、failure、ownership 与
  lifetime behavior，而 minimal dispatch 不需要。
- **Depends on:** `OPEN-03` through `OPEN-05`
- **Reactivation condition:** 真实 API 必须在 erasure 后恢复 concrete type，并能说明
  checked failure path。

### DEFER-04 — Explicit bridge between concepts and dynamic contracts

- **Reason deferred:** Automatic bridge 会隐藏 representation/runtime cost，且尚无真实用例。
- **Depends on:** Implemented concepts and dynamic contracts
- **Reactivation condition:** 重复 APIs 需要同一 contract 的 static/erased forms，并能让
  conversion 显式。

### DEFER-05 — Open-world dynamic generic operations and plugins

- **Reason deferred:** Closed compilation 可枚举 calls；independent artifacts 需要 stable
  erased ABI、registration 与 instance-extension rules。
- **Depends on:** `OPEN-06` and a stable artifact/plugin proposal
- **Reactivation condition:** Supported plugin/separate distribution 需要 external generic
  instances。

### DEFER-06 — User-defined class forms

- **Reason deferred:** 需要先从多个 compiler-defined forms 获得稳定 extension model。
- **Depends on:** Multiple successful compiler-defined class forms
- **Reactivation condition:** 重复 class contracts 证明用户需要 bounded declarative form。

### DEFER-07 — Metaclass transformation

- **Reason deferred:** Arbitrary generation 没有 bounded transformation、observable
  contract 或 execution/diagnostic boundary。
- **Depends on:** Implemented class forms and a dedicated static-meta model
- **Reactivation condition:** 具体 transformation 无法由 ordinary class form 表达，且能
  给出 bounded inputs/outputs。

### DEFER-08 — Concrete inheritance and protected representation

- **Reason deferred:** Dynamic dispatch/reuse 不要求 C++ base-object model，当前也没有
  用例证明 layout/lifetime complexity 合理。
- **Depends on:** Stable ordinary and dynamic class semantics
- **Reactivation condition:** 真实用例无法由 composition、static capabilities 或 dynamic
  contracts 表达。

### DEFER-09 — Stable C++ ABI

- **Reason deferred:** Closed source composition 没有为 dynamic values 定义 layout、
  ownership、calling convention 或 binary compatibility。
- **Depends on:** Dynamic value implementation and a dedicated ABI/interop proposal
- **Reactivation condition:** Supported external consumer 需要 stable layout、call、
  lifetime responsibility 与 compatibility。

### DEFER-10 — Runtime reflection

- **Reason deferred:** Dynamic dispatch 不需要 general metadata registry、type discovery
  或 reflective mutation。
- **Depends on:** Implemented class/dynamic semantics and a reflection proposal
- **Reactivation condition:** Concrete runtime consumer 证明 explicit metadata、ownership、
  lookup 与 cost 合理。

### DEFER-11 — Managed object class form

- **Reason deferred:** `managed` 或 `gc` 名称本身没有定义 reference identity、automatic
  reclamation、cycles、nullability、reclamation timing、finalization、weak references、
  relocation、pinning 或 C++ boundary behavior。Tracing 所需的 layout metadata 也不等于
  `DEFER-10` 的 user-visible runtime reflection。
- **Depends on:** `CLS-06`, settled class ownership and lifetime semantics, and the memory
  model if managed references may cross threads
- **Reactivation condition:** Concrete APIs require opt-in reference identity and automatic
  lifetime that ordinary value or explicit owner/shared forms cannot express, and can state
  the observable guarantees, runtime participation, and interoperability cost.

## Implementation

Accepted ordinary-class slice 在 `OPEN-01`/`OPEN-02` 关闭后可垂直实现：grammar/syntax、
field visibility、associated/instance lookup、receiver access、availability、diagnostics、
semantic IR、lowering 与 permanent docs 必须一起落地。

Dynamic implementation 等待 `OPEN-03` 至 `OPEN-05`，顺序是 contract/conformance、
explicit erased construction、call 与 lifetime。只有 `OPEN-06` 的 optional generic
slice 依赖 generics；只有跨 C++ boundary 的切片依赖 typed `#[cpp]` contract。

Private lowering 只能在 Carven facts 固定后选择 C++ values、factories、operation tables、
specialized thunks、proxy-style storage 或 direct/indirect mixed calls。

## Validation

Ordinary-class slice 必须覆盖：

- transparent struct aggregate/field access 与 class representation privacy；
- in-body direct construction 与 external associated factories；
- 拒绝 `Type()` construction，且不存在 implicit constructors；
- Read/Write/Take receiver、exactly-once evaluation、use-after-Take 与 whole
  representation decomposition；
- class/module boundary 的 visibility/helper diagnostics；
- C++20/C++23 compile/link/run，不固定 constructor/layout strategy。

Dynamic slice 还要验证 nominal conformance、missing/extra/signature diagnostics、
explicit ownership/allocation、referent lifetime、copy/mutation/nullability、exactly-once
calls、typed failure、direct/erased equivalence、finite generic dispatch，以及拒绝
untracked `#[cpp]` entry points。

## References

- [泛型与静态约束](generics.md)
- [Proposal roadmap](roadmap.md)
- [Carven semantics](../docs/semantics.md)
- [Carven compiler model](../docs/compiler.md)
- [Carven backend](../docs/backend.md)
