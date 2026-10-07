module carven:test.internal.semantic.analysis.async_contracts;

import :diagnostics.code;
import :frontend.program.parse;
import :semantic.analyze;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.diagnostics;
import :test.harness.framework;
import std;

namespace {

struct ContractCase final {
    std::string_view name;
    std::string_view source;
    std::optional<DiagnosticCode> rejection;
};

auto check_contract(const ContractCase& input) noexcept -> void {
    auto sources = SourceManager();
    const auto application = sources.append_virtual("contracts.cv", std::string(input.source));
    const auto standard = sources.append_virtual("async.cv", "");
    require(application.has_value());
    require(standard.has_value());
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *application,
            .module_path = *CanonicalModulePath::from_value("contracts")
        },
        SourceModuleInput {
            .source_id = *standard,
            .module_path = *CanonicalModulePath::from_value("crafts.carven.std.async")
        },
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    if (!expect(syntax.has_value()).note("source = ", input.source)) {
        return;
    }
    const auto result = analyze(std::move(*syntax));
    if (!expect_equal(result.has_value(), !input.rejection).note("source = ", input.source)) {
        return;
    }
    if (input.rejection) {
        expect_diagnostic(result.error(), *input.rejection).note("source = ", input.source);
    }
}

const TestSuite suite([] static noexcept {
    "Async templates: static arguments select residual cold bodies"_test = [] static noexcept {
        const auto cases = std::array {
            ContractCase {
                .name = "selected yield with dynamic runtime input",
                .source = R"(import std::async using yield_once;
private async fn leaf(value: i32, const deferred: bool) -> i32 {
    const if deferred { await yield_once(); } return value;
}
async fn run(value: i32) -> i32 {
    let ready = leaf(value, false); let deferred = leaf(value, true);
    return (await ready) + (await deferred);
})",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "static text is not retained by a cold activation",
                .source = R"(async fn leaf(const text: str) -> i32 => 7;
async fn run() { print(await leaf("selected")); })",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "nested static calls share the ordinary instance mechanism",
                .source =
                    R"(private async fn leaf(value: i32, const extra: i32) -> i32 => value + extra;
private async fn outer(value: i32, const extra: i32) -> i32 => await leaf(value, extra);
async fn run(value: i32) -> i32 => await outer(value, 3);)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "recursive static selection reaches a residual base case",
                .source = R"(private async fn count(const remaining: i32) -> i32 {
    const if remaining == 0 { return 0; }
    else { return 1 + await count(remaining - 1); }
}
async fn run() -> i32 => await count(3);)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "runtime inputs do not acquire static admission",
                .source = R"(async fn leaf(const value: i32) -> i32 => value;
async fn run(value: i32) -> i32 => await leaf(value);)",
                .rejection = DiagnosticCode::ConstAdmission
            },
            ContractCase {
                .name = "unselected source arms still undergo ordinary checking",
                .source = R"(async fn leaf(const selected: bool) -> i32 {
    const if selected { return missing; } else { return 7; }
}
async fn run() -> i32 => await leaf(false);)",
                .rejection = DiagnosticCode::NameUnresolved
            },
            ContractCase {
                .name = "runtime slice capture remains outside async admission",
                .source = "async fn leaf(values: [i32], const selected: bool) -> i32 => 7;",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "source export cannot expose unbound static parameters",
                .source = "export(cpp) async fn leaf(const value: i32) -> i32 => value;",
                .rejection = DiagnosticCode::ConstAdmission
            },
            ContractCase {
                .name = "marked source bodies admit cooperative yield",
                .source = "import std::async using yield_once; "
                          "const async fn leaf() { await yield_once(); } const { await leaf(); }",
                .rejection = std::nullopt
            },
        };
        each(cases, &ContractCase::name, check_contract);
    };

    "Async native contracts: signatures and cold source backing"_test = [] static noexcept {
        const auto cases = std::array {
            ContractCase {
                .name = "native import and source export share async completion",
                .source = R"(export struct Error { code: i32 }
private import(cpp) async fn provider(context: ::Context) -> i32 throw Error;
export(cpp) async fn run(context: ::Context) -> i32 throw Error {
    return await provider(context)?;
})",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "cold native Read keeps the holder until observation",
                .source = R"(private import(cpp) fn make_context() -> ::Context;
private import(cpp) async fn provider(context: ::Context) -> i32;
async fn run() {
    var context = make_context(); let operation = provider(context);
    print(await operation); let moved = &&context;
})",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "cold native Read forbids taking retained holder",
                .source = R"(private import(cpp) fn make_context() -> ::Context;
private import(cpp) async fn provider(context: ::Context) -> i32;
async fn run() {
    var context = make_context(); let operation = provider(context);
    let moved = &&context; print(await operation);
})",
                .rejection = DiagnosticCode::AccessBorrowConflict
            },
            ContractCase {
                .name = "cold native Read forbids replacing retained holder",
                .source = R"(private import(cpp) fn make_context() -> ::Context;
private import(cpp) async fn provider(context: ::Context) -> i32;
async fn run() {
    var context = make_context(); let operation = provider(context);
    context = make_context(); print(await operation);
})",
                .rejection = DiagnosticCode::AccessBorrowConflict
            },
            ContractCase {
                .name = "synchronous factory preserves native Read holder identity",
                .source = R"(private import(cpp) fn make_context() -> ::TrivialContext;
private import(cpp) async fn provider(context: ::TrivialContext) -> i32;
fn factory(context: ::TrivialContext) => provider(context);
async fn run() {
    var context = make_context(); let operation = factory(context);
    print(await operation); let moved = &&context;
})",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "factory native Read loan reaches the original caller holder",
                .source = R"(private import(cpp) fn make_context() -> ::TrivialContext;
private import(cpp) async fn provider(context: ::TrivialContext) -> i32;
fn factory(context: ::TrivialContext) => provider(context);
async fn run() {
    var context = make_context(); let operation = factory(context);
    let moved = &&context; print(await operation);
})",
                .rejection = DiagnosticCode::AccessBorrowConflict
            },
            ContractCase {
                .name = "factory cannot return a borrow of its local native owner",
                .source = R"(private import(cpp) fn make_context() -> ::Context;
private import(cpp) async fn provider(context: ::Context) -> i32;
fn factory() { let context = make_context(); return provider(context); })",
                .rejection = DiagnosticCode::AccessBorrowConflict
            },
            ContractCase {
                .name = "native Read without a known source holder is not admitted",
                .source = R"(private import(cpp) fn make_context() -> ::Context;
private import(cpp) async fn provider(context: ::Context) -> i32;
async fn run() { print(await provider(make_context())); })",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "native containing aggregate Read retains containing owner",
                .source = R"(struct Holder { context: ::Context }
private import(cpp) fn make_holder() -> Holder;
private import(cpp) async fn provider(holder: Holder) -> i32;
async fn run() {
    var holder = make_holder(); let operation = provider(holder);
    let moved = &&holder; print(await operation);
})",
                .rejection = DiagnosticCode::AccessBorrowConflict
            },
            ContractCase {
                .name = "child closure releases native Read holder",
                .source = R"(import std::async using cancel;
private import(cpp) fn make_context() -> ::Context;
private import(cpp) async fn provider(context: ::Context) -> i32;
async fn run() {
    var context = make_context();
    if true { async let child = provider(context); cancel(child); }
    let moved = &&context;
})",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "bodyless native async pointer input is rejected",
                .source = "private import(cpp) async fn provider(value: ptr<i32>);",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "bodyless native async slice input is rejected",
                .source = "private import(cpp) async fn provider(value: [i32]);",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "bodyless native async opaque Take is rejected",
                .source = "private import(cpp) async fn provider(&&value: ::Context);",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "bodyless native async pointer result is rejected",
                .source = "private import(cpp) async fn provider() -> ptr<i32>;",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "bodyless native async borrowed failure is rejected",
                .source = "struct Error { message: str } "
                          "private import(cpp) async fn provider() throw Error;",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "native async still rejects static parameters",
                .source = "private import(cpp) async fn provider(const value: i32);",
                .rejection = DiagnosticCode::ConstAdmission
            },
            ContractCase {
                .name = "native async still rejects const functions",
                .source = "private import(cpp) async const fn provider();",
                .rejection = DiagnosticCode::ConstAdmission
            },
        };
        each(cases, &ContractCase::name, check_contract);
    };
    "Async contracts: guards and rethrow carry consumption to real successors"_test =
        [] static noexcept {
            const auto cases = std::array {
                ContractCase {
                    .name = "match rejection keeps guard consumption",
                    .source = R"(async fn leaf() -> i32 => 1;
async fn run(value: i32) {
    let operation = leaf();
    match value { _ if await operation == 0 => {}, _ => { print(await operation); } }
})",
                    .rejection = DiagnosticCode::AccessUnavailable
                },
                ContractCase {
                    .name = "match fallback uses a fresh owner",
                    .source = R"(async fn leaf() -> i32 => 1;
async fn run(value: i32) {
    let operation = leaf();
    match value { _ if await operation == 0 => {}, _ => { print(await leaf()); } }
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "catch rejection keeps guard consumption",
                    .source = R"(struct Failure { code: i32 }
async fn leaf() -> i32 => 1;
async fn fail() throw Failure { throw Failure { code: 2 }; }
async fn run() {
    let operation = leaf();
    try { await fail()?; } catch {
        Failure(_) if await operation == 0 => {},
        Failure(_) => { print(await operation); },
    }
})",
                    .rejection = DiagnosticCode::AccessUnavailable
                },
                ContractCase {
                    .name = "catch fallback uses a fresh owner",
                    .source = R"(struct Failure { code: i32 }
async fn leaf() -> i32 => 1;
async fn fail() throw Failure { throw Failure { code: 2 }; }
async fn run() {
    let operation = leaf();
    try { await fail()?; } catch {
        Failure(_) if await operation == 0 => {},
        Failure(_) => { print(await leaf()); },
    }
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "operand failure precedes consumption of a later argument",
                    .source = R"(struct Failure {}
fn input(flag: bool) -> i32 throw Failure {
    if flag { throw Failure {}; }
    return 1;
}
async fn leaf() -> i32 => 2;
async fn sum(first: i32, second: i32) -> i32 => first + second;
async fn run(flag: bool) {
    let operation = leaf();
    try { print(await sum(input(flag)?, await operation)); } catch {
        Failure(_) => { print(await operation); },
    }
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "completion failure follows consumption of a later argument",
                    .source = R"(struct Failure {}
async fn leaf() -> i32 => 2;
async fn sum(first: i32, second: i32) -> i32 throw Failure { throw Failure {}; }
async fn run() {
    let operation = leaf();
    try { print(await sum(1, await operation)?); } catch {
        Failure(_) => { print(await operation); },
    }
})",
                    .rejection = DiagnosticCode::AccessUnavailable
                },
                ContractCase {
                    .name = "operand failure preserves nullability before a later Write",
                    .source = R"(struct Failure {}
fn input(flag: bool) -> i32 throw Failure {
    if flag { throw Failure {}; }
    return 1;
}
fn clear(&pointer: ptr<i32>) -> i32 { pointer = nullptr; return 2; }
async fn sum(first: i32, second: i32) -> i32 => first + second;
async fn run(flag: bool) -> i32 {
    let value = 3;
    var pointer: ptr<i32> = addressof(value);
    return try { await sum(input(flag)?, clear(&pointer)) } catch {
        Failure(_) => *pointer,
    };
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "completion failure follows nullability invalidation by a Write",
                    .source = R"(struct Failure {}
fn clear(&pointer: ptr<i32>) -> i32 { pointer = nullptr; return 2; }
async fn sum(first: i32, second: i32) -> i32 throw Failure { throw Failure {}; }
async fn run() -> i32 {
    let value = 3;
    var pointer: ptr<i32> = addressof(value);
    return try { await sum(1, clear(&pointer))? } catch {
        Failure(_) => *pointer,
    };
})",
                    .rejection = DiagnosticCode::PointerNonNull
                },
                ContractCase {
                    .name = "partial catch joins only its actual failure paths",
                    .source = R"(enum Failure { First, Second }
async fn leaf() -> i32 => 1;
async fn fail() throw Failure { throw Failure::First; }
async fn run() {
    let operation = leaf();
    try { await fail()?; } catch {
        Failure(.First) => { print(await operation); },
        Failure(.Second) => {},
    }
    print(await operation);
})",
                    .rejection = DiagnosticCode::AccessUnavailable
                },
                ContractCase {
                    .name = "partial catch can consume separate owners",
                    .source = R"(enum Failure { First, Second }
async fn leaf() -> i32 => 1;
async fn fail() throw Failure { throw Failure::First; }
async fn run() {
    try { await fail()?; } catch {
        Failure(.First) => { print(await leaf()); },
        Failure(.Second) => {},
    }
    print(await leaf());
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "rethrow carries consumption outward",
                    .source = R"(struct Failure {}
async fn leaf() -> i32 => 1;
async fn fail() throw Failure { throw Failure {}; }
async fn run() {
    let operation = leaf();
    try {
        try { await fail()?; } catch { Failure(_) => { print(await operation); rethrow; }, }
    } catch { Failure(_) => { print(await operation); }, }
})",
                    .rejection = DiagnosticCode::AccessUnavailable
                },
                ContractCase {
                    .name = "outer recovery can use a fresh owner",
                    .source = R"(struct Failure {}
async fn leaf() -> i32 => 1;
async fn fail() throw Failure { throw Failure {}; }
async fn run() {
    let operation = leaf();
    try {
        try { await fail()?; } catch { Failure(_) => { print(await operation); rethrow; }, }
    } catch { Failure(_) => { print(await leaf()); }, }
})",
                    .rejection = std::nullopt
                },
            };
            each(cases, &ContractCase::name, check_contract);
        };

    "Async contracts: loop cycles preserve single consumption"_test = [] static noexcept {
        const auto cases = std::array {
            ContractCase {
                .name = "header owner cannot be consumed on a backedge",
                .source = "async fn leaf() -> i32 => 1; async fn run() { "
                          "for let operation = leaf(); true; { print(await operation); } }",
                .rejection = DiagnosticCode::AccessUnavailable
            },
            ContractCase {
                .name = "condition owner cannot be repeatedly consumed",
                .source = "async fn leaf() -> i32 => 1; async fn run() { "
                          "for let operation = leaf(); await operation > 0; {} }",
                .rejection = DiagnosticCode::AccessUnavailable
            },
            ContractCase {
                .name = "outer owner cannot be consumed by repeated body",
                .source =
                    "async fn leaf() -> i32 => 1; async fn run() { let operation = leaf(); "
                    "for var index: i32 = 0; index < 2; ++index { print(await operation); } }",
                .rejection = DiagnosticCode::AccessUnavailable
            },
            ContractCase {
                .name = "continue executes its consuming step",
                .source =
                    "async fn leaf() -> i32 => 1; async fn run() { let operation = leaf(); "
                    "for var index: i32 = 0; index < 2; index += await operation { continue; } }",
                .rejection = DiagnosticCode::AccessUnavailable
            },
            ContractCase {
                .name = "break consumes once without a backedge",
                .source = "async fn leaf() -> i32 => 1; async fn run() { "
                          "for let operation = leaf(); true; { print(await operation); break; } }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "condition consumes once when its body breaks",
                .source = "async fn leaf() -> i32 => 1; async fn run() { "
                          "for let operation = leaf(); await operation > 0; { break; } }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "return consumes once without a backedge",
                .source =
                    "async fn leaf() -> i32 => 1; async fn run() -> i32 { "
                    "for let operation = leaf(); true; { return await operation; } return 0; }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "iteration constructs a fresh owner",
                .source = "async fn leaf() -> i32 => 1; async fn run() { "
                          "for var index: i32 = 0; index < 2; ++index { "
                          "let operation = leaf(); print(await operation); } }",
                .rejection = std::nullopt
            },
        };
        each(cases, &ContractCase::name, check_contract);
    };

    "Ownership contracts: proven truth selects normal successors"_test = [] static noexcept {
        const auto cases = std::array {
            ContractCase {
                .name = "true branch consumes a scalar owner",
                .source =
                    "fn run() { let value = 1; if true { let moved = &&value; } print(value); }",
                .rejection = DiagnosticCode::AccessUnavailable
            },
            ContractCase {
                .name = "false branch preserves a scalar owner",
                .source =
                    "fn run() { let value = 1; if false { let moved = &&value; } print(value); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "true short circuit executes a consuming operand",
                .source = "fn consume(&&value: i32) -> bool => true; fn run() { let value = 1; "
                          "if true && consume(&&value) {} print(value); }",
                .rejection = DiagnosticCode::AccessUnavailable
            },
            ContractCase {
                .name = "false short circuit skips a consuming operand",
                .source = "fn consume(&&value: i32) -> bool => true; fn run() { let value = 1; "
                          "if false && consume(&&value) {} print(value); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "true branch dereferences a known null pointer",
                .source = "fn run() -> i32 { var pointer: ptr<i32> = nullptr; "
                          "if true { return *pointer; } return 0; }",
                .rejection = DiagnosticCode::PointerNonNull
            },
            ContractCase {
                .name = "false branch skips a null pointer dereference",
                .source = "fn run() -> i32 { var pointer: ptr<i32> = nullptr; "
                          "if false { return *pointer; } return 0; }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "true loop observes its header child on the only normal exit",
                .source = "async fn leaf() -> i32 => 1; async fn run() { "
                          "for async let child = leaf(); true; { print(await child); break; } }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "false loop leaves its header child without intent",
                .source = "async fn leaf() -> i32 => 1; async fn run() { "
                          "for async let child = leaf(); false; { print(await child); break; } }",
                .rejection = DiagnosticCode::AsyncChildIntent
            },
        };
        each(cases, &ContractCase::name, check_contract);
    };

    "Async contracts: projected lifetime loans distinguish sibling storage"_test =
        [] static noexcept {
            const auto cases = std::array {
                ContractCase {
                    .name = "sibling native Write preserves retained scalar backing",
                    .source = R"(struct Pair { scalar: i32, text: String }
private import(cpp) fn mutate(&value: String);
async fn leaf(&value: i32) -> i32 => value;
async fn run() {
    var pair = Pair { scalar: 1, text: "first" };
    let operation = leaf(&pair.scalar);
    mutate(&pair.text);
    print(await operation);
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "sibling source replacement preserves retained scalar backing",
                    .source = R"(struct Pair { scalar: i32, text: String }
async fn leaf(&value: i32) -> i32 => value;
async fn run() {
    var pair = Pair { scalar: 1, text: "first" };
    let operation = leaf(&pair.scalar);
    pair.text = "second";
    print(await operation);
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "whole owner replacement overlaps retained scalar backing",
                    .source = R"(struct Pair { scalar: i32, text: String }
async fn leaf(&value: i32) -> i32 => value;
async fn run() {
    var pair = Pair { scalar: 1, text: "first" };
    let operation = leaf(&pair.scalar);
    pair = Pair { scalar: 2, text: "second" };
    print(await operation);
})",
                    .rejection = DiagnosticCode::AccessBorrowConflict
                },
                ContractCase {
                    .name = "whole owner Take overlaps retained scalar backing",
                    .source = R"(struct Pair { scalar: i32, text: String }
async fn leaf(&value: i32) -> i32 => value;
async fn run() {
    var pair = Pair { scalar: 1, text: "first" };
    let operation = leaf(&pair.scalar);
    let moved = &&pair;
    print(await operation);
})",
                    .rejection = DiagnosticCode::AccessBorrowConflict
                },
            };
            each(cases, &ContractCase::name, check_contract);
        };

    "Async contracts: returned backing and exit intent follow actual ownership"_test =
        [] static noexcept {
            const auto cases = std::array {
                ContractCase {
                    .name = "wrapper retains only its returned operation backing",
                    .source = R"(async fn leaf(&value: i32) -> i32 => value;
fn wrapped(&used: i32, &discarded: i32) {
    let unused = leaf(&discarded);
    return leaf(&used);
}
async fn run() {
    var used = 1;
    var discarded = 2;
    let operation = wrapped(&used, &discarded);
    let moved = &&discarded;
    print(await operation);
    print(moved);
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "returned backing remains protected",
                    .source = R"(async fn leaf(&value: i32) -> i32 => value;
fn wrapped(&value: i32) => leaf(&value);
async fn run() {
    var value = 1;
    let operation = wrapped(&value);
    let moved = &&value;
    print(await operation);
})",
                    .rejection = DiagnosticCode::AccessBorrowConflict
                },
                ContractCase {
                    .name = "one reaching normal path lacks child intent",
                    .source = R"(import std::async using cancel;
async fn leaf() -> i32 => 1;
async fn run(flag: bool) { async let child = leaf(); if flag { cancel(child); } })",
                    .rejection = DiagnosticCode::AsyncChildIntent
                },
                ContractCase {
                    .name = "observation or request covers every normal path",
                    .source = R"(import std::async using cancel;
async fn leaf() -> i32 => 1;
async fn run(flag: bool) { async let child = leaf();
    if flag { cancel(child); } else { print(await child); }
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "request does not release backing before return operand",
                    .source = R"(import std::async using cancel;
async fn leaf(&value: i32) -> i32 => value;
async fn run() -> i32 {
    var value = 1; async let child = leaf(&value); cancel(child); return &&value;
})",
                    .rejection = DiagnosticCode::AccessBorrowConflict
                },
                ContractCase {
                    .name = "scalar return observation precedes required closure",
                    .source = R"(import std::async using cancel;
async fn leaf(&value: i32) -> i32 => value;
async fn run() -> i32 {
    var value = 1; async let child = leaf(&value); cancel(child); return value;
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "native Write without a retained source loan",
                    .source = "private import(cpp) fn mutate(&value: i32); "
                              "async fn run() { var value = 1; mutate(&value); }",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "native destructive Write conflicts with known backing",
                    .source = "private import(cpp) fn mutate(&value: i32); "
                              "async fn leaf(&value: i32) -> i32 => value; async fn run() { "
                              "var value = 1; let operation = leaf(&value); mutate(&value); "
                              "print(await operation); }",
                    .rejection = DiagnosticCode::AccessBorrowConflict
                },
            };
            each(cases, &ContractCase::name, check_contract);
        };
});

} // namespace
