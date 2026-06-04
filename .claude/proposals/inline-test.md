# 内联测试

## 目标

在 Carven 源文件中直接编写单元测试，类似 Rust 的 `#[test]` 或 Zig 的 `test` 块。

## 候选语法

### 选项 A：C 风格预处理器指令

```carven
#test "test name" {
    let x = add(1, 2);
    assert(x == 3);
}
```

### 选项 B：Rust 式属性

```carven
#[test]
fn test_addition() {
    let x = add(1, 2);
    assert(x == 3);
}
```

### 选项 C：关键字

```carven
test "test addition" {
    let x = add(1, 2);
    assert(x == 3);
}
```

### 选项 D：C++ 式属性

```carven
[[test]]
fn test_addition() {
    let x = add(1, 2);
    assert(x == 3);
}
```

## 对比

| 维度 | A (`#test`) | B (`#[test]`) | C (`test` 关键字) | D (`[[test]]`) |
|------|-------------|---------------|-------------------|-----------------|
| 简洁性 | 高 | 中 | 高 | 低 |
| 与 C/C++ 预处理器风格一致 | 是 | 否 | 否 | 否 |
| 与 Rust 属性风格一致 | 否 | 是 | 否 | 部分 |
| 与 Zig 测试风格一致 | 否 | 否 | 是 | 否 |
| 符合 C++ 属性语法 | 否 | 否 | 否 | 是 |
| 需要新增 token | `#test` | `#[test]` | `test` (关键字) | `[[test]]` (复杂) |

## 待定事项

- [ ] 语法选择
- [ ] 是否需要 `assert` 内置？还是依赖 C++ 的 `assert` 宏？
- [ ] 是否支持测试函数参数（如 fixtures）？
- [ ] 是否支持 `should_panic` / `#[should_fail]` 等特殊标记？
