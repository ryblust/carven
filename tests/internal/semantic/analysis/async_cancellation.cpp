module carven:test.internal.semantic.analysis.async_cancellation;

import :frontend.program.parse;
import :semantic.evaluation.execution;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.evaluation;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

auto cancellation_program(std::string_view text) noexcept -> SemIRProgram {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("cancellation.cv", std::string(text));
    require(source.has_value());
    const auto source_view = sources.view(*source);
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *source,
            .module_path = *CanonicalModulePath::from_value("cancellation")
        },
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    require(syntax.has_value()).note("source = ", source_view.text);
    auto analyzed = analyze_test_syntax(std::move(*syntax), HostEffects::Deny);
    require(analyzed.has_value()).note("source = ", source_view.text);
    return std::move(analyzed->value);
}

auto cancellation_callable(const SemIRProgram& program, std::string_view name) noexcept
    -> CallableID {
    auto found = std::optional<CallableID>();
    for (const auto entry : program.declarations().functions()) {
        if (program.provenance().spelling(entry.value.name) == name) {
            found = entry.value.callable;
        }
    }
    require(found.has_value()).note("function = ", name);
    return *found;
}

auto check_completion(
    const SemIRProgram& program,
    std::string_view name,
    bool cancelled,
    std::span<const bool> observations
) noexcept -> void {
    const auto callable = cancellation_callable(program, name);
    expect_equal(program.may_complete_cancelled(callable), cancelled).note("function = ", name);
    const auto body_id = program.declarations().body_for_callable(callable);
    require(body_id.has_value());
    const auto& body = program.bodies().body(*body_id);
    auto actual = std::vector<bool>();
    visit_semantic_nodes(body.region(), [&](const SemanticExpression& expression) noexcept {
        if (const auto* awaited = std::get_if<SemAwait>(&expression.value)) {
            actual.push_back(program.await_completion_may_be_cancelled(*body_id, *awaited));
        }
    });
    if (!expect_equal(actual.size(), observations.size()).note("function = ", name)) {
        return;
    }
    for (auto index = 0uz; index < observations.size(); ++index) {
        expect_equal(static_cast<bool>(actual[index]), observations[index])
            .note("function = ", name, "observation = ", index);
    }
}

struct CancellationCase final {
    std::string_view name;
    std::string_view source;
    bool cancelled;
};

auto check_single_observation(const CancellationCase& input) noexcept -> void {
    const auto program = cancellation_program(input.source);
    const auto observations = std::array {input.cancelled};
    check_completion(program, "probe", input.cancelled, observations);
    for (const auto entry : program.declarations().functions()) {
        const auto& signature = program.callable_signatures().signature(
            program.declarations().callable(entry.value.callable).signature
        );
        if (signature.execution == CallableExecutionKind::Synchronous) {
            expect_equal(program.may_complete_cancelled(entry.value.callable), false)
                .note("function = ", program.provenance().spelling(entry.value.name));
        }
    }
}

const TestSuite suite([] static noexcept {
    "Async cancellation: cancellation is independent of nominal failures"_test =
        [] static noexcept {
            const auto program = cancellation_program(R"(import std::async using cancel_point;
import std::async using yield_once;

struct Failure {}
async fn ready() {}
async fn yielding() { await yield_once(); }
async fn failure_only() throw Failure { throw Failure {}; }
async fn checked() { await cancel_point(); }
async fn failure_checked() throw Failure { await cancel_point(); throw Failure {}; }
async fn recover_failure() { try { await failure_only()?; } catch { Failure(_) => {}, } }
async fn recover_checked() { try { await failure_checked()?; } catch { Failure(_) => {}, } }
)");
            check_completion(program, "ready", false, {});
            check_completion(program, "yielding", false, std::array {false});
            check_completion(program, "failure_only", false, {});
            check_completion(program, "checked", true, std::array {true});
            check_completion(program, "recover_failure", false, std::array {false});
            check_completion(program, "recover_checked", true, std::array {true});
            for (const auto name :
                 {std::string_view("checked"), std::string_view("failure_only")}) {
                const auto callable = cancellation_callable(program, name);
                const auto& signature = program.callable_signatures().signature(
                    program.declarations().callable(callable).signature
                );
                expect_equal(
                    program.failure_sets().failure_set(signature.failures).members.empty(),
                    name == "checked"
                );
            }
        };

    "Async cancellation: terminal facts and draining do not forward child effects"_test =
        [] static noexcept {
            const auto program = cancellation_program(R"(import std::async using cancel_point;
import std::async using wait_ready;
import std::async using close;

struct Error {}
async fn checked() throw Error { await cancel_point(); throw Error {}; }
async fn waiting() -> (usize, bool) {
    async let child = checked(); let selected = await wait_ready(child);
    await close(child); return selected;
}
async fn closing() { async let child = checked(); await close(child); }
async fn parent_checked() {
    async let child = checked(); print(await wait_ready(child));
    await cancel_point(); await close(child);
}
)");
            check_completion(program, "waiting", false, {});
            check_completion(program, "closing", false, {});
            check_completion(program, "parent_checked", true, std::array {true});
            const auto callable = cancellation_callable(program, "waiting");
            const auto body_id = program.declarations().body_for_callable(callable);
            require(body_id.has_value());
            const auto& body = program.bodies().body(*body_id);
            auto waits = 0uz;
            auto closes = 0uz;
            visit_semantic_nodes(body.region(), [&](const SemanticExpression& expression) noexcept {
                if (std::holds_alternative<SemWaitReady>(expression.value)) {
                    ++waits;
                    const auto* tuple = std::get_if<TupleTypeValue>(
                        &program.types().type(expression.type.resolved()).value
                    );
                    require(tuple != nullptr);
                    require(tuple->elements.size() == 2uz);
                    expect(
                        program.types().type(tuple->elements[0]).value
                        == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Usize}}
                    );
                    expect(
                        program.types().type(tuple->elements[1]).value
                        == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Bool}}
                    );
                } else if (std::holds_alternative<SemCloseChild>(expression.value)) {
                    ++closes;
                } else {
                    return;
                }
                expect_equal(
                    evaluation_rule(program, expression).action,
                    EvaluationAction::Required
                );
                expect(program.failure_sets()
                           .failure_set(expression.failures.resolved())
                           .members.empty());
            });
            expect_equal(waits, 1uz);
            expect_equal(closes, 1uz);
        };

    "Async cancellation: storage and factory returns preserve the observed producer"_test =
        [] static noexcept {
            constexpr auto cases = std::array {
                CancellationCase {
                    .name = "stored yield transfers without accepting cancellation",
                    .source = R"(import std::async using yield_once;

async fn probe() {
    let original = yield_once();
    let moved = &&original;
    await moved;
})",
                    .cancelled = false
                },
                CancellationCase {
                    .name = "stored checkpoint retains its acceptance after transfer",
                    .source = R"(import std::async using cancel_point;

async fn probe() {
    let original = cancel_point();
    let moved = &&original;
    await moved;
})",
                    .cancelled = true
                },
                CancellationCase {
                    .name = "factory returns the transferred yield, not its discarded checkpoint",
                    .source = R"(import std::async using cancel_point;
import std::async using yield_once;

fn factory() {
    let discarded = cancel_point();
    let original = yield_once();
    let moved = &&original;
    return &&moved;
}
async fn probe() { await factory(); })",
                    .cancelled = false
                },
                CancellationCase {
                    .name = "factory returns a transferred checkpoint",
                    .source = R"(import std::async using cancel_point;

fn factory() {
    let original = cancel_point();
    let moved = &&original;
    return &&moved;
}
async fn probe() { let stored = factory(); await stored; })",
                    .cancelled = true
                },
            };
            each(cases, &CancellationCase::name, check_single_observation);
        };

    "Async cancellation: successful structured values join producer capabilities"_test =
        [] static noexcept {
            constexpr auto cases = std::array {
                CancellationCase {
                    .name = "if selects only nonaccepting producers",
                    .source = R"(import std::async using yield_once;

fn factory(flag: bool) {
    return if flag { yield_once() } else { yield_once() };
}
async fn probe(flag: bool) { await factory(flag); })",
                    .cancelled = false
                },
                CancellationCase {
                    .name = "if can select a checkpoint",
                    .source = R"(import std::async using cancel_point;
import std::async using yield_once;

fn factory(flag: bool) {
    return if flag { yield_once() } else { cancel_point() };
}
async fn probe(flag: bool) { await factory(flag); })",
                    .cancelled = true
                },
                CancellationCase {
                    .name = "match selects only nonaccepting producers",
                    .source = R"(import std::async using yield_once;

fn factory(value: i32) {
    return match value { 0 => yield_once(), _ => yield_once(), };
}
async fn probe(value: i32) { await factory(value); })",
                    .cancelled = false
                },
                CancellationCase {
                    .name = "match can select a checkpoint",
                    .source = R"(import std::async using cancel_point;
import std::async using yield_once;

fn factory(value: i32) {
    return match value { 0 => yield_once(), _ => cancel_point(), };
}
async fn probe(value: i32) { await factory(value); })",
                    .cancelled = true
                },
                CancellationCase {
                    .name = "try and catch return nonaccepting producers",
                    .source = R"(import std::async using yield_once;

struct Failure {}
fn factory(flag: bool) throw Failure {
    if flag { throw Failure {}; }
    return yield_once();
}
async fn probe(flag: bool) {
    let selected = try { factory(flag)? } catch { Failure(_) => yield_once(), };
    await selected;
})",
                    .cancelled = false
                },
                CancellationCase {
                    .name = "catch success can return a checkpoint",
                    .source = R"(import std::async using cancel_point;
import std::async using yield_once;

struct Failure {}
fn factory(flag: bool) throw Failure {
    if flag { throw Failure {}; }
    return yield_once();
}
async fn probe(flag: bool) {
    let selected = try { factory(flag)? } catch { Failure(_) => cancel_point(), };
    await selected;
})",
                    .cancelled = true
                },
            };
            each(cases, &CancellationCase::name, check_single_observation);
        };

    "Async cancellation: child closure and cold construction do not propagate acceptance"_test =
        [] static noexcept {
            const auto program = cancellation_program(R"(import std::async using cancel_requested;
import std::async using cancel_point;
import std::async using yield_once;

async fn cold_only() { let unused = cancel_point(); }
async fn query_only() -> bool => cancel_requested();
async fn discarded_then_yield() { let unused = cancel_point(); await yield_once(); }
async fn closed_child() {
    async let child = cancel_point();
    cancel child;
}
async fn observed_yield() { async let child = yield_once(); await child; }
async fn observed_checkpoint() { async let child = cancel_point(); await child; }
)");
            check_completion(program, "cold_only", false, {});
            check_completion(program, "query_only", false, {});
            check_completion(program, "discarded_then_yield", false, std::array {false});
            check_completion(program, "closed_child", false, {});
            check_completion(program, "observed_yield", false, std::array {false});
            check_completion(program, "observed_checkpoint", true, std::array {true});
        };

    "Async cancellation: recursive dependencies converge from accepting producers"_test =
        [] static noexcept {
            const auto program = cancellation_program(R"(import std::async using cancel_point;

async fn ready_recursive(depth: i32) -> i32 {
    if depth == 0 { return 1; }
    return await ready_recursive(depth - 1);
}
async fn checked_recursive(depth: i32) -> i32 {
    if depth == 0 { await cancel_point(); return 1; }
    return await checked_recursive(depth - 1);
}
async fn ready_first(depth: i32) -> i32 {
    if depth == 0 { return 1; }
    return await ready_second(depth - 1);
}
async fn ready_second(depth: i32) -> i32 {
    if depth == 0 { return 1; }
    return await ready_first(depth - 1);
}
async fn checked_first(depth: i32) -> i32 {
    if depth == 0 { return 1; }
    return await checked_second(depth - 1);
}
async fn checked_second(depth: i32) -> i32 {
    if depth == 0 { await cancel_point(); return 1; }
    return await checked_first(depth - 1);
}
)");
            check_completion(program, "ready_recursive", false, std::array {false});
            check_completion(program, "checked_recursive", true, std::array {true, true});
            check_completion(program, "ready_first", false, std::array {false});
            check_completion(program, "ready_second", false, std::array {false});
            check_completion(program, "checked_first", true, std::array {true});
            check_completion(program, "checked_second", true, std::array {true, true});
        };

    "Async cancellation: operand execution is separate from the outer observation"_test =
        [] static noexcept {
            const auto program = cancellation_program(R"(import std::async using cancel_point;

async fn checked() -> i32 { await cancel_point(); return 7; }
async fn ready(value: i32) -> i32 => value;
async fn probe() -> i32 => await ready(await checked());
)");
            // Traversal enters the outer await before visiting its argument's await.
            check_completion(program, "probe", true, std::array {false, true});
        };

    "Async cancellation: static factories publish only selected producer flows"_test =
        [] static noexcept {
            const auto program = cancellation_program(R"(import std::async using cancel_point;

async fn ready() -> i32 => 7;
async fn checked() -> i32 { await cancel_point(); return 7; }
fn factory(const accepting: bool) {
    const if accepting { return checked(); }
    return ready();
}
async fn ready_probe() -> i32 => await factory(false);
async fn checked_probe() -> i32 => await factory(true);
)");
            check_completion(program, "ready_probe", false, std::array {false});
            check_completion(program, "checked_probe", true, std::array {true});
        };
});

} // namespace
