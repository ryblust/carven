# `match` 表达式

**状态：已实现 (done)**

## 目标

为 Carven 添加模式匹配表达式，支持运行时值匹配和编译期类型匹配。

## 提议语法

### 基本形式

```carven
match value {
    1 => { /* body */ }
    2 | 3 => { /* multi-pattern */ }
    _ => { /* default/wildcard */ }
}
```

### 带解构（未来，当结构体字面量可用时）

```carven
match point {
    Point { x: 0, y: 0 } => { /* origin */ }
    Point { x, y: 0 } => { /* x axis */ }
    _ => { /* other */ }
}
```

## 转译策略

### 选项 A：`if`/`else if` 链

适用于简单值匹配，通用且灵活。

```cpp
if (value == 1) { /* body */ }
else if (value == 2 || value == 3) { /* body */ }
else { /* default */ }
```

### 选项 B：`switch` 语句

适用于整数/枚举匹配，性能更高但类型受限。

### 选项 C：混合策略

对整数/枚举用 `switch`，其他类型用 `if`/`else if`。

## 待定事项

- [ ] 整体设计方向？是轻量的值匹配，还是 Rust 式的全功能模式匹配？
- [ ] 是否作为**表达式**（像 `if` 一样返回值的）？
- [ ] 穷尽性检查：编译器是否强制要求匹配所有情况？
- [ ] 模式类型：值匹配、绑定、通配符、范围、守卫条件？
- [ ] `_` vs `else` vs `default` 作为默认分支？
- [ ] 需要哪些 token？`match` 关键字已在关键词表中，`=>` (FatArrow) token 已被词法分析器识别
