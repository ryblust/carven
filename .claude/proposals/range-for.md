# Range For 循环

## 目标

支持两种新的 for 循环语法：
1. **Range 循环**：`for i in 1..10` — 基于 `std::views::iota` 的整数范围迭代
2. **For-each 循环**：`for data in datas` — 基于 C++ range-for 的容器迭代

## 语法

### Range 循环

```carven
for i in 1..10 {
    std::println("{}", i);
}
```

#### 转译输出

```cpp
for (auto&& i : std::views::iota(1, 10)) {
    std::println("{}", i);
}
```

### For-each 循环

```carven
var datas = [1, 2, 3];

for data in datas {
    data += 2;
}
```

#### 转译输出

```cpp
auto datas = std::array{1, 2, 3};

for (auto&& data : datas) {
    data += 2;
}
```

## 待定事项

- [ ] Range 语法：`1..10` 是左闭右开？是否需要 `1..=10` 表示左闭右闭？
- [ ] 是否需要 `..10` 表示 `0..10`？
- [ ] 是否需要步长支持，如 `1..10:2`（步长为2）？
- [ ] For-each 中迭代变量的可变性：默认 `auto&&` 还是用关键字区分 `for data in` vs `for &data in`？
- [ ] 现有的 C 风格 `for (;;)` 是否保留？

## 参考

- Rust: `for i in 1..10` → range expression
- C++20: `std::views::iota(1, 10)` → lazy integer range
- C++11: `for (auto&& x : container)` → range-based for
