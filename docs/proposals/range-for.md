# Range For Loop

## Goal

Support two new for loop syntaxes:
1. **Range loop**: `for i in 1..10` — integer range iteration based on `std::views::iota`
2. **For-each loop**: `for data in datas` — container iteration based on C++ range-for

## Syntax

### Range Loop

```carven
for i in 1..10 {
    std::println("{}", i);
}
```

#### Transpiled Output

```cpp
for (auto&& i : std::views::iota(1, 10)) {
    std::println("{}", i);
}
```

### For-each Loop

```carven
var datas = [1, 2, 3];

for data in datas {
    data += 2;
}
```

#### Transpiled Output

```cpp
auto datas = std::array{1, 2, 3};

for (auto&& data : datas) {
    data += 2;
}
```

## Open Questions

- [ ] Range syntax: is `1..10` left-inclusive, right-exclusive? Do we need `1..=10` for inclusive range?
- [ ] Do we need `..10` to mean `0..10`?
- [ ] Do we need step size support, e.g., `1..10:2` (step of 2)?
- [ ] Mutability of iteration variables in for-each: default `auto&&`, or distinguish with keywords like `for data in` vs `for &data in`?
- [ ] Should the existing C-style `for (;;)` be retained?

## References

- Rust: `for i in 1..10` → range expression
- C++20: `std::views::iota(1, 10)` → lazy integer range
- C++11: `for (auto&& x : container)` → range-based for
