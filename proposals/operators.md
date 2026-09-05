# 运算符能力

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Closed user-defined semantic hooks for existing operator tokens
- **Depends on:** [Generics](generics.md), including canonical evidence and associated-type normalization

## Summary

Carven 需要让 nominal type 参与已有 operator token，同时避免引入 C++ overload
lookup、ADL、implicit-conversion ranking 或 user-defined syntax。真实需求包括同型
arithmetic、异型 operands，以及与两侧 operand 都不同的 result type。

当前 leading direction 是把一组封闭 token 映射到 compiler-known static capabilities，
并用 canonical `impl` evidence 提供行为。这个方向仍只是 candidate：operator
capability carrier、identity、token set、operand/result shape、equality 与 compound
assignment 都没有被接受。

| Slice | Maturity | Current frontier |
| --- | --- | --- |
| Identity and visibility | Exploration | `OPEN-02` 是下一讨论 |
| Capability carrier | Exploration | 只有 leading candidate；`OPEN-01` 仍未裁定 |
| Initial operator set and signatures | Exploration | `OPEN-03` 至 `OPEN-05` |
| Compound assignment | Exploration | `OPEN-06` |
| Additional operator families | Deferred | `DEFER-01` 至 `DEFER-05` |

## Context

### 用户需求

需要覆盖的空间至少包括：

```text
Vec3   + Vec3   -> Vec3
Vec3   * f32    -> Vec3
Meter  * Meter  -> SquareMeter
Matrix * Matrix -> Matrix
```

Fieldwise derive 只能自然覆盖其中一部分。通过 C++ source fragment/provider 绕行其他行为会让
arithmetic、evaluation order、failure 与 access 逃离 Carven ordinary semantic path。目标是
允许 nominal behavior，而不是复制 C++ multi-path lookup。

### 已实现或已接受的约束

- Grammar 已固定 operator token、precedence 与 associativity；parsing 不查询 operand type。
- `&&`/`||` 左到右 short-circuit；其他 expression operands 左到右且各求值一次。
- Access position 的 `&`/`&&` 是 Write/Take marker，不是可重载 operator。
- Equality 当前是 compiler-derived recursive property，被 runtime comparison、
  aggregate equality、pattern normalization 与 constant facts 共同使用；floating
  pattern 把 `0.0` 与 `-0.0` 视为相等。
- Assignment 与 compound assignment 参与 availability restoration；只有完整 plain
  assignment 恢复 Taken `var`。
- Generic proposal 已接受 definition-site checking、canonical evidence、strong global
  coherence、module-domain/anchored-head 限制与 associated-output normalization，但尚未实现。

任何 operator extension 都必须保持这些事实，不能改变 parse tree、evaluation count、
short-circuit behavior 或 availability transition。

### Authority relations

- [Generics](generics.md) 拥有 capability identity、canonical evidence、definition-site
  checking、coherence 与 associated normalization；本文只拥有 existing token 到这些
  facts 的映射。
- Grammar precedence 与本文正交：hook 只改变 resolved meaning，不改变 parse tree。
- [C++ interoperation contract](../docs/semantics.md#c-interoperation) 独立决定 export façade 是否暴露带 nominal
  operator behavior 的 type；operator capability 不自动扩大 C++ caller surface。
- Concurrency 与本文正交：operator evidence 不提供 synchronization、data-race 或
  thread-safety guarantee。

## Goals and non-goals

### Goals

- 让选中的已有 token 为 nominal type 表达 checked behavior。
- 复用唯一 canonical static capability/evidence model。
- 在真实需求存在时支持异型 right operand 与 output type。
- 在 target lowering 前把每个 operator 解析成一个 semantic operation。
- 保持 source evaluation、access、failure 与 diagnostics。

### Non-goals

- User-defined operator token、precedence 或 associativity。
- C++ ADL、overload set、SFINAE、implicit-conversion ranking、specialization、
  priority 或 declaration-order tie-break。
- Blanket impl、local impl activation 或 call-site witness selection。
- Overload short-circuit、call、index、member access、plain assignment 或
  Read/Write/Take syntax。
- 把 top-level C++ source-fragment fence 扩张为 general attribute namespace，或为 `@`
  增加含义。
- 通过 `str + str` 隐藏 allocation。

## Design

尚未选定 coherent operator design。下一项 `OPEN-02` 先验证 leading candidate
是否能拥有唯一、source-visible 的 identity 与 visibility model，但不决定是否采纳
该 carrier；`OPEN-01` 随后决定采纳、修改或拒绝它。

### Candidate capability carrier

**Maturity:** Exploration; not accepted.

Leading candidate 把 operators 映射到 compiler-defined capabilities，并通过 ordinary
canonical `impl` 提供 evidence：

```carven
impl Add for Vec3 {
    type Rhs = Vec3;
    type Output = Vec3;
    fn add(left: Vec3, right: Vec3) -> Vec3 { ... }
}

impl Mul for Meter {
    type Rhs = Meter;
    type Output = SquareMeter;
    fn mul(left: Meter, right: Meter) -> SquareMeter { ... }
}
```

这个 spelling 只展示 required semantic shape。`Rhs` 是 associated type、capability
argument 还是其他 bounded static input，仍待裁定。`Output` 必须由 canonical
evidence 唯一决定，不能由 caller 选择。

Candidate resolution path 是：

```text
parsed operator
  + normalized operand types
  -> one compiler-known capability obligation
  -> zero or one canonical evidence
  -> one resolved named capability operation in SemIRProgram
```

其中没有 member/non-member/ADL fallback、implicit-conversion candidate set、
specialization、priority 或 declaration-order tie-break。

### Candidate operator surface

**Maturity:** Exploration; owned by `OPEN-03` through `OPEN-06`.

最小 candidate 从 unary arithmetic `-` 与 binary arithmetic `+ - * / %` 开始。
Unary result、heterogeneous right operand 与 heterogeneous output 属于 signature
问题，不是既定假设。

当前方向让以下 forms 留在初始范围之外：

- `==`/`!=`，因为 equality 同时服务 runtime、aggregate、pattern 与 constant facts；
- `&&`/`||`，因为 operation hook 会改变 operand evaluation；
- `=`、`++`、`--` 与 compound assignment，直到 `OPEN-06` 关闭；
- call、index、member access、`?`、`as` 与 access markers，它们各有独立的
  control、lookup、conversion 或 ownership contract。

这些只是 candidate exclusions 或 scope boundary，不是 accepted operator decisions。
Equality 与 compound assignment 仍由显式 OPEN entries 决定。

### Generic definition-site behavior

**Maturity:** Candidate consequence of `OPEN-01`.

如果 carrier 被接受，generic body 中的 operator 在 definition site 根据声明的
capability 完成解析：

```carven
fn sum<T: Add>(left: T, right: Add::Rhs<T>) -> Add::Output<T> {
    return left + right;
}
```

Spelling 仍是示意。Application site 验证唯一 substitution 与 evidence；associated
projection 在 lowering 前规范化。C++ substitution 不重新选择 operator meaning。

### Diagnostics and compiler facts

任何可接受设计都必须区分 absent evidence、conflicting evidence、invalid closed
capability name、projection failure/cycle、signature mismatch 与被排除的 forms。
Expression diagnostic 锚定 operator/operands；invalid evidence 锚定 `impl` head/member。

若 carrier 被接受，SemIRProgram 记录 resolved capability operation 与唯一 evidence dependency；
lowering 不保留 unresolved C++ operator lookup。

### Candidate lowering boundary

**Maturity:** Candidate consequence of `OPEN-01`; no backend selected.

Lowering 可以生成 named operation call 或其他等价 static form，同时保持 Carven
evaluation order 与 failure/access behavior。它不需要生成 C++ overloaded operator，
也不能从 generated operand types 恢复 semantic lookup。

## Open decisions

**Next discussion:** `OPEN-02`

### OPEN-02 — What identity and visibility do compiler-known capabilities use?

- **Status:** Active
- **Depends on:** `GEN-05`, `GEN-06`, `GEN-11`, `GEN-12` in
  [Generics](generics.md)
- **Blocked by:** None
- **Activation condition:** Active now
- **Why it matters:** Operator desugaring 与 source `impl Add` 必须命名同一个 identity，
  并进入同一个 coherence domain；本问题只评估 leading candidate，不接受该 carrier。
- **Constraints:** 只有一份 capability identity、ordinary deterministic visibility 与
  evidence domain；runtime-header path 不能替代 source identity。
- **Options:** A — true language-builtin identity；B — 位于 reserved
  `crafts.std...` paths 的 toolchain-provided declarations；C — compiler 提供、通过
  ordinary import/prelude 可见的 declarations。
- **Closure condition:** 得到一套 conditional model，使 source lookup、desugaring、
  coherence 与 missing-name diagnostics 有一致解释；是否采用 carrier 仍由 `OPEN-01`
  决定。

### OPEN-01 — Should existing operator tokens use canonical closed capabilities?

- **Status:** Blocked
- **Depends on:** `OPEN-02`
- **Blocked by:** `OPEN-02`
- **Activation condition:** Leading candidate 已有可评估的 coherent identity 与
  visibility model。
- **Why it matters:** 后续 token、signature 与 lowering 都需要一条承载 user-provided
  operator behavior 的路径。
- **Constraints:** Parsing 保持 type-independent；resolution deterministic；source
  meaning 在 C++ lowering 前关闭；evaluation/access rules 不变。
- **Options:** A — proposed compiler-known capability + canonical `impl`；B —
  fieldwise derive only（无法表达异型或算法行为）；C — nominal types 继续不能使用
  user-defined operators。
- **Closure condition:** 用 `OPEN-02` 的 identity、真实 nominal types、definition-site
  checking、coherence、diagnostics 与 hidden-cost 检查候选并作出选择。

### OPEN-03 — Which token families enter the first slice?

- **Status:** Blocked
- **Depends on:** `OPEN-01`, `OPEN-02`
- **Blocked by:** `OPEN-01`, `OPEN-02`
- **Activation condition:** Carrier 与 identity 已固定。
- **Why it matters:** 最小切片需要真实用户，同时不能带入 ordering、shift、equality 或
  allocation contracts。
- **Constraints:** Existing precedence/evaluation rules 固定；每个 token 只映射到一个
  closed capability identity。
- **Options:** A — unary `-` 加 binary `+ - * / %`；B — binary arithmetic only；
  C — 更小的 use-case-driven subset。Bitwise、shift 与 ordering 分别由
  `DEFER-01` 至 `DEFER-03` 跟踪。
- **Closure condition:** 选择同型、heterogeneous-Rhs 与 heterogeneous-output 用例，
  只保留该切片实际需要的 tokens。

### OPEN-04 — How are right operands and output types represented?

- **Status:** Blocked
- **Depends on:** `OPEN-01`, `OPEN-02`, `OPEN-03`
- **Blocked by:** `OPEN-01`, `OPEN-02`, `OPEN-03`
- **Activation condition:** 首个 token set 及其用例已知。
- **Why it matters:** Shape 决定 generic bounds、evidence identity、projection
  normalization，以及 caller 能否歧义地选择 output。
- **Constraints:** Canonical evidence 决定唯一 operation/output；异型 Rhs/result 可表达；
  local solving 保持有限。
- **Options:** Associated `Rhs`/`Output`；capability arguments 加 associated output；
  或另一种能满足 accepted generic rules 的 shape。
- **Closure condition:** 为每个首批 token 固定 unary/binary signature，并证明 generic 与
  concrete call 的唯一 normalization。

### OPEN-05 — Is user-defined equality excluded from the initial operator scope?

- **Status:** Blocked
- **Depends on:** `OPEN-01`, `OPEN-02`
- **Blocked by:** `OPEN-01`, `OPEN-02`
- **Activation condition:** Capability identity model 已固定。
- **Why it matters:** Equality 同时被 runtime comparison、aggregate、pattern 与 constant
  reasoning 使用，一个 hook 会改变多个 contracts。
- **Constraints:** 若加入，必须定义哪些 consumers 共用 evidence、implementation 是否可
  non-constant，以及保证哪些 equivalence properties。
- **Options:** A — 初始范围排除 `==`/`!=`；B — 仅在独立完成 unified equality contract 后加入。
- **Closure condition:** 记录初始范围的 exclusion，或提供完整 cross-consumer equality design。

### OPEN-06 — How does compound assignment relate to binary capabilities?

- **Status:** Blocked
- **Depends on:** `OPEN-03`, `OPEN-04`
- **Blocked by:** `OPEN-03`, `OPEN-04`
- **Activation condition:** 至少一个 binary capability 及 result shape 已固定。
- **Why it matters:** 用户会期待 `left += right`，但它与 target-before-value evaluation
  和 availability restoration 交叉。
- **Constraints:** Operands 各求值一次；不会错误恢复 unavailable owner；result 满足
  left-place contract。
- **Options:** A — Output 可完整赋回 left type 时由 binary capability 派生；B — 独立
  closed assignment capability；C — 初始范围拒绝 user-defined compound assignment。
- **Closure condition:** 通过真实 mutable-place examples 检查 access、Take、failure 与
  evaluation，并选择规则。

## Deferred work

### DEFER-01 — Bitwise capabilities

- **Reason deferred:** Integer-like bitwise behavior 不是首个 arithmetic 用例所需，并且需要
  独立 admissible-type contract。
- **Depends on:** `OPEN-01` through `OPEN-04`
- **Reactivation condition:** 真实 generic/nominal API 需要 `~ & | ^`，并能说明 operand、
  result 与 diagnostic contract。

### DEFER-02 — Shift capabilities

- **Reason deferred:** Shift Rhs type 与 invalid-count behavior 需要独立语义合同。
- **Depends on:** `OPEN-01` through `OPEN-04`
- **Reactivation condition:** 真实 API 需要 `<<`/`>>`，并给出 count、result、failure 与
  diagnostic model。

### DEFER-03 — Ordering capabilities

- **Reason deferred:** 四个 ordering tokens 需要一致性合同，并与既有 comparison semantics 交叉。
- **Depends on:** `OPEN-01` through `OPEN-04`
- **Reactivation condition:** Generic/nominal API 需要 ordering，且能定义 `< <= > >=`
  之间的关系。

### DEFER-04 — Owning string concatenation

- **Reason deferred:** 当前 `str` 是 UTF-8 view；`str + str` 需要 owning result 与可见
  allocation intent。
- **Depends on:** Owning string and explicit allocation model
- **Reactivation condition:** Carven 拥有 owning string type 和使 allocation 可见的 source
  contract。

### DEFER-05 — Fieldwise derive shorthand

- **Reason deferred:** 在 general canonical `impl` 落地前，derive marker 会形成第二条路径。
- **Depends on:** Implemented operator capability and canonical evidence model
- **Reactivation condition:** 重复 fieldwise implementations 证明需要生成同一 canonical
  `impl` 的纯 syntax sugar。

## Implementation

在 selected operator slice 的 `OPEN-01` 至 `OPEN-06` 全部关闭、generic associated-type
normalization 存在前，implementation 不可执行。随后交付 closed capability identities、
evidence validation、semantic IR 中的 operator-to-operation resolution、source
diagnostics，以及 resolved operation lowering。

已有 operator token 不需要 grammar change。Implementation 不预建 C++ overload lookup、
operator registry 或 unused capability scaffolding。

## Validation

决策实验与最终切片必须覆盖：

- 同型 operands、heterogeneous Rhs 与 heterogeneous Output；
- generic definition-site resolution 与 application-site evidence validation；
- duplicate/overlap evidence 的稳定拒绝；
- 未进入切片的 equality、short-circuit、assignment、access 与 control forms 保持不变；
- precedence 与 associativity 不变；
- operands 左到右各求值一次，并保持 ordinary failure/access behavior；
- C++20/C++23 compile、link、run，不 snapshot private generated names 或 capability
  representation。

## References

- [泛型与静态约束](generics.md)
- [C++ interoperation semantics](../docs/semantics.md#c-interoperation)
- [Memory model](memory-model.md)
- [Proposal roadmap](roadmap.md)
- [Carven grammar](../docs/grammar.md)
- [Carven semantics](../docs/semantics.md)
