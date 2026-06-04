# Carven 特性路线图

状态：`proposed` → `planned` → `in-progress` → `done`

## 基础特性（已完成）

| 特性 | 状态 |
|------|------|
| `import` 模块导入（含 using 声明/通配符/大括号列表） | done |
| `fn` 函数定义（含类型标注/无类型参数） | done |
| `let`/`var`/`const` 变量声明 | done |
| `enum` 枚举（含可选底层类型） | done |
| `struct` 结构体 | done |
| `if`/`else` 表达式 | done |
| `while` 循环 | done |
| `for` 循环（C 风格） | done |
| `return` 语句 | done |
| 完整表达式系统（前缀/中缀/后缀/赋值/复合赋值） | done |
| 13 种内置类型映射 | done |
| 代码生成（含 C++14-26 五级 #include 头文件） | done |

## 待实现特性

| 特性 | 状态 | 提案 |
|------|------|------|
| 数组字面量 `[1, 2, 3]` + 数组类型 `[i32; 3]` | done | [array-literals.md](proposals/array-literals.md) |
| Range for 循环 `for i in 1..10` | proposed | [range-for.md](proposals/range-for.md) |
| `match` 表达式 | done | [match.md](proposals/match.md) |
| 文档注释 `//!` / `///` | proposed | [doc-comments.md](proposals/doc-comments.md) |
| 内联测试 | proposed | [inline-test.md](proposals/inline-test.md) |
| 内联 C++ 代码 | proposed | [inline-cpp.md](proposals/inline-cpp.md) |

## 未来方向

以下特性尚未有具体提案，但属于语言规划的一部分：

- 结构体字面量 `Point { x: 1, y: 2 }`
- 泛型 / 模板
- Trait / 接口
- Lambda 表达式
- 枚举变体携带数据
- 模块级 `const` 和 `static`
- `extern` 函数声明
- 多文件项目支持
- `export` 模块导出
- `as` 类型转换
