# Code Review — Zero Transpiler

## 一、架构设计审查

### 1. AST 设计的生命周期隐患

`parser.cppm` 中所有 AST 节点使用 `Span` 存储信息而非解析后的字符串。代码生成时通过 `text_at(source, span)` 还原。这在 parser 阶段零拷贝、高效，但调用方必须确保 source 的生命周期覆盖 AST 的使用。当前单文件 transpile 场景下没问题，未来如果需要缓存 AST 或跨模块分析，这会成为 bug 来源。

**结论**：目前不需要改，但值得在注释或 CLAUDE.md 中说明 Span 的生命周期依赖。

```
driver/
  cli.cppm         — 入口：__zero_main__() 纯路由、dispatch() 命令执行
  command.cppm     — 命令注册表：Command/Flag 类型、COMMANDS、find_command()、help 渲染
  handler.cppm     — 命令实现：run/tokens/ast/check/build（接收 const Driver&）
  pipeline.cppm    — 转译核心：Driver 配置 + parse_cli_args() + transpile() + run_single_file()
  toolchain.cppm   — C++ 工具链：编译、产物路径管理
```

---

## 二、逐文件代码审查

### `src/common/source.cppm`

- `Span.end` 是 exclusive 语义，但未在注释中说明。所有 parser 和 codegen 代码依赖这个语义。
- `text_at` 没有边界检查（span.end > text.size()）。当前调用方均来自 parser，保证 span 合法。
- `Span` 的零值（start=0, end=0）被用作 "不存在" 的语义（如 `FunctionItem.return_type`），建议用 `std::optional<Span>` 或 `is_valid()` 让意图更明确。

### `src/common/process.cppm`

整体实现正确且惯用：`fork`/`execvp` 的 Unix 路径处理了 `EINTR`，信号退出码映射用 `128 + WTERMSIG` 符合 shell 惯例。

- **`const_cast` 可以通过签名消除**：`run_process` 参数为 `std::span<const std::string>`，导致 `arg.c_str()` 返回 `const char*`，被迫 `const_cast` 来匹配 `execvp(char* const [])`。改为 `std::span<std::string>` 后 `arg.data()` 直接返回 `char*`，无需 const_cast。调用方 pipeline.cppm 传的是 `std::vector<std::string>`，签名兼容，零影响。
- **fork / waitpid 失败时静默丢弃 errno**：`fork()` 失败和 `waitpid` 非 `EINTR` 失败均返回 `{started=false, exit_code=1}`，调用方无法区分"没启动"和"启动了但返回码恰好是 1"，也无法知道失败原因（EAGAIN / ENOMEM / ECHILD 等）。应至少打印 errno 到 stderr。
- **Windows 命令行转义不完整**：只检查了空格和 tab，未处理参数内的双引号、`%`、尾部反斜杠等特殊字符。当前调用方只传 compiler flag 和文件路径，暂不触发，先标注。
- **无超时机制**：`WaitForSingleObject(INFINITE)` 和 `waitpid(status, 0)` 永久阻塞。当前场景概率低，YAGNI，等需要时再加。
- **Windows 编码**：`CreateProcessA` 使用 ANSI 编码，非 ASCII 路径会出问题。见下方优先级表。

### `src/frontend/lexer.cppm`

- **性能**：`identifier_or_keyword()` 对 17 个关键字做线性搜索。对当前文件规模不是瓶颈但需要知晓。
- 只支持 `//` 行注释，不支持 `/* */` 块注释（如果是有意设计，建议加注释说明）。

### `src/frontend/parser.cppm`

- `return std::move(result)` 阻止 NRVO。在 `parse_enum`、`parse_struct` 等中同样存在。建议去掉 `std::move`。
- 有基本的错误同步机制（`synchronize_top_level` / `synchronize_to_item_end`），属于好的实践。
- `advance()` 返回 `std::optional<Token>` 但所有调用方都丢弃返回值，建议改为 `void`。

### `src/backend/codegen.cppm`

- **bug**：`generate_function` 中硬编码 `main` 返回 `int`，如果用户写了 `fn main() -> i32`，codegen 会静默覆盖为 `int`。应该优先使用用户声明的返回类型，只在无声明时默认 `int`。
- **Type mapping 的 fallback**：未知类型直接 pass through，让 C++ 编译器报错——可行但错误信息对用户不友好。
- **巧妙设计**：用 `#include "headers/base.inc"` 在编译期将 std headers 嵌入 `constexpr std::string_view`。

---

## 三、全局问题

### 1. 单文件模型 vs 多文件项目

Parser 支持 `import` 语句，但没有解析被导入的 `.zero` 文件。当前 `import` 是纯 pass-through（生成 C++ module import）。这个设计选择应在文档中说明。

### 2. 错误报告不友好

错误报告格式为 `error: <message> [<start>..<end>]`，给出字节偏移而非行列号。应显示 `filename:line:col:` 格式。

### 3. TODO.md 和代码的同步

lexer 已识别 `if`、`else`、`while`、`for`、`return` 等 keywords 但 parser 不认识它们（函数体做 source-slice passthrough）。这是 OK 的——这些 keywords 在函数体内使用。

---

## 四、问题优先级

| 优先级 | 问题 | 状态 |
|--------|------|------|
| **P0** | `generate_function` 中 `main` 返回类型静默覆盖 | 待修复 |
| **P1** | 错误报告使用字节偏移非行列号 | 待修复 |
| **P1** | `Span` 零值表示 "不存在" 的隐式约定 | 待修复 |
| **P2** | parser 返回值 `std::move` 阻止 NRVO | 待修复 |
| **P2** | `advance()` 返回 `std::optional` 但无人使用 | 待修复 |
| **P2** | process.cppm: `const_cast` 通过改签名为 `span<string>` 消除 | 待修复 |
| **P2** | process.cppm: fork/waitpid 失败时打印 errno | 待修复 |
| **P3** | `CreateProcessA` ANSI 编码问题 | 待修复 |
| **P3** | process.cppm: Windows 命令行转义不完整 | 待修复 |
| **P3** | 注释风格完善（span exclusive 语义、仅行注释） | 待修复 |
