# 线程、同步与原子操作

- **Status:** Deferred — memory model 与真实 cross-thread API 提供具体 contract 后重新激活
- **Implementation:** Not started
- **Scope:** Thread lifecycle, blocking synchronization, atomics, and channels
- **Depends on:** [Memory model](memory-model.md)

## Summary

本文负责显式 multi-thread execution 与 synchronization：thread creation/shutdown、跨线程
callable/value admission、mutex/condition/semaphore、atomic 与 memory order、channel，以及
failure、cancellation 与 C++ thread interoperation。

整个领域当前保持 Deferred。Carven 没有对应 source form；在 memory model 之前设计 API，会让
value movement、data race 与 happens-before 缺少定义。Thread 可以独立于 async 存在；future
cross-thread async executor 则同时依赖本文与 memory model。

当前保存四个 inactive 方向：`DEFER-01` thread lifecycle、`DEFER-02` blocking synchronization、
`DEFER-03` atomic，以及 `DEFER-04` channel/message passing。它们都不是当前讨论 frontier。

## Context

当前 Carven source 没有 thread、mutex、condition、semaphore、channel、atomic 或 memory-order
form。Write access 是 nonexclusive，不能证明 unique mutable access；immutable binding 不使其
递归 representation 自动 thread-safe；generated C++ 被 C++ caller 跨线程使用也不产生 Carven
thread guarantee。Shared ownership 只解决 lifetime，同样不足以提供 synchronization。

本文依赖 [memory model](memory-model.md) 的 data-race validity、happens-before 与 cross-thread
value capability，但不依赖 async operation。[Async](async.md) 只有在 task、continuation、
capture 或 completion 可能迁移到另一 thread，或 blocking primitive 暴露于 async context 时，
才消费本文的保证。

## Goals and non-goals

### Goals

- 定义 accountable thread lifetime、join/shutdown 与 entry callable/capture admission；
- 为每一种 blocking/atomic operation 提供显式 synchronization 与 failure contract；
- 保持 shared lifetime 与 synchronized access 的区别；
- 先以最窄真实 cross-thread vertical slice 建立能力，再扩张 API family；
- 显式定义 C++ thread interoperation 与 target limitation，而不是从标准库可用性推导。

### Non-goals

- 在本文设计 `async fn`、`await`、operation frame 或 structured async ownership；
- 因 C++ 存在就复制完整 `std::thread`、`std::atomic` 或 synchronization API；
- 在 memory model 之前预造 Send/Sync-like、lock、atomic 或 channel semantic operations；
- 默认承诺 work stealing、parallel algorithm、actor runtime 或 production executor；
- 把 shared ownership 等同于 thread safety；
- 同时引入 mutex、atomic、channel 与 cross-thread async。

## Design

### 当前设计状态

**Maturity:** Deferred；尚未选择 coherent API 或 semantic design。

所有待保留问题位于 `DEFER-01` 至 `DEFER-04`。它们不是 active options 或 settled behavior。
重新激活时，应由 concrete program 选择第一个 slice，并消费而不是自行发明所需 memory-model
guarantee。

### Authority boundary

本文定义 ordinary Carven thread 与 synchronization semantics。C++ thread、callback、atomic 或
lock 只能在 source contract 固定后作为 lowering/C++ interoperation mechanism；target API 不能自动决定
ownership、failure propagation、cancellation、lock poisoning、memory-order default 或
blocking-in-async validity。

## Open decisions

**Next discussion:** None

当前没有 active question。所有方向都等待 memory model 与 concrete cross-thread use case，因此保留在
Deferred work。

## Deferred work

### DEFER-01 — Thread lifecycle

- **Reason deferred:** Entry-value capability、data-race validity 与 completion synchronization 尚未定义。
- **Depends on:** [Memory model](memory-model.md) 与真实 thread-creation use case
- **Reactivation condition:** 需要 scoped thread 或等价 cross-thread execution slice。

必须分别裁定：

- thread handle 是 owner、shared value 还是 scope-owned child；
- entry callable 与 captures 需要何种 cross-thread capability；
- join、detach、structured shutdown 与 process shutdown；
- typed failure 如何跨 thread boundary 传递；
- C++ thread-local state 与 callback obligation 如何被包含。

### DEFER-02 — Blocking synchronization

- **Reason deferred:** Guard/access interaction 与 happens-before 没有 owning memory-model contract。
- **Depends on:** [Memory model](memory-model.md) 与 concrete shared-state use case
- **Reactivation condition:** 程序真实需要 blocking mutex、condition、semaphore 或等价 primitive。

必须分别裁定：

- mutex guard 如何与 Read/Write access 组合；
- lock/unlock 建立什么 guarantee；
- poisoning、failure、cancellation 与 early return；
- condition wait 的 guard release/reacquisition、spurious wakeup 与 predicate contract；
- blocking operation 是否可用于 async context，以及如何诊断误用。

### DEFER-03 — Atomic

- **Reason deferred:** Payload admission、ordering vocabulary 与 target fallback 需要已裁定 memory model。
- **Depends on:** [Memory model](memory-model.md) 与 concrete atomic use case
- **Reactivation condition:** Shared-state design 需要一个无法由更窄 abstraction 表达的 atomic operation。

必须分别裁定：

- atomic 是显式 type family、capability 还是 intrinsic-operation family；
- 支持哪些 payload types；
- memory-order vocabulary 与 default；
- compare/exchange 的 success/failure ordering；
- target 无 lock-free implementation 时的 observable contract；
- C++ ABI 与 platform lowering。

### DEFER-04 — Channel 与 message passing

- **Reason deferred:** Capacity、movement、close 与 synchronization semantics 需要 concrete value
  capability 与 happens-before rule。
- **Depends on:** [Memory model](memory-model.md) 与 producer/consumer use case
- **Reactivation condition:** 真实程序需要 cross-thread communication；channel 可以作为首个窄切片，
  以减少 shared mutable state。

必须分别裁定：

- channel 是 library abstraction 还是 language/runtime primitive；
- send 是否移动 owner，receive 如何恢复 availability；
- bounded capacity、blocking、close 与 failure；
- channel 是否 awaitable，并与 async proposal 形成显式集成。

重新激活时，优先选择一个 scoped-thread 或 message-passing vertical slice；不要同时激活完整
mutex、atomic、channel 与 executor surface。

## Implementation

当前不能实施。它等待 [memory model](memory-model.md)、选定的第一个 cross-thread use case，以及
对应 `DEFER-*` reactivation condition。

未来切片必须连接 source form、value/callable admission、ownership/shutdown、
SemIRProgram facts、memory-model edges、target lowering、显式 C++ interoperation contract、
diagnostics、tests 与永久文档。
不应提前建立 generic Send/Sync-like、lock、atomic 或 channel representation。

## Validation

当前 feature 不需要 runtime validation。重新激活的 evidence 至少包括真实 source program，并证明：

- normal completion、failure 与 shutdown 下的 thread/captured-value lifetime；
- accepted/rejected cross-thread values 与精确 diagnostics；
- promised happens-before 与 data-race behavior；
- target fallback 与 C++ interoperation obligation；
- 没有 implicit detach、orphan work 或 shared-lifetime/synchronization 混淆；
- 若跨越 async boundary，blocking-in-async behavior 已定义；
- supported targets 上 generated C++ compile/link/run。

## References

- [Memory model proposal](memory-model.md)
- [Async proposal](async.md)
- [Classes proposal](classes.md)
- [Generics proposal](generics.md)
- [C++ interoperation semantics](../docs/semantics.md#c-interoperation)
- [Proposal roadmap](roadmap.md)
