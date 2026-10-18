module carven:test.internal.semantic.analysis.async_contracts;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analyze;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.source.fixture;
import std;

namespace {

struct ContractCase final {
    std::string_view name;
    std::string_view source;
    std::optional<DiagnosticCode> rejection;
};

auto check_contract_modules(
    const ContractCase& input,
    std::span<const std::pair<std::string_view, std::string_view>> modules,
    std::span<const std::string_view> standard_modules = {}
) noexcept -> void {
    auto sources = SourceManager();
    const auto application = sources.append_virtual("contracts.cv", std::string(input.source));
    require(application.has_value());
    auto inputs = std::vector {
        SourceModuleInput {
            .source_id = *application,
            .module_path = *CanonicalModulePath::from_value("contracts")
        },
    };
    for (const auto& [path, source] : modules) {
        const auto id = sources.append_virtual(std::string(path) + ".cv", std::string(source));
        require(id.has_value());
        inputs.push_back({.source_id = *id, .module_path = *CanonicalModulePath::from_value(path)});
    }
    append_standard_sources(sources, inputs, standard_modules);
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    if (!expect(syntax.has_value()).note("source = ", input.source)) {
        return;
    }
    const auto result = analyze(std::move(*syntax), HostEffects::Deny);
    if (!expect_equal(result.has_value(), !input.rejection)
             .note("source = ", input.source)
             .note([&]() noexcept {
                 auto messages = std::string();
                 if (!result) {
                     for (const auto& diagnostic : result.error()) {
                         if (diagnostic.finding.severity == DiagnosticSeverity::Error) {
                             messages += std::format(
                                 "\n  {}: {}",
                                 diagnostic_code_info(diagnostic.finding.code).name,
                                 diagnostic.finding.message
                             );
                         }
                     }
                 }
                 return messages;
             })) {
        return;
    }
    if (input.rejection) {
        expect_diagnostic(result.error(), *input.rejection).note("source = ", input.source);
    }
}

auto check_contract(const ContractCase& input) noexcept -> void {
    check_contract_modules(input, {});
}

auto check_timeout_contract(const ContractCase& input) noexcept -> void {
    constexpr auto standard_modules = std::to_array<std::string_view>({"time"});
    auto source = std::string(
        "import std::time using { Duration, RangeError }; "
        "import std::time using Timeout; "
    );
    source += input.source;
    check_contract_modules(
        ContractCase {.name = input.name, .source = source, .rejection = input.rejection},
        {},
        standard_modules
    );
}

const TestSuite suite([] static noexcept {
    "Async contracts: generic definitions and concrete instances share admission"_test =
        [] static noexcept {
            constexpr auto cases = std::array {
                ContractCase {
                    .name = "unused generic Read slice is rejected at its definition",
                    .source = "async fn invalid<T>(values: [T]) {}",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "unused generic Take slice is rejected at its definition",
                    .source = "async fn invalid<T>(&&values: [T]) {}",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "known borrowed field rejects a rigid nominal input",
                    .source = R"(
struct View<T> { payload: T, text: str }
async fn invalid<T>(&&value: View<T>) {}
)",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "known borrowed result rejects a dependent product",
                    .source = R"(
async fn invalid<T>(&&value: T) -> (T, str) => (&&value, "view");
)",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "unknown input and result retain dependent admission",
                    .source = R"(
async fn forward<T>(&&value: T) -> T => &&value;
async fn inspect<T>(value: T) {}
async fn run() { print(await forward(&&7)); await inspect(9); }
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "instantiated borrowed Read input discharges the same requirement",
                    .source = R"(
async fn inspect<T>(value: T) {}
fn run(value: str) { let pending = inspect(value); }
)",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name =
                        "known native boundary rejects operation input in an unused generic body",
                    .source = R"(import std::async using Operation;

private import(cpp) fn inspect(value: Operation<i32>);
fn invalid<T>(value: Operation<i32>, marker: T) { inspect(value); }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "native import rejects operation input in an ordinary body",
                    .source = R"(import std::async using Operation;

private import(cpp) fn inspect(value: Operation<i32>);
fn invalid(value: Operation<i32>) { inspect(value); }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "native import rejects a nested operation owner",
                    .source = R"(import std::async using Operation;

struct Box { value: Operation<i32> }
private import(cpp) fn inspect(value: Box);
fn invalid<T>(value: Box, marker: T) { inspect(value); }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "source calls preserve typed operation Read and Take contracts",
                    .source = R"(import std::async using Operation;

fn inspect(value: Operation<i32>) {}
fn relay(&&value: Operation<i32>) -> Operation<i32> => &&value;
fn valid<T>(&&value: Operation<i32>, marker: T) -> Operation<i32> {
    inspect(value); return relay(&&value);
}
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "unused generic caller rejects a closed Take slice argument",
                    .source = R"(import std::async using Operation;

async fn sink<T>(&&value: T) {}
fn invalid<U>(&&values: [i32]) -> Operation<void> => sink<[i32]>(&&values);
)",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "unused generic caller rejects a closed Read borrowed argument",
                    .source = R"(import std::async using Operation;

async fn sink<T>(value: T) {}
fn invalid<U>(value: str) -> Operation<void> => sink<str>(value);
)",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "generic caller rejects known borrowed fields beside unknown payload",
                    .source = R"(import std::async using Operation;

struct View<T> { value: T, text: str }
async fn sink<T>(&&value: T) {}
fn invalid<U>(&&value: View<U>) -> Operation<void> => sink(&&value);
)",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "unused generic caller checks substituted completion backing",
                    .source = R"(import std::async using Operation;

async fn forward<T>(&&value: T) -> T => &&value;
fn invalid<U>(&&pending: Operation<i32>) -> Operation<Operation<i32>> {
    return forward<Operation<i32>>(&&pending);
}
)",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "dependent caller and closed owning arguments keep valid admission",
                    .source = R"(import std::async using Operation;

async fn sink<T>(&&value: T) {}
fn dependent<U>(&&value: U) -> Operation<void> => sink(&&value);
fn closed<U>(&&values: [i32; 1]) -> Operation<void> => sink<[i32; 1]>(&&values);
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "unused async definition rejects a direct test-stop effect",
                    .source = "async fn invalid<T>() { require(false); }",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "unused async definition rejects a concrete helper test-stop effect",
                    .source = R"(
fn stop() { fail(); }
async fn invalid<T>() { stop(); }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "unused async definition rejects transitive generic helper test-stop",
                    .source = R"(
fn stop<T>() { require(false); }
fn relay<T>() { stop<T>(); }
async fn invalid<T>() { relay<T>(); }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "recursive generic helper propagates test-stop through the same graph",
                    .source = R"(
fn stop<T>(again: bool) -> void {
    if again { stop<T>(false); } else { fail(); }
}
async fn invalid<T>() { stop<T>(true); }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "ordinary generic branches retain conservative test-stop effects",
                    .source = R"(
fn skipped<T>() { if false { fail(); } check(true); }
async fn invalid<T>() { skipped<T>(); }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name =
                        "ordinary helpers retain conservative test-stop effects in false branches",
                    .source = R"(
fn skipped() { if false { fail(); } check(true); }
async fn invalid() { skipped(); }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name =
                        "ordinary true branches retain later conditions and else report effects",
                    .source = R"(
fn stop() -> bool { require(false); return false; }
fn skipped<T>() {
    if true { check(true); } else if stop() { fail(); } else { require(false); }
}
async fn invalid<T>() { skipped<T>(); }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name =
                        "ordinary short circuit and loop nodes retain conservative test-stop effects",
                    .source = R"(
fn stop() -> bool { fail(); }
fn skipped<T>() {
    let conjunction = false && stop(); let disjunction = true || stop();
    for ; false; { fail(); }
}
async fn invalid<T>() { skipped<T>(); }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "executed false conditions retain their stopping call effect",
                    .source = R"(
fn stop() -> bool { require(false); return false; }
async fn invalid<T>() { if stop() { check(true); } }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "static selection removes test-stop effects for ordinary callers",
                    .source = R"(
fn skipped() { const if false { fail(); } check(true); }
async fn valid() { skipped(); }
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "generic callers share concrete helper static effect selection",
                    .source = R"(
fn skipped() { const if false { fail(); } check(true); }
async fn valid<T>() { skipped(); }
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "nonstopping generic helpers remain admissible",
                    .source = R"(
fn checked<T>() { check(true); }
async fn valid<T>() { checked<T>(); }
)",
                    .rejection = std::nullopt
                },
            };
            each(cases, &ContractCase::name, check_contract);
        };

    "Async owners: explicit signatures use ordinary Take and owning slots"_test = [] static noexcept {
        constexpr auto cases = std::array {
            ContractCase {
                .name = "public synchronous relay transfers a typed cold owner",
                .source = R"(import std::async using Operation;

async fn leaf() -> i32 => 7;
export fn relay(&&pending: Operation<i32>) -> Operation<i32> => &&pending;
async fn run() { let pending: Operation<i32> = relay(&&leaf()); print(await pending); }
)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "async forwarding preserves nominal failures",
                .source = R"(import std::async using Operation;

struct Error {}
async fn leaf() -> i32 throw Error { throw Error {}; }
async fn forward(&&pending: Operation<i32, throw Error>) -> i32 throw Error {
    return await pending?;
}
async fn run() { let value = try { await forward(&&leaf())? } catch { Error(_) => 9, }; }
)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "tuple record and array slots transfer complete owners",
                .source = R"(import std::async using Operation;

struct Box { pending: Operation<i32> }
async fn leaf() -> i32 => 7;
fn boxed(&&pending: Operation<i32>) -> Box => Box { pending: &&pending };
async fn run() {
    let box = boxed(&&leaf()); print(await (&&box).pending);
    let pair: (Operation<i32>, void) = (leaf(), ());
    let (pending, empty) = &&pair; print(await pending);
    let array: [Operation<i32>; 1] = [leaf()]; print(await (&&array)[0]);
}
)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "direct Read copy of a cold owner is unavailable",
                .source = R"(import std::async using Operation;

async fn leaf() -> i32 => 7;
fn relay(pending: Operation<i32>) -> Operation<i32> => pending;
)",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "ordinary Read parameters borrow operations without consuming them",
                .source = R"(import std::async using Operation;

async fn leaf() -> i32 => 7;
fn inspect(pending: Operation<i32>) -> bool => true;
async fn run() {
    let pending = leaf(); print(inspect(pending), inspect(pending)); print(await pending);
}
)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "ordinary Read array parameters permit readonly slice receivers",
                .source = R"(import std::async using Operation;

async fn leaf() -> i32 => 7;
fn inspect(values: [Operation<i32>; 1]) -> usize => values.slice().length();
fn inspect_slice(values: [Operation<i32>]) -> usize => values.length();
async fn run() {
    let values = [leaf()]; print(inspect(values), inspect(values), inspect_slice(values));
    print(await (&&values)[0]);
}
)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "unknown type Read forwarding borrows without requiring Copy",
                .source = R"(import std::async using Operation;

async fn leaf() -> i32 => 7;
fn inspect<T>(value: T) -> bool => true;
fn forward<T>(value: T) -> bool => inspect(value);
async fn run() {
    let pending: Operation<i32> = leaf(); print(forward(pending)); print(await pending);
}
)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "unknown array elements permit readonly slice receivers",
                .source = R"(import std::async using Operation;

async fn leaf() -> i32 => 7;
fn inspect<T>(values: [T; 1]) -> usize => values.slice().length();
async fn run() {
    let values: [Operation<i32>; 1] = [leaf()];
    print(inspect(values)); print(await (&&values)[0]);
}
)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "owning array and Vector indices deliver independent String values",
                .source = R"(
fn from_array(&&values: [String; 2]) -> String => (&&values)[0];
fn from_sequence(&&values: Vector<String>) -> String => (&&values)[0];
fn run() {
    let first = from_array(&&["first", "second"]);
    var values: Vector<String> = {}; values.push("kept");
    let second = from_sequence(&&values);
}
)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "consumed slice descriptor does not consume its operation element",
                .source = R"(
async fn leaf() -> i32 => 7;
async fn run() {
    let values = [leaf()]; let view = values.slice();
    print(await (&&view)[0]);
}
)",
                .rejection = DiagnosticCode::AccessTakeOperand
            },
            ContractCase {
                .name = "aggregate Read cannot copy an operation slot",
                .source = R"(import std::async using Operation;

struct Box { pending: Operation<i32> }
async fn leaf() -> i32 => 7;
fn run() { let box = Box { pending: leaf() }; let copied = box; }
)",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "Take relay retains the original backing until observation",
                .source = R"(import std::async using Operation;
import std::async using yield_once;

async fn leaf(&value: i32) -> i32 { await yield_once(); return value; }
fn relay(&&pending: Operation<i32>) -> Operation<i32> => &&pending;
async fn run() {
    var value = 7; let pending = relay(&&leaf(&value));
    let moved = &&value; print(await pending);
}
)",
                .rejection = DiagnosticCode::AccessBorrowConflict
            },
            ContractCase {
                .name = "async returned owner awaits completion relationship forwarding",
                .source = R"(import std::async using Operation;
import std::async using yield_once;

async fn leaf(&value: i32) -> i32 { await yield_once(); return value; }
async fn relay(&&pending: Operation<i32>) -> Operation<i32> => &&pending;
async fn run() {
    var value = 7; let pending = await relay(&&leaf(&value));
    let moved = &&value; print(await pending);
}
)",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "owner observation releases the transferred loan",
                .source = R"(import std::async using Operation;
import std::async using yield_once;

async fn leaf(&value: i32) -> i32 { await yield_once(); return value; }
fn relay(&&pending: Operation<i32>) -> Operation<i32> => &&pending;
async fn run() {
    var value = 7; let pending: Operation<i32> = relay(&&leaf(&value));
    print(await pending); let moved = &&value;
}
)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "operation failure arguments remain nominal",
                .source =
                    "import std::async using Operation;\n\nfn relay(&&pending: Operation<i32, throw i32>) {}",
                .rejection = DiagnosticCode::EffectThrowType
            },
            ContractCase {
                .name = "owner replacement releases only the previous retained backing",
                .source = R"(import std::async using Operation;

async fn leaf(&value: i32) -> i32 => value;
async fn run() {
    var first = 1; var second = 2;
    var pending: Operation<i32> = leaf(&first);
    pending = leaf(&second);
    let moved = &&first; print(await pending); let other = &&second;
}
)",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "owner replacement retains its new backing",
                .source = R"(import std::async using Operation;

async fn leaf(&value: i32) -> i32 => value;
async fn run() {
    var first = 1; var second = 2;
    var pending: Operation<i32> = leaf(&first);
    pending = leaf(&second);
    let moved = &&second; print(await pending);
}
)",
                .rejection = DiagnosticCode::AccessBorrowConflict
            },
            ContractCase {
                .name = "fixed type constructors reject failure-kind arguments",
                .source = "struct Error {} fn relay(value: Vector<throw Error>) {}",
                .rejection = DiagnosticCode::TypeGenericArguments
            },
        };
        each(cases, &ContractCase::name, check_contract);
    };

    "Async names: ordinary declarations and native selections precede builtins"_test = [] static noexcept {
        constexpr auto cases = std::array {
            ContractCase {
                .name = "module declaration selects its ordinary signature",
                .source =
                    "fn when_all(value: i32) -> i32 => value; fn run() -> i32 => when_all(7);",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "local callable shadows builtin",
                .source =
                    "import std::async using when_all;\n\nfn run() { let when_all = [](value: i32) -> i32 => value; let result: i32 = when_all(7); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "noncallable local does not fall back to builtin",
                .source =
                    "import std::async using when_all;\n\nfn run() { let when_all = 7; when_all(); }",
                .rejection = DiagnosticCode::TypeNotCallable
            },
            ContractCase {
                .name = "wrong source signature does not fall back to builtin",
                .source = "fn yield_once(value: i32) {} fn run() { yield_once(); }",
                .rejection = DiagnosticCode::TypeCallArity
            },
            ContractCase {
                .name = "constant root retains errors in the selected source signature",
                .source =
                    "const fn yield_once(value: i32) -> i32 => value; const result = yield_once();",
                .rejection = DiagnosticCode::TypeCallArity
            },
            ContractCase {
                .name = "user timeout needs no standard time provider",
                .source =
                    "const fn timeout(value: i32) -> i32 => value; const result: i32 = timeout(9); fn run() -> i32 => timeout(7);",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "constant roots select ordinary source declarations",
                .source =
                    "const fn cancel_requested() -> i32 => 11; const result: i32 = cancel_requested(); const { assert(result == 11); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "explicit native selection precedes builtin",
                .source = "import <native> using vendor::yield_once; fn run() { yield_once(7); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "constant root preserves explicit native selection",
                .source =
                    "import <native> using vendor::cancel_requested; const result = cancel_requested();",
                .rejection = DiagnosticCode::ConstAdmission
            },
            ContractCase {
                .name = "constant builtin precedes opened native fallback",
                .source =
                    "import std::async using cancel_requested; import <native> using vendor::*; const result: bool = cancel_requested();",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "opened native lookup remains a fallback",
                .source =
                    "import std::async using cancel_requested;\nimport std::async using yield_once;\n\nimport <native> using vendor::*; async fn run() { await yield_once(); let requested: bool = cancel_requested(); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "explicit native root retains its namespace",
                .source = "import <utility>; fn run() { ::async::when_all(); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "static roots construct builtins without source declarations",
                .source =
                    "import std::async using when_all;\n\nconst async fn leaf() -> i32 => 1; const result = await when_all(leaf());",
                .rejection = std::nullopt
            },
        };
        each(cases, &ContractCase::name, check_contract);
    };

    "Async names: await observers use the selected callable"_test = [] static noexcept {
        constexpr auto cases = std::array {
            ContractCase {
                .name = "grouped builtin observers remain direct await operands",
                .source =
                    "import std::async using wait_ready;\nimport std::async using close;\n\nasync fn leaf() {} async fn run() { async let child = leaf(); await ((wait_ready)(child)); await (close)(child); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "ordinary observer names retain generic inference",
                .source =
                    "async fn wait_ready<T>(value: T) {} async fn close<T>(value: T) {} async fn run() { await wait_ready(7); await close(true); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "local callable observer names use ordinary arguments",
                .source =
                    "import std::async using Operation;\nimport std::async using wait_ready;\n\nasync fn leaf(value: i32) -> i32 => value; async fn run() { let wait_ready = [](value: i32) -> Operation<i32> => leaf(value); let result: i32 = await wait_ready(7); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "local close binding blocks builtin observer",
                .source =
                    "import std::async using close;\n\nasync fn leaf() {} async fn run() { let close = 7; async let child = leaf(); await close(child); await child; }",
                .rejection = DiagnosticCode::TypeNotCallable
            },
            ContractCase {
                .name = "wrong observer signature does not select builtin",
                .source =
                    "async fn leaf() {} fn wait_ready() -> i32 => 1; async fn run() { async let child = leaf(); await wait_ready(child); await child; }",
                .rejection = DiagnosticCode::TypeCallArity
            },
            ContractCase {
                .name = "constant await uses ordinary source observer names",
                .source =
                    "const async fn wait_ready() -> i32 => 7; const async fn close() {} const value: i32 = await wait_ready(); const { await close(); assert(value == 7); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "explicit native observer is not restricted to lexical children",
                .source = "import <native> using vendor::wait_ready; fn run() { wait_ready(7); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "await does not reinterpret native call as builtin observer",
                .source =
                    "import <native> using vendor::wait_ready; async fn run() { await wait_ready(); }",
                .rejection = DiagnosticCode::AsyncAdmission
            },
        };
        each(cases, &ContractCase::name, check_contract);
    };

    "Async names: only visible module imports affect lookup"_test = [] static noexcept {
        constexpr auto modules = std::array {
            std::pair<std::string_view, std::string_view> {
                "provider",
                "export const fn yield_once() -> i32 => 9; "
                "export const fn cancel_requested() -> i32 => 11; "
                "export const fn when_all(value: i32) -> i32 => value; "
                "export const async fn wait_ready() -> i32 => 7; "
                "export const async fn close() {}"
            },
            std::pair<std::string_view, std::string_view> {
                "relay",
                "import provider using *; export fn marker() {}"
            }
        };
        constexpr auto cases = std::array {
            ContractCase {
                .name = "explicit Carven imports select source in bodies and constants",
                .source =
                    "import provider using { yield_once, cancel_requested }; const value: i32 = yield_once(); fn run() -> i32 => cancel_requested();",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "wildcard import applies to direct await observers",
                .source =
                    "import provider using *; const value: i32 = await wait_ready(); async fn run() { let value: i32 = await wait_ready(); await close(); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "unimported declarations do not suppress builtin lookup",
                .source =
                    "import std::async using cancel_requested;\nimport std::async using yield_once;\nimport std::async using when_all;\n\nconst value = await when_all(); async fn run() { await yield_once(); let requested: bool = cancel_requested(); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "imports do not leak through another module",
                .source =
                    "import std::async using cancel_requested;\nimport std::async using yield_once;\nimport std::async using when_all;\n\nimport relay using marker; fn prepare() { marker(); } const value = await when_all(); async fn run() { await yield_once(); let requested: bool = cancel_requested(); }",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "wrong imported signature does not fall back to builtin",
                .source = "import provider using when_all; async fn run() { await when_all(); }",
                .rejection = DiagnosticCode::TypeCallArity
            },
        };
        each(cases, &ContractCase::name, [&](const auto& input) noexcept {
            check_contract_modules(input, modules);
        });
    };

    "Async composition: builtin lookup preserves ordinary names and cold ownership"_test =
        [] static noexcept {
            constexpr auto cases = std::array {
                ContractCase {
                    .name = "heterogeneous join keeps nested and void elements",
                    .source = R"(import std::async using when_all;

async fn scalar() -> i32 => 1;
async fn unit() {}
async fn pair() -> (i32, bool) => (2, true);
async fn run() { let (number, nothing, nested) = await when_all(scalar(), unit(), pair()); let value: i32 = number; let empty: () = nothing; let pair_value: (i32, bool) = nested; }
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "zero and one input preserve arity",
                    .source = R"(import std::async using when_all;

async fn leaf() -> i32 => 1;
async fn run() { let empty: () = await when_all(); let (only,) = await when_all(leaf()); let value: i32 = only; }
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "builtin composition has no explicit generic arguments",
                    .source =
                        "import std::async using when_all;\n\nconst async fn leaf() -> i32 => 1; const value = await when_all<i32>(leaf());",
                    .rejection = DiagnosticCode::TypeGenericArguments
                },

                ContractCase {
                    .name = "named cold owner is explicitly transferred",
                    .source = R"(import std::async using when_all;

async fn leaf() -> i32 => 1;
async fn run() { let cold = leaf(); let (value,) = await when_all(&&cold); }
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "unmarked named operation cannot be adopted",
                    .source = R"(import std::async using when_all;

async fn leaf() {}
async fn run() { let cold = leaf(); await when_all(cold); }
)",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "active lexical child is not a cold input",
                    .source = R"(import std::async using when_all;

async fn leaf() {}
async fn run() { async let child = leaf(); await when_all(&&child); }
)",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "duplicate transfers are rejected by ordinary affine flow",
                    .source = R"(import std::async using when_all;

async fn leaf() {}
async fn run() { let cold = leaf(); await when_all(&&cold, &&cold); }
)",
                    .rejection = DiagnosticCode::AccessUnavailable
                },
                ContractCase {
                    .name = "composite owns retained loans until observation",
                    .source = R"(import std::async using when_all;

private import(cpp) fn make_context() -> ::Context;
private import(cpp) async fn provider(context: ::Context) -> i32;
async fn run() {
    var context = make_context(); let cold = provider(context); let group = when_all(&&cold);
    let (value,) = await group; let moved = &&context;
}
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "transfer to composite does not release native backing",
                    .source = R"(import std::async using when_all;

private import(cpp) fn make_context() -> ::Context;
private import(cpp) async fn provider(context: ::Context) -> i32;
async fn run() {
    var context = make_context(); let cold = provider(context); let group = when_all(&&cold);
    let moved = &&context; await group;
}
)",
                    .rejection = DiagnosticCode::AccessBorrowConflict
                },
                ContractCase {
                    .name = "construction and deferred failures remain separate",
                    .source = R"(import std::async using when_all;

struct Failure {}
async fn leaf() -> i32 throw Failure { throw Failure {}; }
fn prepare() { let group = when_all(leaf()); }
async fn run() -> (i32,) throw Failure => await when_all(leaf())?;
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "constructor argument failures still need propagation",
                    .source = R"(import std::async using when_all;

struct Failure {}
fn number() -> i32 throw Failure { throw Failure {}; }
async fn leaf(value: i32) -> i32 => value;
async fn run() { await when_all(leaf(number())); }
)",
                    .rejection = DiagnosticCode::EffectUnmarked
                },
                ContractCase {
                    .name = "race returns source position and common result",
                    .source = R"(import std::async using when_any;
import std::async using first_successful;

async fn leaf() -> i32 => 1;
async fn run() { let (index, value) = await when_any(leaf(), leaf()); let position: usize = index; let number: i32 = value; let (winner, result) = await first_successful(leaf()); }
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "race rejects unlike successes",
                    .source = R"(import std::async using when_any;

async fn number() -> i32 => 1;
async fn boolean() -> bool => true;
async fn run() { await when_any(number(), boolean()); }
)",
                    .rejection = DiagnosticCode::TypeMismatch
                },
                ContractCase {
                    .name = "race requires an input",
                    .source = R"(import std::async using when_any;

async fn run() { await when_any(); }
)",
                    .rejection = DiagnosticCode::TypeCallArity
                },

                ContractCase {
                    .name = "composition builtin requires a direct call",
                    .source = R"(import std::async using when_all;

fn run() { let callback = when_all; }
)",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
            };
            each(cases, &ContractCase::name, check_contract);
        };

    "Async timeout: builtin admission uses standard nominal types and cold ownership"_test =
        [] static noexcept {
            constexpr auto cases = std::array {
                ContractCase {
                    .name = "owned cold input retains success and nominal failures",
                    .source = R"(import std::time using timeout;

struct Failure {}
async fn leaf() -> i32 throw Failure { throw Failure {}; }
async fn run() -> i32 throw Failure + Timeout + RangeError {
    let pending = leaf();
    return await timeout(&&pending, Duration { nanoseconds: 1 })?;
}
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "construction does not execute deferred failures",
                    .source = R"(import std::time using timeout;

async fn leaf() -> i32 => 7;
fn run() { let pending = timeout(leaf(), Duration { nanoseconds: 1 }); }
)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "zero arguments reject before operand admission",
                    .source =
                        "import std::time using timeout;\n\nfn run() { let pending = timeout(); }",
                    .rejection = DiagnosticCode::TypeCallArity
                },
                ContractCase {
                    .name = "duration is required",
                    .source =
                        "import std::time using timeout;\n\nasync fn leaf() {} fn run() { let pending = timeout(leaf()); }",
                    .rejection = DiagnosticCode::TypeCallArity
                },
                ContractCase {
                    .name = "extra arguments are rejected",
                    .source =
                        "import std::time using timeout;\n\nasync fn leaf() {} fn run() { let pending = timeout(leaf(), Duration {}, 1); }",
                    .rejection = DiagnosticCode::TypeCallArity
                },
                ContractCase {
                    .name = "ordinary values cannot replace a cold operation",
                    .source =
                        "import std::time using timeout;\n\nfn run() { let pending = timeout(7, Duration {}); }",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "matching fields do not replace the standard Duration identity",
                    .source = R"(import std::time using timeout;

struct Interval { nanoseconds: i64 }
async fn leaf() {}
fn run() { let pending = timeout(leaf(), Interval { nanoseconds: 1 }); }
)",
                    .rejection = DiagnosticCode::TypeMismatch
                },
                ContractCase {
                    .name = "named operation requires explicit ownership transfer",
                    .source = R"(import std::time using timeout;

async fn leaf() {}
fn run() { let pending = leaf(); let wrapped = timeout(pending, Duration {}); }
)",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "Read operation parameter cannot supply an owner",
                    .source = R"(import std::async using Operation;
import std::time using timeout;

fn run(pending: Operation<void>) { let wrapped = timeout(&&pending, Duration {}); }
)",
                    .rejection = DiagnosticCode::AccessTakeOperand
                },
                ContractCase {
                    .name = "active child cannot be consumed as a cold operation",
                    .source = R"(import std::time using timeout;

async fn leaf() {}
async fn run() throw Timeout + RangeError {
    async let child = leaf();
    await timeout(&&child, Duration {})?;
}
)",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "timeout retains the protected operation backing",
                    .source = R"(import std::time using timeout;

async fn leaf(&value: i32) -> i32 => value;
async fn run() throw Timeout + RangeError {
    var value = 1;
    let wrapped = timeout(leaf(&value), Duration {});
    let moved = &&value;
    await wrapped?;
}
)",
                    .rejection = DiagnosticCode::AccessBorrowConflict
                },
                ContractCase {
                    .name = "nonpositive static deadlines do not start protected work",
                    .source = R"(import std::time using timeout;

const async fn leaf(&started: i32) -> i32 { ++started; return 7; }
const {
    var started = 0;
    for ticks in [0i64, -1i64] {
        let expired = try {
            let _ = await timeout(leaf(&started), Duration { nanoseconds: ticks })?;
            false
        } catch {
            Timeout(_) => true,
            RangeError(_) => false,
        };
        assert(expired && started == 0);
    }
}
)",
                    .rejection = std::nullopt
                },
            };
            each(cases, &ContractCase::name, check_timeout_contract);
        };

    "Async class operations: associated functions retain ordinary explicit inputs"_test =
        [] static noexcept {
            constexpr auto cases = std::array {
                ContractCase {
                    .name = "associated async body accesses its lexical class representation",
                    .source = R"(import std::async using yield_once;

class Value {
    value: i32,
    fn new(value: i32) -> Value => { value: value };
    async fn read(value: Value) -> i32 { await yield_once(); return value.value; }
}
async fn run() -> i32 => await Value::read(Value::new(3));)",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "associated async function constructs an independent class result",
                    .source = R"(class Value {
    value: i32,
    async fn new(value: i32) -> Value => { value: value };
    fn read(self) -> i32 => self.value;
}
async fn run() -> i32 { let value = await Value::new(3); return value.read(); })",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "associated async inputs keep existing borrow exclusions",
                    .source = "class Value { async fn read(value: str) {} }",
                    .rejection = DiagnosticCode::AsyncOwnership
                },
                ContractCase {
                    .name = "associated access does not grant module peers field authority",
                    .source = R"(class Value {
    value: i32,
    fn new() -> Value => { value: 3 };
    async fn read(value: Value) -> i32 => value.value;
}
async fn run() -> i32 { let value = Value::new(); return value.value; })",
                    .rejection = DiagnosticCode::AccessClassPrivate
                },
                ContractCase {
                    .name = "Read async receiver remains excluded",
                    .source = "class Value { async fn read(self) {} }",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "Write async receiver remains excluded",
                    .source = "class Value { async fn change(&self) {} }",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
                ContractCase {
                    .name = "Take async receiver remains excluded",
                    .source = "class Value { async fn consume(&&self) {} }",
                    .rejection = DiagnosticCode::AsyncAdmission
                },
            };
            each(cases, &ContractCase::name, check_contract);
        };

    "Async cancellation: cancel requires an admitted lexical child"_test = [] static noexcept {
        constexpr auto cases = std::array {
            ContractCase {
                .name = "cold operation is not a lexical child",
                .source = "async fn leaf() {} async fn run() { "
                          "let operation = leaf(); cancel operation; }",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "ordinary synchronous body has no async cancellation context",
                .source = "fn run() { let child = 1; cancel child; }",
                .rejection = DiagnosticCode::AsyncAdmission
            },
            ContractCase {
                .name = "ordinary value is not a lexical child",
                .source = "async fn run() { let value = 1; cancel value; }",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "missing child retains ordinary name diagnostics",
                .source = "async fn run() { cancel missing; }",
                .rejection = DiagnosticCode::NameUnresolved
            },
        };
        each(cases, &ContractCase::name, check_contract);
    };

    "Async terminal waiting: direct child admission and affine availability"_test = [] static noexcept {
        constexpr auto cases = std::array {
            ContractCase {
                .name = "heterogeneous children produce ordinary selection facts",
                .source = R"(import std::async using wait_ready;
import std::async using close;

async fn first() -> i32 => 1;
async fn second() -> bool => true;
async fn run() {
    async let a = first(); async let b = second();
    let (position, cancelled): (usize, bool) = await wait_ready(a, b);
    print(position, cancelled); await close(a); await close(b);
})",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "closing merges consumed and live branches and is idempotent",
                .source = R"(import std::async using close;

async fn leaf() -> i32 => 1;
async fn run(selected: bool) {
    async let child = leaf();
    if selected { print(await child); }
    await close(child); await close(child);
})",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "closing discards nominal child failures",
                .source = R"(import std::async using close;

struct Error {} async fn leaf() throw Error { throw Error {}; }
async fn run() { async let child = leaf(); await close(child); })",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "wait alone does not satisfy normal exit intent",
                .source =
                    "import std::async using wait_ready;\n\nasync fn leaf() {} async fn run() { "
                    "async let child = leaf(); print(await wait_ready(child)); }",
                .rejection = DiagnosticCode::AsyncChildIntent
            },
            ContractCase {
                .name = "wait requires at least one candidate",
                .source =
                    "import std::async using wait_ready;\n\nasync fn run() { print(await wait_ready()); }",
                .rejection = DiagnosticCode::TypeCallArity
            },
            ContractCase {
                .name = "close requires exactly one candidate",
                .source = "import std::async using close;\n\nasync fn leaf() {} async fn run() { "
                          "async let child = leaf(); await close(child, child); }",
                .rejection = DiagnosticCode::TypeCallArity
            },
            ContractCase {
                .name = "wait rejects duplicate identity",
                .source =
                    "import std::async using wait_ready;\n\nasync fn leaf() {} async fn run() { "
                    "async let child = leaf(); print(await wait_ready(child, child)); }",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "cold owner is not a wait candidate",
                .source =
                    "import std::async using wait_ready;\n\nasync fn leaf() {} async fn run() { "
                    "let operation = leaf(); print(await wait_ready(operation)); }",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "close rejects cold owners",
                .source = "import std::async using close;\n\nasync fn leaf() {} async fn run() { "
                          "let operation = leaf(); await close(operation); }",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "wait does not evaluate arbitrary candidate expressions",
                .source =
                    "import std::async using wait_ready;\n\nasync fn leaf() {} async fn run() { "
                    "print(await wait_ready(leaf())); }",
                .rejection = DiagnosticCode::AsyncOwnership
            },
            ContractCase {
                .name = "missing candidate preserves ordinary name diagnostics",
                .source =
                    "import std::async using close;\n\nasync fn run() { await close(missing); }",
                .rejection = DiagnosticCode::NameUnresolved
            },
            ContractCase {
                .name = "bare wait call cannot create a stored observer",
                .source =
                    "import std::async using wait_ready;\n\nasync fn leaf() {} async fn run() { "
                    "async let child = leaf(); let probe = wait_ready(child); }",
                .rejection = DiagnosticCode::AsyncAdmission
            },
            ContractCase {
                .name = "bare close call cannot create a stored observer",
                .source = "import std::async using close;\n\nasync fn leaf() {} async fn run() { "
                          "async let child = leaf(); let probe = close(child); }",
                .rejection = DiagnosticCode::AsyncAdmission
            },
            ContractCase {
                .name = "wait is not an ordinary callable value",
                .source =
                    "import std::async using wait_ready;\n\nfn run() { let observer = wait_ready; }",
                .rejection = DiagnosticCode::AsyncAdmission
            },
            ContractCase {
                .name = "synchronous await retains async admission",
                .source = "import std::async using wait_ready;\n\nfn run() { await wait_ready(); }",
                .rejection = DiagnosticCode::AsyncAdmission
            },
            ContractCase {
                .name = "already observed candidate is unavailable to wait",
                .source =
                    "import std::async using wait_ready;\n\nasync fn leaf() {} async fn run() { "
                    "async let child = leaf(); await child; "
                    "print(await wait_ready(child)); }",
                .rejection = DiagnosticCode::AccessUnavailable
            },
            ContractCase {
                .name = "potentially observed candidate is unavailable to wait",
                .source =
                    "import std::async using wait_ready;\nimport std::async using close;\n\nasync fn leaf() {} async fn run(selected: bool) { "
                    "async let child = leaf(); if selected { await child; } "
                    "print(await wait_ready(child)); await close(child); }",
                .rejection = DiagnosticCode::AccessUnavailable
            },
            ContractCase {
                .name = "closed child cannot be awaited",
                .source = "import std::async using close;\n\nasync fn leaf() {} async fn run() { "
                          "async let child = leaf(); await close(child); await child; }",
                .rejection = DiagnosticCode::AccessUnavailable
            },
            ContractCase {
                .name = "outer consumed candidate is rejected on a loop backedge",
                .source = R"(import std::async using wait_ready;

async fn leaf() {} async fn run() {
    async let child = leaf();
    for var index: i32 = 0; index < 2; ++index {
        print(await wait_ready(child)); await child;
    }
})",
                .rejection = DiagnosticCode::AccessUnavailable
            },
            ContractCase {
                .name = "ordinary local callable keeps the wait name",
                .source = R"(import std::async using yield_once;
import std::async using wait_ready;

private fn ordinary(value: i32) => yield_once();
async fn run() { let wait_ready = ordinary; await wait_ready(3); })",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "static specialization preserves lexical observer identities",
                .source =
                    R"(import std::async using wait_ready;
import std::async using close;

async fn leaf() {} async fn selected(const observe: bool) -> (usize, bool) {
    async let child = leaf();
    const if observe {
        let result = await wait_ready(child); await close(child); return result;
    } else { await close(child); return (0usize, false); }
}
async fn run() { print(await selected(true), await selected(false)); })",
                .rejection = std::nullopt
            },
            ContractCase {
                .name = "ordinary module declarations keep observer names",
                .source = R"(async fn wait_ready(value: i32) -> i32 => value;
async fn close(value: i32) -> i32 => value;
async fn run() { print(await wait_ready(3), await close(4)); })",
                .rejection = std::nullopt
            },
        };
        each(cases, &ContractCase::name, check_contract);
    };

    "Async terminal closing: borrowed backing is released only by consumption"_test =
        [] static noexcept {
            constexpr auto cases = std::array {
                ContractCase {
                    .name = "wait leaves the scalar Write loan live",
                    .source = R"(import std::async using wait_ready;
import std::async using close;

async fn leaf(&value: i32) -> i32 => value;
async fn run() { var value = 1; async let child = leaf(&value);
    print(await wait_ready(child)); let moved = &&value; await close(child);
})",
                    .rejection = DiagnosticCode::AccessBorrowConflict
                },
                ContractCase {
                    .name = "close releases the scalar Write loan",
                    .source = R"(import std::async using close;

async fn leaf(&value: i32) -> i32 => value;
async fn run() { var value = 1; async let child = leaf(&value);
    await close(child); let moved = &&value; print(moved);
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "conditional await followed by close releases merged loans",
                    .source = R"(import std::async using close;

async fn leaf(&value: i32) -> i32 => value;
async fn run(selected: bool) { var value = 1; async let child = leaf(&value);
    if selected { print(await child); }
    await close(child); let moved = &&value; print(moved);
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "close preserves the borrowed backing of other children",
                    .source = R"(import std::async using close;

async fn leaf(&value: i32) -> i32 => value;
async fn run() { var value = 1;
    async let a = leaf(&value); async let b = leaf(&value);
    await close(a); let moved = &&value; await close(b);
})",
                    .rejection = DiagnosticCode::AccessBorrowConflict
                },
            };
            each(cases, &ContractCase::name, check_contract);
        };

    "Async templates: static arguments select residual cold bodies"_test = [] static noexcept {
        constexpr auto cases = std::array {
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
                .source =
                    "import std::async using yield_once;\n\nconst async fn leaf() { await yield_once(); } const { await leaf(); }",
                .rejection = std::nullopt
            },
        };
        each(cases, &ContractCase::name, check_contract);
    };

    "Async native contracts: signatures and cold source backing"_test = [] static noexcept {
        constexpr auto cases = std::array {
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
                .source = R"(
private import(cpp) fn make_context() -> ::Context;
private import(cpp) async fn provider(context: ::Context) -> i32;
async fn run() {
    var context = make_context();
    if true { async let child = provider(context); cancel child; }
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
            constexpr auto cases = std::array {
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
        constexpr auto cases = std::array {
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
        constexpr auto cases = std::array {
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
            constexpr auto cases = std::array {
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
            constexpr auto cases = std::array {
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
                    .source = R"(
async fn leaf() -> i32 => 1;
async fn run(flag: bool) { async let child = leaf(); if flag { cancel child; } })",
                    .rejection = DiagnosticCode::AsyncChildIntent
                },
                ContractCase {
                    .name = "observation or request covers every normal path",
                    .source = R"(
async fn leaf() -> i32 => 1;
async fn run(flag: bool) { async let child = leaf();
    if flag { cancel child; } else { print(await child); }
})",
                    .rejection = std::nullopt
                },
                ContractCase {
                    .name = "request does not release backing before return operand",
                    .source = R"(
async fn leaf(&value: i32) -> i32 => value;
async fn run() -> i32 {
    var value = 1; async let child = leaf(&value); cancel child; return &&value;
})",
                    .rejection = DiagnosticCode::AccessBorrowConflict
                },
                ContractCase {
                    .name = "scalar return observation precedes required closure",
                    .source = R"(
async fn leaf(&value: i32) -> i32 => value;
async fn run() -> i32 {
    var value = 1; async let child = leaf(&value); cancel child; return value;
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
