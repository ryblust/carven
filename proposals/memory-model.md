# 内存模型与共享状态

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Cross-thread values, shared state, data races, and happens-before
- **Depends on:** 当前保证陈述无前置；未来模型等待具体 ownership 或 cross-thread API

## Summary

本文负责 Carven 未来的跨线程内存模型：value movement 与 sharing、shared-state validity、data
race、synchronization relation，以及这些保证在 C++ source-fragment author、provider 或
export caller boundary 何处停止。
Async operation、thread API、atomic 与 lock 的具体语义分别由相邻 proposal 负责。

当前没有选定 coherent memory model。最近的可讨论问题，是永久文档是否应明确陈述 Carven
目前不提供跨线程 memory-model guarantee。更具体的模型继续等待 ownership、shared state
或真实 cross-thread API 提供可分析的 value 与 operation。

这些 gate 最初记录于 v0.1.0 milestone context；这不要求 v0.1.0 实现 thread 或 memory model，
只要求 ownership、dynamic value 与 cross-thread async 不在无意识中锁定相关语义。

| Slice | Maturity | Current frontier |
| --- | --- | --- |
| 当前保证边界 | Exploration | `OPEN-01` |
| Shared-state 与 data-race 边界 | Exploration | `OPEN-02` |
| 跨线程 value capability | Exploration | `OPEN-03` |
| Happens-before guarantee | Exploration | `OPEN-04` |

## Context

### 当前仓库事实

- 当前 grammar 没有 thread、atomic、lock、shared ownership 或 synchronization source form；
- 永久语言文档没有定义 Carven memory model，也不保证 data-race freedom、thread safety、
  happens-before、Send/Sync-like capability 或 cross-thread task lifetime；
- Write access 是 non-owning 且 **nonexclusive**：同一 mutable owner 可以在一次 call 中传给多个
  Write parameters；
- C++ source fragment、`import(cpp)` provider 与 `export(cpp)` caller 位于显式责任边界；
  越过 Carven 已建模边界的 control transfer、mutation、lifetime、concurrency 与
  undefined-behavior obligation 归相应 C++ author；
- downstream C++ toolchain 负责 target compilation 与 ABI validity，但 ordinary Carven
  observable semantics 不能从 generated C++ 的偶然行为推导。

“永久文档尚未给出保证”不能被读成一种隐含保证。

### Write 非独占的准确含义

现有 Write rule 排除一个具体推理：compiler 不能仅凭 `&`/Write 就把当前状态当作唯一 mutable
alias，因此不能直接复用 Rust `&mut` 式的 thread-exclusivity proof。

这不排除所有未来静态并发设计。未来仍可能引入并发专用 capability/type property，只允许特定
owner/atomic/lock type 跨线程，在显式 concurrent context 中加强 access contract，使用 runtime
synchronization，或把责任留在显式 C++ interoperation boundary。

### 当前不能推导的保证

- Generated C++ 可被跨线程调用，不等于 Carven 保证 thread safety；
- immutable binding 不使其递归 representation 自动可跨线程共享；
- Write nonexclusive 不表示 data race 合法或已有定义；
- C++ toolchain 接受 target 不定义 Carven memory-model behavior；
- failure carrier、callable view、string view 或 future operation 的 representation 可复制，不会仅凭
  copyability 获得 cross-thread lifetime contract；
- same-thread `async`/`await` 不表示 parallel execution 或 cross-thread sharing。

### 来自现有与相邻 authority 的约束

以下是当前永久文档或相邻 proposal 已经建立的边界，不是本文新接受的 memory-model decision：

- C++ interoperation 已有显式 provider/export-caller obligation boundary；threading 与
  synchronization 不自动成为例外，也不能静默改变 boundary type mapping 或 C++ consumer
  contract；
- 任何 reentrancy、thread safety 或 thread-compatible value shape 保证都必须由
  [C++ interoperation contract](../docs/semantics.md#c-interoperation) 显式定义；
- owner、borrowed/shared/null、erased storage、allocator、dynamic dispatch table 与
  failure/control carrier 会约束跨线程有效性；shared lifetime 不等于 synchronized access；
- future Send/Sync-like 或 atomic-operation capability 即使复用 generic capability model，也需要
  独立 semantic guarantee；evidence/coherence 不能代替 runtime synchronization；
- same-thread suspension 不建立 cross-thread sharing。只有 operation、frame、continuation、
  capture 或 completion 可以迁移时，[Async](async.md) 才消费本文的保证；
- [Threading](threading.md) 依赖本文的 data-race、value-capability 与 happens-before contract，
  但本文也可以由 non-async thread、channel、shared owner 或 C++ interoperation use case 激活。

## Goals and non-goals

### Goals

- 诚实陈述当前边界，不把 generated C++ accident 写成 language semantics；
- 记录未来 cross-thread value 与 shared-state model 必须关闭的问题；
- 除非显式 concurrent contract 加强，否则保持 Write 现有 nonexclusive 含义；
- 让 C++ source-fragment author、provider 与 export caller 成为 memory-model reasoning
  的显式边界；
- 要求未来 feature 以 source semantics、compiler facts、lowering、C++ interoperation、diagnostics、
  tests 与永久文档组成完整垂直切片。

### Non-goals

- 在本文设计 atomic type family、memory-order vocabulary、lock API 或 channel API；
- 设计 async operation lifetime、structured concurrency、scheduler 或 task frame；
- 当前决定 Send/Sync-like capability 的名字、derivation、orphan 或 coherence policy；
- 当前引入 static race detector、borrow checker 或 lifetime annotation；
- 默认承诺 generated function reentrant 或 thread-safe；
- 把未来 memory model 绑定到某一种 C++ library mechanism。

## Design

### 当前设计状态

**Maturity:** Exploration；尚未选定 Carven memory model。

`OPEN-01` 是当前唯一未阻塞的问题。`OPEN-02` 至 `OPEN-04` 保存未来必须关闭的 semantic
gate，但在 concrete ownership、value、sharing 或 synchronization operation 出现前保持 blocked。
Open entries 中的 candidate direction 不是已接受 behavior。

## Open decisions

**Next discussion:** `OPEN-01`

### OPEN-01 — 永久文档是否应明确当前没有 Carven memory model？

- **Status:** Active
- **Depends on:** None
- **Blocked by:** None
- **Activation condition:** Active now
- **Why it matters:** 用户与 integration author 需要区分“没有并发保证”和 generated C++ 的偶然行为。
- **Constraints:** 只描述当前事实，保持 Write 与既有 C++ responsibility boundary，不选择未来模型。
- **Options:** 当前 proposal 建议采用等价英文陈述：Carven 当前不定义跨线程 memory model，不提供
  thread creation、sharing、synchronization 或 atomic source form，也不证明 data-race freedom；
  integration 跨线程使用生成代码时，calling、sharing、synchronization、referent lifetime 与
  target data-race validity 归 integration author；source-fragment/provider/export-caller 内并发行为
  继续服从既有 boundary responsibility。该措辞尚未接受。
- **Closure condition:** 在 language/lowering 文档一致性审查后，明确接受或修订一份永久陈述。

### OPEN-02 — Future shared-state 与 data-race guarantee 从何处开始和停止？

- **Status:** Blocked
- **Depends on:** None
- **Blocked by:** 当前没有 owner/shared/null design 或真实 shared-state API 要求回答
- **Activation condition:** 此类 value 或 API 进入 active design
- **Why it matters:** 模型必须定义 ordinary Carven 中哪些 aliasing/synchronization pattern 合法，
  以及责任在哪里转交显式 C++ boundary。
- **Constraints:** Ordinary Write 继续 nonexclusive，除非显式新 contract 加强；opaque boundary
  responsibility 必须保持诚实。
- **Options:** 已知方向包括显式 type/capability、仅在 concurrent context 加强 access、runtime
  synchronization 或显式 C++ boundary responsibility；保证与成本均未选定。
- **Closure condition:** 用一个 concrete shared-state example 定义 ordinary-source guarantee、
  aliasing proof boundary 与准确的 source-fragment/provider/export-caller handoff。

### OPEN-03 — 哪些 values 可以跨线程移动或共享，由谁提供 evidence？

- **Status:** Blocked
- **Depends on:** None
- **Blocked by:** 相关 owner、borrowed、shared、dynamic 或 operation value 与首个 consumer 尚未稳定
- **Activation condition:** 某一 value 必须越过真实 thread boundary
- **Why it matters:** Representation copyability 与 lifetime 本身不足以建立 cross-thread validity。
- **Constraints:** 不得从 immutability、Write、generic conformance 或 generated-C++ layout
  静默推导 thread safety。
- **Options:** Capability 可以由 compiler derive、source impl 提供、C++ boundary assertion 提供，
  或在显式 coherence rule 下组合；没有方向已选定。
- **Closure condition:** 对 concrete value set 定义 movement、sharing、referent lifetime、
  evidence ownership、diagnostics 与 boundary assertion。

### OPEN-04 — 哪些 operations 建立 happens-before，并提供什么保证？

- **Status:** Blocked
- **Depends on:** `OPEN-02`、`OPEN-03` 与 concrete synchronization operation
- **Blocked by:** 当前没有 ordinary Carven atomic、lock、channel、thread completion 或
  cross-thread task completion operation
- **Activation condition:** 第一个此类 operation 进入 active design
- **Why it matters:** Data-race validity 与 visibility 需要显式 ordering guarantee，而不是 target accident。
- **Constraints:** Ordinary read/write、atomic、lock、channel、thread completion 与 task completion
  可以有不同 contract；target 不支持 lock-free 时不能静默改变 observable semantics。
- **Options:** 在第一个 operation 与 use case 选定前未知。
- **Closure condition:** 定义该 operation 的 synchronization relation、observable ordering、
  target lowering 与显式 C++ interoperation contract。

## Deferred work

None. 当前 inactive memory-model questions 保留为 blocked open decisions，因为它们是未来模型的必要
组成部分，不是可选的相邻 capability。

## Implementation

当前不能实施 memory-model feature。

`OPEN-01` 关闭后可以形成 documentation-only slice：把接受的当前保证陈述写入永久 language
documentation；该切片不增加 syntax、IR、runtime 或 tests。

未来任何 memory-model feature 都必须形成 complete vertical slice：source form、semantic
guarantee、diagnostics、SemanticProgram facts、target/runtime、source-fragment/provider/export-caller
boundary、tests 与永久文档同时闭合。实现必须由具体 movement、sharing 与 synchronization operation 驱动，而不是预造
Send/Sync、lock、atomic 或 race-analysis IR。

## Validation

`OPEN-01` 只需要文档一致性审查：

- 不与现有 Write/access、C++ boundary responsibility、callable-view lifetime 或 string-view lifetime 冲突；
- 不把 downstream C++ ownership 转成 source guarantee；
- 不在 proposal roadmap 承诺 release scope 或 delivery date；
- v0.1.0 ownership、dynamic-value 与 async 方向能够发现 Write/memory-model gates，而不把它们
  变成交付承诺。

未来 feature 还需要 source acceptance/rejection、diagnostics、happens-before/data-race edges、
generated C++ compile/link/run、C++ interoperation boundary 与相应 resource/cost evidence。

## References

- [Permanent language semantics](../docs/semantics.md)
- [Permanent backend documentation](../docs/backend.md)
- [Async proposal](async.md)
- [Threading proposal](threading.md)
- [Classes proposal](classes.md)
- [Generics proposal](generics.md)
- [C++ interoperation semantics](../docs/semantics.md#c-interoperation)
- [Proposal roadmap](roadmap.md)
