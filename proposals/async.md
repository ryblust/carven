# 异步编程

- **Status:** Draft
- **Implementation:** Not started
- **Scope:** Async user semantics, compiler facts, lowering, and backend/interop research
- **Depends on:** None for the same-thread core; [memory model](memory-model.md) and
  [threading](threading.md) for cross-thread execution

## Summary

本文是 Carven async 领域的临时设计 authority。它统一记录用户侧语义、compiler-owned facts、
尚待关闭的问题，以及 lowering/backend research。它不描述当前实现，也不承诺 grammar、SemanticProgram、
coroutine lowering、runtime、scheduler 或 `#[cpp]` adapter 已经存在；当前行为仍以代码、测试与
`docs/` 永久文档为准。

| Slice | Maturity | Current frontier |
| --- | --- | --- |
| Phase-one semantic contract | Accepted | Operation、completion、structured ownership、组合器与明确的 phase-one exclusions |
| Source surface | Working spelling | 名称与精确 grammar 可打磨，但不能改变已接受语义 |
| Execution context | Exploration | `OPEN-01` |
| Suspension、borrow 与 frame | Exploration | `OPEN-02` |
| Compiler admission | Draft | 等待 `OPEN-01` 与 `OPEN-02` |
| Future async capabilities | Deferred | `DEFER-01` 至 `DEFER-07` |
| Backend evidence collection | Non-normative research | 可并行继续；不能选择 source semantics 或 provider |
| Lowering、provider 与 public `#[cpp]` adapter | Deferred selection | `DEFER-08` |

Same-thread async 是当前 proposal 的独立基础切片。Operation、frame、capture 或 completion 一旦允许
迁移到另一 thread，才依赖独立的 memory-model 与 threading contracts。

## Context

当前 grammar 没有 async/coroutine、task、thread、atomic、lock 或共享所有权 source form；当前语言
也没有 operation lifetime、scheduler、data-race freedom、Send/Sync 或 cross-thread continuation
guarantee。Generated C++ 可以被其他 C++ code 以并发方式使用，不等于 ordinary Carven source 已经
获得这些保证。

Same-thread suspension 不要求跨线程 memory model，也不隐含 thread blocking、thread creation、
thread-pool submission 或 physical parallelism。Thread 可以在没有 async 的情况下存在；async 也可以
只运行于一个 logical thread 或 event loop。Cross-thread executor、blocking operation in async context
与 awaitable channel 是显式集成点，不能把一个领域的 mechanism 自动提升为另一个领域的 semantics。

Backend/interop 是单向下游 consumer。C++ coroutine、sender、runtime task 或 provider 必须承载本文
冻结的 Carven facts，不能因为其支持 exception、detach、thread migration 或 shared task 就自动开放
相应 source capability。网络、文件、timer、DNS 与 TLS 属于后续 I/O/provider 产品面；真实用例出现
前不进入 async core，也不预留 source syntax。

本文使用四种局部成熟度：

- **Accepted**：后续讨论可以依赖；若要推翻，必须显式重开对应 decision。
- **Working spelling**：语义已接受，但名称或精确 grammar 仍可打磨。
- **Exploration**：问题已经成形，但还没有可依赖的 coherent answer。
- **Deferred**：第一阶段不设计或不开放，不能被 implementation 顺手引入。

### Change discipline

- 后续讨论引用 decision ID（例如 `ALL-04`），避免只引用易漂移的示例或聊天措辞；
- 已接受决策不得被静默改写或删除。若重开后改变结论，保留原 decision，并标记它被哪个新
  decision supersede；
- working spelling 的调整不能暗中改变 completion、ownership、lifetime 或 failure semantics；
- Deferred 表示第一阶段明确不开放，不表示永久拒绝；重新讨论时必须先补齐用例与影响轴；
- 进入实现前，把相关 decision IDs 映射到 grammar/SemanticProgram/diagnostics/tests；进入永久语言保证后，再
  链接对应 `docs/` authority。

## Goals and non-goals

### Goals

- 为 cold operation、`await`、lexical child、cancellation、scope closure 与固定规模组合器建立一套
  可独立于 backend 的 Carven observable semantics；
- 让用户侧结构保持现代、简洁且符合 structured ownership，同时允许 compiler 根据已知 intent
  专门化生成代码；
- 在实现前关闭 execution context、suspension lifetime 与 compiler admission facts；
- 为未来 C++ coroutine、`std::execution` 与 provider adapter 保留受 source contract 约束的空间。

### Non-goals

- 把 stdexec、async_simple、libcoro、ASIO、Yalanting 或某个 runtime 变成 source semantics；
- 第一阶段提供 cross-thread executor、dynamic task group、stream/select、shared task、detach 或
  supervisor；
- 预先设计 network、filesystem、timer、DNS、TLS 或完整 production scheduler API；
- 用 structured cancellation 自动回滚数据库写入、网络发送、支付或其他外部副作用；
- 固定 private generated C++ spelling、heap allocation 次数或第三方 library representation；
- 把本文的设计状态写成 release scope、交付日期或当前实现保证。

## Design

### Semantic authority and cost model

**Maturity:** Accepted.

本节记录 semantic leverage 在 async 领域产生的具体约束，不另行定义项目通用先验。

#### METHOD-01 — Carven owns the semantics

**Accepted.**

Carven source semantics 由 compiler/SemanticProgram 拥有，不由某个 C++ async library、coroutine promise、
sender concept、runtime task type 或 template substitution 反向定义。

`std::execution`、stdexec、async_simple、libcoro、ASIO 等可以是思想来源、adapter 或 lowering
mechanism，但不是 ordinary Carven source vocabulary，也不是 source validity 的第二条路径。

#### METHOD-02 — No abstraction tax applies after intent analysis

**Accepted.**

用户写下的高层结构只承诺 source-observable semantics，不承诺对应的通用 runtime object：

```text
Carven intent and facts
    -> specialized C++ state machine
    -> flattened frame/storage
    -> backend-native cancellation/deadline
    -> std::execution adapter
    -> or another observationally equivalent form
```

如果 compiler 已经知道固定 child 数量、具体 value/failure types、ownership tree、cancellation
authority、context 与 lifetime facts，就可以消除 heap allocation、type erasure、generic task wrapper、
tuple materialization、stop source 或 join state。Source 组合器不能因为门面高级就强迫重量级 runtime。

`await` 建立 potential suspension boundary，但只有跨越实际可达 suspension 后仍需使用的 value、
failure、cancellation、ownership、destruction 与 resume state 才需要 persistent runtime
representation。Source form 本身不要求 general task object、heap allocation 或统一 completion carrier；
backend 仍须为真正发生的 suspension 保存足以正确 resume 的最小状态。Compiler 为 C++ expression
spelling 引入的 IIFE 也不是 source semantic boundary，不能反过来证明 carrier 必要。

#### METHOD-03 — Mechanism correctness and use correctness are separate

**Accepted.**

语言机制负责 exactly-once completion、ownership、lifetime closure、cancellation propagation、
result typing 与安全销毁。程序员仍负责判断 operations 是否真的独立、是否允许并发、是否有业务
优先级，以及 cancellation 是否能容忍已经发生的外部副作用。

结构化取消不会自动回滚数据库写入、网络发送、支付或其他外部行为。

### Vocabulary

**Maturity:** Accepted.

#### TERM-01 — Operation

async callable invocation 产生的 cold、owning、single-consumer value。Operation 描述工作，但在被
`await`、`async let` 或组合器启动前不执行工作。

`Task` 只在泛指其他生态或 runtime representation 时使用；Carven source 设计优先使用
`operation` 与 `child`，避免把 source value 绑定到某个 Task class。

#### TERM-02 — Child

已经由 lexical owner 启动并纳入 structured lifetime 的 operation。`async let` 与固定规模组合器
可以建立 child relationship。

#### TERM-03 — Completion

```text
completion<T, E> = value(T) | failure(E) | cancelled
```

`failure(E)` 是 nominal typed failure；`cancelled` 是独立 completion channel，不属于 failure set。

#### TERM-04 — Cancellation request

request 是 cooperative signal，不是 completion：

```text
request cancellation != operation completed as cancelled
```

Operation 可以在 request 到达前已经完成、暂时不能响应、忽略 request，或在安全点接受 request
并最终完成为 `cancelled`。

#### TERM-05 — Observation

用户是否消费 child 的 value/failure。放弃 observation 不会放弃 lifetime responsibility。

#### TERM-06 — Lifetime-closed

Operation 满足以下永久 postcondition：

- 不会再有 execution agent、callback、continuation 或 completion 访问 operation state；
- 不会再访问从 owner 借用的 frame、local、buffer 或其他 storage；
- 所有可能恢复 operation 的外部注册已经撤销或完成；
- lifetime-relevant synchronization 已完成；
- operation state 现在可以同步销毁而不产生阻塞、data race 或 use-after-free。

正常 completion 后的 closure、cancel 后等待、backend 保证的同步撤销，以及 compiler proof 都可以
满足该 postcondition。`join` 是常见 closure mechanism，不要求存在某个 `join()` runtime call。

#### TERM-07 — Accountable lifetime owner

负责确保 operation 最终 lifetime-close 的唯一 owner。Observer、borrow、cancellation requester 或
内部 reference 不因此成为 lifetime owner。

### Single async operation

**Maturity:** Accepted.

#### TASK-01 — Async invocation is cold

**Accepted.**

调用 async callable 只构造 cold operation；function body 不开始执行。

```carven
let pending = fetch_user(id); // cold
```

#### TASK-02 — `await` starts in the current logical task

**Accepted.**

```carven
let user = await fetch_user(id)?;
```

`await` 启动并消费 operand operation，在当前 logical task 中执行它，不建立独立 structured child。
它可以 suspension，但不隐含 thread blocking、thread creation 或 thread-pool submission。

#### TASK-03 — Operation is owning, move-only and single-consumer

**Accepted.**

Operation 不能被隐式复制或多次 `await`。第一阶段不提供 shared task、multi-consumer future 或
重复启动。

#### TASK-04 — Dropping a cold operation is safe but diagnosed

**Accepted.**

Cold operation 可以同步销毁 captures，且不会开始工作。无意中丢弃 operation 应触发 must-use
diagnostic，以发现遗漏的 `await` 或 `async let`。

#### TASK-05 — `await` and `?` remain orthogonal

**Accepted.**

```carven
let value = await operation?;
```

固定等价于：

```text
(await operation)?
```

`await` 负责 suspension、value/cancelled completion 与 cancellation propagation；`?` 只处理
nominal typed failure。`?` 不处理 cancellation。

#### TASK-06 — Async callable intrinsically admits cancellation

**Accepted.**

Async callable 不需要在 signature 的 `throw`/failure set 中声明 `Cancelled`。Cancellation channel
是 async completion contract 的固有部分；failure set 只描述 typed failures。

### Async let and lexical children

**Maturity:** Accepted semantics; working grammar.

#### CHILD-01 — `async let` starts one lexical child

**Accepted; exact grammar remains working spelling.**

```carven
async let remote = fetch_remote();
```

它建立一个 non-escaping child binding，并把 child lifetime 绑定到 lexical owner scope。

#### CHILD-02 — Plain `async let` children are failure-independent

**Accepted.**

一个 child 提前 failure 不会异步打断 parent，也不会自动取消其他普通 `async let` children。Completion outcome
保存到明确的 observation point；`await child?` 传播 failure 并开始 scope closure 时，其他 children
才因 owner exit 被取消和关闭。

需要 sibling fail-fast policy 时使用 `when_all`，不把它隐含进 `async let`。

#### CHILD-03 — Normal exits require explicit result intent

**Accepted.**

在正常 `return`/fallthrough 路径上，每个 `async let` binding 必须满足以下之一：

- 已被 `await`/消费；
- 已显式 `cancel(child)`，表明该路径不再要求观察结果。

否则 compiler 应诊断。Typed-failure propagation、cancellation propagation 等 non-value exit 不要求
用户逐个写 `cancel`；owner epilogue 自动关闭 children，原 parent outcome 保持 primary。

```carven
async let remote = fetch_remote();

if cache_hit {
    cancel(remote);
    return cached;
}

return await remote?;
```

### Cancellation surface

**Maturity:** Accepted semantics; selected names and module placement remain working spellings.

#### CANCEL-01 — Cancellation authority follows structured ownership

**Accepted.**

Lexical owner 拥有请求其 children 停止的 authority。Ambient cancellation context 由 parent 向下
传递；普通 observer 不自动获得任意 task cancellation authority。

#### CANCEL-02 — `cancel(child)` is request-only

**Accepted; standard module placement remains working spelling.**

```carven
cancel(child);
```

它是 compiler-known standard operation，不是 keyword 或 contextual keyword。Compiler 在名称解析
后按 canonical symbol identity 形成 cancellation SemanticProgram operation；同名 user function 仍是普通调用。

`cancel(child)`：

- 请求取消；
- 不等待、不保证 terminal、不直接销毁；
- 不消费 binding，不转移 ownership；
- 允许之后继续 `await child`；
- 在 `cancel(child)` 后不观察并退出 scope 的路径上，作为 explicit result-abandonment intent。

#### CANCEL-03 — Ambient request is forwarded, not automatically accepted at every `await`

**Accepted.**

`await` 把 ambient cancellation context 传给 operation，但语言不会在每个 `await` 前后偷偷插入
强制 cancellation completion。Operation 根据自己的安全点与 contract 响应 request。

增删无关 `await` 不应自动改变 cancellation control flow。

#### CANCEL-04 — Query and checkpoint are separate operations

**Accepted semantics; names are working spellings.**

```carven
if cancellation_requested() {
    return partial;
}

await cancellation_point();
```

- `cancellation_requested() -> bool` 是纯查询，不转移控制；
- `await cancellation_point()` 显式接受已到达的 request：无 request 时立即 value-complete，有 request
  时完成为 `cancelled`；
- compiler 可以把二者降低为 flag load/branch，不需要分配 task object。

#### CANCEL-05 — No cancellation keyword family in phase one

**Accepted.**

第一阶段不增加 `cancel`、`exit`、`stop`、`check cancellation`、`shield`、`on cancel` 或 `join`
keyword。现有 `async`、`await`、`async let`、`return` 与 `throw` 承担 source control structure；
cancellation request/query/checkpoint 使用 compiler-known operations。

#### CANCEL-06 — No user cancellation source/handler/shield in phase one

**Deferred.**

UI button、server shutdown、deadline、test orchestration、supervisor 与 `#[cpp]` stop-token bridge
可能需要未来的 source/context capability。第一阶段由 structured owner 与 runtime 内部持有 source，
不开放任意 Task handle、custom cancellation handler 或 shield。

### Lifetime and scope closure

**Maturity:** Accepted phase-one invariants; ownership transfer remains deferred.

#### OWN-01 — Every active operation has one accountable owner

**Accepted.**

每个 started operation 在任何时刻必须有且只有一个 accountable lifetime owner。该约束是 source
responsibility invariant，不要求实现只能有一个 pointer/reference。

#### OWN-02 — Owner completion requires child lifetime closure

**Accepted.**

Owner scope 可以开始执行 async closing epilogue，但在 owned child 仍 live 时不能向外发布完成、
销毁自身 frame 或释放 child 可达 storage。

```text
request cancellation if needed
    -> wait for or prove child closure
    -> destroy child state
    -> publish parent value/failure/cancelled
```

该等待可以 suspension，但不阻塞 thread。

#### OWN-03 — Result abandonment is not lifecycle abandonment

**Accepted.**

```text
abandon result != destroy running operation
```

用户不再关心 child value/failure 后，owner 仍负责 cancellation、closure 与安全销毁。Loser 或
speculative child 的 outcome 可以成为 secondary，但 active state 不能因此被直接丢弃。

#### OWN-04 — Active child has no ordinary immediate `drop`

**Accepted.**

```text
drop(cold operation)   -> safe, with must-use diagnostic
drop(active child)     -> rejected as immediate destruction
drop(closed state)     -> safe
```

受信任 backend 若保证同步 revoke 后永久 quiescent，可以直接满足 lifetime-closed postcondition；
这是 compiler/backend proof，不是普通 destructor 的默认含义。

#### OWN-05 — Future non-joining work requires explicit ownership transfer

**Deferred.**

未来 service、actor 或 supervisor 可以成为明确的新 owner。Transfer 必须原子地交接 lifetime、
shutdown cancellation、failure policy 与 storage responsibility；不提供 ownerless `detach`。

第一阶段没有 transfer，因此 `async let` 与组合器 children 必须在当前 structured tree 内关闭。

### When all

**Maturity:** Accepted.

#### ALL-01 — Source name is `when_all`

**Accepted.**

使用 `when_all` 与 C++26 `std::execution::when_all` 的概念命名对齐，并避免与 collection predicate
`all` 冲突。不提供 `all` alias。

`when_all` 是 compiler-known function-shaped operation，不是 keyword/contextual keyword。

#### ALL-02 — `when_all` is a cold fixed-size composite

**Accepted.**

第一阶段只接受静态已知的 cold input operations。调用只构造 composite；`await` 或 `async let`
启动 composite 时才启动全部 children。

```carven
let pair = when_all(fetch_user(id), fetch_permissions(id)); // cold
let (user, permissions) = await pair?;                       // starts both
```

Argument expressions 左到右求值。所有 children 都处于同一并发组合边界；参数顺序不建立依赖，
但不承诺创建 threads 或物理 parallelism。已经启动的 `async let` bindings 不传入 `when_all`；未来若
需要该能力，使用不同的 explicit join operation，而不重载 ownership semantics。

#### ALL-03 — Success returns an argument-ordered heterogeneous tuple

**Accepted.**

```carven
let (a, b, c) = await when_all(op_a(), op_b(), op_c())?;
```

Result order 按 argument order，不按 completion order。Conceptual type：

```text
Operation<A, E1> + Operation<B, E2>
    -> Operation<(A, B), E1 | E2>
```

Tuple 是一般静态 product，不是 async-only container。固定异构 product 使用 `(a, b, c)`，不使用
array/list spelling。

#### ALL-04 — First non-value closes the group; final failure outranks cancellation

**Accepted.**

两个阶段必须分开：

1. 首个 child `failure`/`cancelled` 或 accepted ambient cancellation 立即触发 sibling cancellation；
2. 全部 children lifetime-close 后才裁定 outward completion。

最终裁定：

```text
any failure     -> first committed failure
else cancelled  -> cancelled
else            -> argument-ordered value tuple
```

因此 genuine typed failure 不会因为 cancellation 早到几微秒而被掩盖。Cancellation-induced backend
结果应由对应 operation/adapter 正确映射为 `cancelled`，而不是伪装成 typed failure。

Multiple failures 之间 first committed failure wins；business error priority 必须通过顺序/嵌套表达，
不由 `when_all` 猜测。

#### ALL-05 — `when_all` owns and closes all internal children

**Accepted.**

即使已经选出 outward failure/cancellation，composite 仍必须 close every child。Compiler 可以展平
semantic ownership tree，不要求生成 runtime group object。

### Competitive composition

**Maturity:** Accepted semantics; selected spellings and label grammar remain working.

#### ANY-01 — Provide `when_any` and `first_successful`, not `race`

**Accepted semantics; `first_successful` remains working spelling.**

`race` 在不同生态中可能表示 first terminal、first value、drop losers 或 detach losers，语义不稳定。
第一阶段不提供 `race` 或 control-flow `select` keyword。

#### ANY-02 — `when_any` selects the first terminal completion

**Accepted.**

```text
first(value | failure | cancelled) wins
```

Winner 一旦 committed，later loser value/failure/cancelled 不能替换它。Multiple candidates 同时 ready
时，第一阶段以 argument/start order 作为 tie-breaker；循环式 fairness 留给未来 `select`/stream 设计。

#### ANY-03 — `first_successful` selects the first value completion

**Accepted.**

Child failure/cancelled 只使该 candidate 退出竞争，不立即结束 composite。首个 value 成为 winner 并
触发 loser cancellation。全部 candidates 关闭且没有 value 时：

```text
any failure -> first committed failure
else        -> cancelled
```

Accepted ambient cancellation 终止整个 composite，不继续寻找 success。

#### ANY-04 — Competitive losers are cancelled and lifetime-closed

**Accepted.**

```text
commit winner
    -> request cancellation of losers
    -> lifetime-close every loser
    -> publish winner
```

Winner 已选出不代表调用者立即恢复；unstoppable loser 会延迟 outward completion。第一阶段不通过
hidden runtime ownership transfer 提前返回。Backend synchronous revoke 可以零成本满足 closure。

#### ANY-05 — `when_any` success is a statically tagged branch sum

**Accepted result model; label grammar is working spelling.**

Conceptual type：

```text
Operation<A, E1> + Operation<B, E2>
    -> Operation<Choice<A, B>, E1 | E2>
```

Success result 必须携带 winner identity，且使用静态 sum，不返回 Task handle、`index + Any` 或动态
type erasure。理想 source 形态可能使用 labeled branches：

```carven
let event = await when_any(
    packet: socket.read(),
    shutdown: wait_shutdown(),
)?;
```

标签的 exact grammar 尚未接受。

#### ANY-06 — `first_successful` initially requires one normalized value type

**Accepted.**

```text
Operation<T, E1> + Operation<T, E2>
    -> Operation<T, E1 | E2>
```

异构 candidates 应先显式映射到共同 domain type，或使用 `when_any` 的 tagged sum。不让 result shape
因为偶然的 type equality 隐式变化。

### Timeout

**Maturity:** Accepted semantics; source and failure-type spellings remain working.

#### TIME-01 — Timeout is a compiler-known operation, not syntax

**Accepted semantics; `timeout` remains working spelling.**

```carven
let response = await timeout(fetch_response(), 2.seconds)?;
```

不增加 `within`/`timeout` keyword。调用构造 cold composite，启动时竞争 operation 与 deadline。
Compiler 可以使用 backend-native deadline、linked timeout、event-loop timer 或显式 timer child，
不要求 source shape 对应 runtime timer task。

#### TIME-02 — Local timeout produces typed failure

**Accepted; `Timeout` type name remains working spelling.**

```text
operation value first     -> value
operation failure first   -> propagate failure
operation cancelled first -> cancelled
deadline first            -> close operation; failure(Timeout)
ambient cancellation      -> close operation and timer; cancelled
```

Conceptual failure set 是 `E | Timeout`。Local deadline policy 与 owner/ambient cancellation 保持可区分。
Deadline 一旦赢得竞争，loser 的晚到 completion 不替换 `Timeout`。

### Source surface

**Maturity:** Accepted semantic roles with the per-form spelling maturity below.

| Form | Maturity |
| --- | --- |
| `async fn f() { ... }` | Async callable semantics accepted; exact declaration grammar working |
| `await operation?` | `await`/`?` composition accepted; exact grammar working |
| `async let child = operation;` | Child semantics accepted; exact grammar working |
| `cancel(child)` | Function-shaped compiler-known operation accepted; standard-module placement working |
| `cancellation_requested()` | Semantics accepted; name working |
| `await cancellation_point()` | Semantics accepted; name working |
| `when_all(a(), b())` | Name and function-shaped operation accepted |
| `when_any(left: a(), right: b())` | `when_any` semantics accepted; label grammar working |
| `first_successful(a(), b())` | Semantics accepted; name working |
| `timeout(operation, duration)` | Semantics accepted; name and `Timeout` type spelling working |

第一阶段不因 cancellation、join、race、timeout、shield 或 ownership transfer 新增 keyword。
Compiler-known standard operations 可以在 canonical symbol resolution 后获得专用 SemanticProgram operations 与 diagnostics，
无需升级为 contextual keyword。

### Compiler-owned facts and feature admission

**Maturity:** Draft; blocked by `OPEN-01` and `OPEN-02`.

Accepted decision 不等于 implementation authorization。进入 grammar/SemanticProgram 前，至少需要把以下 facts
闭合为一个垂直切片：

- async callable signature 与 value/failure/cancellation completion；
- cold operation construction、move-only/single-consumer consumption 与 must-use；
- `await` evaluation、start、suspension、resume 与 completion disposition；
- lexical owner、child、observation、cancellation authority 与 lifetime-closure obligations；
- composite 的 exactly-once child start、arbitration、secondary outcomes 与 result shape；
- scope-closing control edges 与 parent completion publication barrier；
- context inheritance/transition 与 same-thread restriction；
- frame captures、borrow admission、destruction order 与 storage requirement；
- 与现有 name、type、call、access、failure、control 和 availability facts 的关系。

SemanticProgram 必须在 publish 前拥有这些 source facts。Promise type、template substitution、destructor 或 library
behavior 不得重新决定 source validity。

### Selected lowering

**Maturity:** No lowering has been selected.

Lowering 可以选择 C++ coroutine、显式 state machine、specialized frame、sender adapter 或其他
observationally equivalent mechanism。只要 source contract、compiler facts、diagnostics 与成本边界
不变，compiler 可以展平 ownership tree、消除 runtime group/task wrapper、避免 allocation/type
erasure，或使用 backend-native cancellation/deadline。Inactive candidate research 记录在
`DEFER-08`。Frame 或 operation storage 只保留 runtime execution 跨 suspension、completion 或
interop boundary 后仍需携带的状态；compiler-generated coroutine、IIFE 或 library wrapper 不自行
建立新的 source obligation。

## Decision record

本表是 authority index；完整 contract 由上面的 Design 拥有。

| ID | Decision | Design | Rationale |
| --- | --- | --- | --- |
| `METHOD-01` | Carven owns async semantics | [Semantic authority](#semantic-authority-and-cost-model) | Backend mechanism must not become a second source-validity path |
| `METHOD-02` | Intent analysis may specialize away generic machinery | [Semantic authority](#semantic-authority-and-cost-model) | High-level syntax must not impose an abstraction tax |
| `METHOD-03` | Mechanism correctness and use correctness are distinct | [Semantic authority](#semantic-authority-and-cost-model) | Structured safety cannot infer business independence or rollback effects |
| `TERM-01` | Invocation produces an operation | [Vocabulary](#vocabulary) | Avoid binding source identity to a runtime Task class |
| `TERM-02` | A started structured operation is a child | [Vocabulary](#vocabulary) | Name the lexical lifetime relationship |
| `TERM-03` | Completion is value, typed failure, or cancelled | [Vocabulary](#vocabulary) | Cancellation remains separate from the failure set |
| `TERM-04` | Cancellation request is not completion | [Vocabulary](#vocabulary) | Cooperative stop may be delayed or ignored |
| `TERM-05` | Observation is distinct from lifetime responsibility | [Vocabulary](#vocabulary) | Abandoning a result must not orphan work |
| `TERM-06` | Lifetime-closed is a backend-independent postcondition | [Vocabulary](#vocabulary) | Safe destruction matters more than a particular join operation |
| `TERM-07` | Every operation has an accountable lifetime owner | [Vocabulary](#vocabulary) | References and observers do not own closure |
| `TASK-01` | Async invocation is cold | [Single operation](#single-async-operation) | Construction alone must not start work |
| `TASK-02` | `await` starts in the current logical task | [Single operation](#single-async-operation) | Suspension does not imply a new child or thread |
| `TASK-03` | Operation is owning, move-only, and single-consumer | [Single operation](#single-async-operation) | Phase one avoids shared/repeated consumption |
| `TASK-04` | Dropping a cold operation is safe but diagnosed | [Single operation](#single-async-operation) | Captures are destroyable while missed work remains visible |
| `TASK-05` | `await` and `?` are orthogonal | [Single operation](#single-async-operation) | Typed failure and async cancellation remain separate control channels |
| `TASK-06` | Async callables intrinsically admit cancellation | [Single operation](#single-async-operation) | `Cancelled` does not pollute nominal failure sets |
| `CHILD-01` | `async let` starts one lexical child | [Lexical children](#async-let-and-lexical-children) | Parallel intent gets structured ownership |
| `CHILD-02` | Plain children are failure-independent | [Lexical children](#async-let-and-lexical-children) | Fail-fast sibling policy remains explicit in `when_all` |
| `CHILD-03` | Normal exits require explicit result intent | [Lexical children](#async-let-and-lexical-children) | Silent result abandonment is diagnosed without burdening non-value exits |
| `CANCEL-01` | Cancellation authority follows structured ownership | [Cancellation](#cancellation-surface) | Arbitrary observers must not gain stop authority |
| `CANCEL-02` | `cancel(child)` is request-only | [Cancellation](#cancellation-surface) | A request neither waits nor destroys active state |
| `CANCEL-03` | Ambient request is forwarded, not auto-accepted at each `await` | [Cancellation](#cancellation-surface) | Unrelated suspension points must not silently alter control flow |
| `CANCEL-04` | Query and checkpoint are separate operations | [Cancellation](#cancellation-surface) | Observation and explicit acceptance have different effects |
| `CANCEL-05` | Phase one adds no cancellation keyword family | [Cancellation](#cancellation-surface) | Function-shaped compiler-known operations are sufficient |
| `CANCEL-06` | User cancellation sources, handlers, and shields are deferred | [Cancellation](#cancellation-surface) | Structured owner/runtime authority is sufficient for phase one |
| `OWN-01` | Every active operation has one accountable owner | [Lifetime](#lifetime-and-scope-closure) | Live async state must never be ownerless |
| `OWN-02` | Owner completion waits for child lifetime closure | [Lifetime](#lifetime-and-scope-closure) | Parent storage cannot disappear while children may reach it |
| `OWN-03` | Result abandonment is not lifecycle abandonment | [Lifetime](#lifetime-and-scope-closure) | Secondary outcomes do not make active state disposable |
| `OWN-04` | Active child has no ordinary immediate drop | [Lifetime](#lifetime-and-scope-closure) | Default destruction cannot assume callback quiescence |
| `OWN-05` | Non-joining work requires future explicit ownership transfer | [Lifetime](#lifetime-and-scope-closure) | Detach must transfer, not erase, closure responsibility |
| `ALL-01` | The source name is `when_all` | [When all](#when-all) | Align with execution vocabulary and avoid collection `all` |
| `ALL-02` | `when_all` is a cold fixed-size composite | [When all](#when-all) | Static intent enables structured start and specialization |
| `ALL-03` | Success is an argument-ordered heterogeneous tuple | [When all](#when-all) | Result shape follows source arguments, not completion order |
| `ALL-04` | First non-value closes the group; final failure outranks cancellation | [When all](#when-all) | Genuine failures must not be hidden by cancellation timing |
| `ALL-05` | `when_all` owns and closes every internal child | [When all](#when-all) | Outward selection never permits orphaned work |
| `ANY-01` | Provide `when_any` and `first_successful`, not `race` | [Competitive composition](#competitive-composition) | `race` has ecosystem-dependent meaning |
| `ANY-02` | `when_any` selects first terminal completion | [Competitive composition](#competitive-composition) | Winner policy is explicit and stable |
| `ANY-03` | `first_successful` selects first value completion | [Competitive composition](#competitive-composition) | Failed candidates do not prematurely end value search |
| `ANY-04` | Competitive losers are cancelled and lifetime-closed | [Competitive composition](#competitive-composition) | Winner selection does not transfer loser ownership |
| `ANY-05` | `when_any` success is a statically tagged branch sum | [Competitive composition](#competitive-composition) | Winner identity stays typed without dynamic erasure |
| `ANY-06` | `first_successful` initially requires one normalized value type | [Competitive composition](#competitive-composition) | Result shape must not change through accidental type equality |
| `TIME-01` | Timeout is a compiler-known operation, not syntax | [Timeout](#timeout) | Backend deadlines need no new control keyword |
| `TIME-02` | Local timeout produces typed failure | [Timeout](#timeout) | Deadline policy remains distinct from ambient cancellation |

## Open decisions

**Next discussion:** `OPEN-01`

### OPEN-01 — What execution context does a same-thread operation inherit?

- **Status:** Active
- **Depends on:** `TASK-01`, `TASK-02`, `CANCEL-03`, `OWN-01`, `OWN-02`
- **Blocked by:** None
- **Activation condition:** Active now
- **Why it matters:** Context capture、continuation placement、inline completion、same-thread progress 与
  submission failure 都依赖这个答案。
- **Constraints:** Scheduler is an execution mechanism rather than operation identity; `async` does not imply
  a thread pool, parallelism, or migration; phase one remains same-thread.
- **Options:** The following coupled axes remain unresolved:
  1. cold construction records caller context, or start inherits the structured owner's current context;
  2. continuation resumes in awaiting context, completion context, or an explicitly selected context;
  3. direct completion may synchronously resume parent on the starting stack, or must enqueue;
  4. deep synchronous completion chains use unrestricted recursion, trampolining, or another bounded mechanism;
  5. a same-thread queue guarantees FIFO, eventual progress, another fairness level, or none;
  6. allocator, clock, deadline, and ambient cancellation are wholly or partly context properties;
  7. same-thread scheduler submission can fail, and if so whether that failure is nominal.
- **Closure condition:** Select a coherent same-thread context and continuation model, then validate it against
  nested awaits, immediate completion, reentrancy, deep chains, cancellation, and submission failure.

### OPEN-02 — Which values and borrows may live across suspension, and where may the frame live?

- **Status:** Blocked
- **Depends on:** `OPEN-01`
- **Blocked by:** `OPEN-01`
- **Activation condition:** Execution-context and continuation semantics are closed.
- **Why it matters:** Source lifetime safety、destruction order、phase-one frame storage、cost visibility 与
  allocation failure 都依赖一个由 Carven 拥有的 suspension boundary。
- **Constraints:** C++ coroutine-frame lifetime must not become an accidental Carven guarantee; until a proof
  exists, implementation must reject an unsafe borrow rather than emit potentially dangling C++.
- **Options:** The design must close all of these dimensions:
  1. operand evaluation, temporary destruction, and typed-failure propagation around suspension;
  2. locals/captures moved into the frame versus referents held by an external owner;
  3. whether Read/Write borrows can cross a potentially suspending `await`;
  4. whether a narrow borrow subset is admitted when the compiler proves owner coverage;
  5. how cancellation and the closing epilogue extend child-reachable storage;
  6. caller/scope frame storage versus compiler/runtime allocation;
  7. `async` 是否足以让 phase-one frame-storage cost 可见，以及 allocation failure 归哪个
     phase-one contract。
- **Closure condition:** Define a diagnostic admission rule, destruction order, storage contract, and examples
  covering success, failure, cancellation, and owner exit.

## Deferred work

### DEFER-01 — Advanced suspension and frame control

- **Reason deferred:** Phase one needs a conservative, diagnosable borrow and storage boundary before exposing
  general lifetime proof or user-controlled frame placement.
- **Depends on:** `OPEN-02`
- **Reactivation condition:** The phase-one frame contract is implemented and a concrete API requires more
  general borrow proof, custom allocation, or explicit storage control.

This includes general borrow-across-suspension proof beyond the conservative admission boundary, custom
allocator selection, and user control over frame placement or allocation policy.

### DEFER-02 — Dynamic composition and async abstractions

- **Reason deferred:** Fixed operations and lexical composition must close before runtime-sized or iterative
  composition introduces new ownership and fairness rules.
- **Depends on:** `OPEN-01` and `OPEN-02`
- **Reactivation condition:** A real program cannot be expressed by fixed-size combinators and supplies a
  complete lifetime, cancellation, result, and fairness contract.

This includes dynamic/runtime-sized task groups, async closures, generators, streams, and loop-oriented
`select`.

### DEFER-03 — Shared observation

- **Reason deferred:** The phase-one operation is move-only and single-consumer; sharing changes result
  storage, observation, cancellation authority, and lifetime.
- **Depends on:** `TASK-03`, `OPEN-01`, and `OPEN-02`
- **Reactivation condition:** A concrete API requires multiple observers and can define repeated observation,
  result retention, cancellation authority, and closure.

This includes shared tasks, multi-consumer futures, and repeated `await`.

### DEFER-04 — User cancellation control

- **Reason deferred:** Phase one obtains cancellation authority from structured ownership and does not need an
  arbitrary user-created control plane.
- **Depends on:** `CANCEL-01` through `CANCEL-06`
- **Reactivation condition:** UI shutdown, service control, tests, deadlines, or interop require a source-level
  cancellation source and can specify propagation, handlers, shielding, and lifetime.

This includes user cancellation sources/tokens, custom cancellation handlers, and shields. `CANCEL-06`
remains the phase-one authority.

### DEFER-05 — Ownership transfer and supervision

- **Reason deferred:** Phase one keeps every started child inside one structured ownership tree.
- **Depends on:** `OWN-01` through `OWN-05`
- **Reactivation condition:** A service, actor, or supervisor use case requires work to outlive the current
  scope and can atomically transfer lifetime, shutdown, failure, and storage responsibility.

This includes daemon work, explicit detach-like transfer, supervisor/service/actor ownership, and background
runtime ownership. Ownerless work remains invalid; `OWN-05` is the phase-one authority.

### DEFER-06 — Async resource disposal

- **Reason deferred:** Phase one can express explicit cleanup without selecting a generic async-disposal
  protocol or new control construct.
- **Depends on:** `OPEN-01`, `OPEN-02`, and a concrete async resource API
- **Reactivation condition:** Repeated resource APIs require one reusable disposal contract and can state
  failure, cancellation, ordering, and scope-exit behavior.

This direction owns any generic async resource-disposal construct.

### DEFER-07 — Cross-thread async and scheduler surface

- **Reason deferred:** Same-thread async can be designed independently; migration introduces value movement,
  sharing, happens-before, synchronization, affinity, and shutdown obligations.
- **Depends on:** [Memory model](memory-model.md) and [threading](threading.md)
- **Reactivation condition:** Those proposals define a concrete cross-thread value and synchronization contract
  for an actual executor or provider use case.

This direction includes cross-thread resume, physical parallel execution, Send/Sync-like capability,
scheduler/executor source APIs, source-visible scheduler hops, thread affinity, priority, fairness, and
cross-thread completion carriers.

### DEFER-08 — Provider/lowering selection and public `#[cpp]` async interop

- **Reason deferred:** Provider/lowering selection and source-facing adapter implementation require closed
  source semantics, execution context, suspension lifetime, and feature-admission facts.
- **Depends on:** `OPEN-01` and `OPEN-02`; [memory model](memory-model.md) and [threading](threading.md) for
  cross-thread candidates
- **Reactivation condition:** The source contract and compiler facts form an actionable vertical slice; a
  provider or lowering may then be selected without leaking experimental types into public artifacts.

Non-normative evidence collection may continue in parallel through standards/library study, code inspection,
benchmarks, and private prototypes. That work does not select C++ coroutine, `std::execution`, stdexec,
async_simple, libcoro, ASIO, Yalanting, or another runtime, and cannot modify operation, completion, failure,
cancellation, lifetime, or context semantics. Provider/lowering selection, public C++ async ABI, third-party
task/sender leakage, and source-facing adapter implementation remain deferred by this entry.

#### Research scope and boundaries

- Carven operation/completion and C++ coroutine/awaitable adapters in both directions;
- Carven operation and C++26 `std::execution` sender adapters in both directions;
- platform I/O, event loops, thread pools, and third-party runtime providers;
- exception, cancellation, scheduler, allocator, generated representation, public consumer surface, ABI, and
  downstream build conversion;
- C++ exceptions cannot cross a no-exception Carven frame; an exception-enabled adapter catches and maps them
  to declared typed failure;
- third-party task, sender, scheduler, socket, and allocator types do not enter phase-one public generated ABI;
- downstream builds explicitly select, pin, and link a provider; the compiler does not become a second package
  manager;
- ordinary generated C++ remains inspectable and debuggable behind a Carven-owned adapter contract.

#### Candidate roles

| Candidate | Possible role | Not its role |
| --- | --- | --- |
| Carven-owned operation | Default no-exception operation, typed completion, symmetric transfer | Platform I/O or full production scheduler |
| C++26 `std::execution` / stdexec | Completion/scheduler/scope adapter and long-term standards bridge | Source vocabulary or current default public ABI |
| async_simple | Implementation reference or explicit adapter for cold task, symmetric transfer, and executor propagation | Carven typed-failure/cancellation semantics |
| libcoro | Experimental I/O/network provider and end-to-end prototype | Language operation contract or stable ABI |
| ASIO/Yalanting | Opt-in event-loop, network, or RPC provider | Async language semantics |

#### Research questions

- Which C++ awaitables/senders can be imported safely, and how are value/error/stopped signatures declared?
- How does a C++ exception set map statically to nominal failure?
- How does Carven cancellation map bidirectionally to `std::stop_token` or a library token?
- How are scheduler affinity, thread migration, and callback lifetime verified?
- May an adapter allocate or erase types, and how is cost visible in the source/build contract?
- Is the C++ consumer API a blocking bridge, callback, awaitable, sender, or separate layered surfaces?
- Does ABI stop at a C adapter or include a C++ contract within one compiler/toolchain domain?
- Who owns provider shutdown, outstanding work, process lifetime, and the test harness?

#### Adapter evidence

Every adapter is an independent product slice and must validate:

- lossless value, typed-failure, and cancellation mapping;
- exactly-once completion and operand evaluation;
- frame, operation, and provider lifetime;
- scheduler/context transition;
- no-exceptions and exception-enabled boundaries;
- generated C++20/C++23 compile, link, and run behavior;
- dependency, license, version pin, and downstream build instructions;
- no private backend type leaks through public surfaces unless the consumer contract explicitly accepts it.

Evidence collection may compare one `std::execution` sender bridge and one coroutine-operation bridge using
generated code size, compile time, completion fidelity, lifetime, and diagnostics. Such experiments remain
private and do not activate implementation or select a provider.

## Implementation

Implementation is not yet actionable. `OPEN-01` and `OPEN-02` block a coherent grammar/SemanticProgram/control/lifetime
slice; `DEFER-08` keeps provider/lowering selection deferred until that slice exists.

Once unblocked, each relevant decision ID must map through grammar, ParsedBatch, SemanticProgram, verification, unit-local TargetUnit lowering,
lowering/runtime, interop boundaries, diagnostics, tests, and permanent-document handoff. Scope-closing control
edges and the parent-completion publication barrier must be explicit compiler facts rather than destructor or
library accidents. Cross-thread work remains separately blocked by the memory-model and threading proposals.

## Validation

Feature admission must cover:

- cold operation does not start early and accidental discard emits a must-use diagnostic;
- exactly-once operation start, consume, and completion;
- normal-exit child observation or explicit cancellation intent;
- cancellation request remains distinct from cancelled completion;
- an unresponsive child delays owner/composite completion;
- `when_all` evaluation, start, result order, failure precedence, and lifetime closure;
- `when_any` winner stability, tie-break, and loser secondary outcomes;
- `first_successful` skips failed/cancelled candidates;
- timeout, ambient cancellation, and child completion races;
- parent frame destruction only after every owned child is lifetime-closed;
- synchronous backend revoke satisfies the complete lifetime-closed postcondition;
- rejection of active drop, orphan work, implicit detach, repeated await, and cross-thread resume;
- direct/inline completion reentrancy and deep-chain behavior;
- temporary, borrow, frame, and destruction behavior around suspension;
- generated C++20/C++23 compile, link, and run checks under no-exceptions configuration.

Tests assert Carven observable semantics and representation invariants. They do not fix private generated
spelling, a runtime class, heap-allocation count, or a third-party type. Cost claims require benchmarks and
artifact inspection in addition to semantic tests. Adapter-specific evidence remains under `DEFER-08` and may
be collected without turning a candidate into an implementation choice.

## References

- [Carven philosophy](../docs/philosophy.md)
- [Async learning note](../notes/async-programming.md)
- [Failure-contract semantics](../docs/semantics.md#failure-contracts)
- [Failure-model and runtime-materialization note](../notes/failure-models.md)
- [Memory model proposal](memory-model.md)
- [Threading proposal](threading.md)
- [C++ interop proposal](cpp-interop.md)
- [Proposal roadmap](roadmap.md)
