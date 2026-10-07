module carven:test.internal.semantic.analysis.async_cancellation;

import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import std;

namespace {

auto cancellation_program(std::string_view text) noexcept -> SemIRProgram {
    auto sources = SourceManager();
    const auto source = sources.append_virtual(
        "cancellation.cv",
        "import std::async using {cancel, cancellation_point, cancellation_requested, yield_once};\n"
            + std::string(text)
    );
    const auto standard = sources.append_virtual("async.cv", "");
    require(source.has_value() && standard.has_value());
    const auto source_view = sources.view(*source);
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *source,
            .module_path = *CanonicalModulePath::from_value("cancellation")
        },
        SourceModuleInput {
            .source_id = *standard,
            .module_path = *CanonicalModulePath::from_value("crafts.carven.std.async")
        },
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    require(syntax.has_value()).note("source = ", source_view.text);
    auto analyzed = analyze(std::move(*syntax));
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
            const auto program = cancellation_program(R"(
struct Failure {}
async fn ready() {}
async fn yielding() { await yield_once(); }
async fn failure_only() throw Failure { throw Failure {}; }
async fn checked() { await cancellation_point(); }
async fn failure_checked() throw Failure { await cancellation_point(); throw Failure {}; }
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

    "Async cancellation: storage and factory returns preserve the observed producer"_test =
        [] static noexcept {
            const auto cases = std::array {
                CancellationCase {
                    .name = "stored yield transfers without accepting cancellation",
                    .source = R"(async fn probe() {
    let original = yield_once();
    let moved = &&original;
    await moved;
})",
                    .cancelled = false
                },
                CancellationCase {
                    .name = "stored checkpoint retains its acceptance after transfer",
                    .source = R"(async fn probe() {
    let original = cancellation_point();
    let moved = &&original;
    await moved;
})",
                    .cancelled = true
                },
                CancellationCase {
                    .name = "factory returns the transferred yield, not its discarded checkpoint",
                    .source = R"(fn factory() {
    let discarded = cancellation_point();
    let original = yield_once();
    let moved = &&original;
    return &&moved;
}
async fn probe() { await factory(); })",
                    .cancelled = false
                },
                CancellationCase {
                    .name = "factory returns a transferred checkpoint",
                    .source = R"(fn factory() {
    let original = cancellation_point();
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
            const auto cases = std::array {
                CancellationCase {
                    .name = "if selects only nonaccepting producers",
                    .source = R"(fn factory(flag: bool) {
    return if flag { yield_once() } else { yield_once() };
}
async fn probe(flag: bool) { await factory(flag); })",
                    .cancelled = false
                },
                CancellationCase {
                    .name = "if can select a checkpoint",
                    .source = R"(fn factory(flag: bool) {
    return if flag { yield_once() } else { cancellation_point() };
}
async fn probe(flag: bool) { await factory(flag); })",
                    .cancelled = true
                },
                CancellationCase {
                    .name = "match selects only nonaccepting producers",
                    .source = R"(fn factory(value: i32) {
    return match value { 0 => yield_once(), _ => yield_once(), };
}
async fn probe(value: i32) { await factory(value); })",
                    .cancelled = false
                },
                CancellationCase {
                    .name = "match can select a checkpoint",
                    .source = R"(fn factory(value: i32) {
    return match value { 0 => yield_once(), _ => cancellation_point(), };
}
async fn probe(value: i32) { await factory(value); })",
                    .cancelled = true
                },
                CancellationCase {
                    .name = "try and catch return nonaccepting producers",
                    .source = R"(struct Failure {}
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
                    .source = R"(struct Failure {}
fn factory(flag: bool) throw Failure {
    if flag { throw Failure {}; }
    return yield_once();
}
async fn probe(flag: bool) {
    let selected = try { factory(flag)? } catch { Failure(_) => cancellation_point(), };
    await selected;
})",
                    .cancelled = true
                },
            };
            each(cases, &CancellationCase::name, check_single_observation);
        };

    "Async cancellation: child closure and cold construction do not propagate acceptance"_test =
        [] static noexcept {
            const auto program = cancellation_program(R"(
async fn cold_only() { let unused = cancellation_point(); }
async fn query_only() -> bool => cancellation_requested();
async fn discarded_then_yield() { let unused = cancellation_point(); await yield_once(); }
async fn closed_child() {
    async let child = cancellation_point();
    cancel(child);
}
async fn observed_yield() { async let child = yield_once(); await child; }
async fn observed_checkpoint() { async let child = cancellation_point(); await child; }
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
            const auto program = cancellation_program(R"(
async fn ready_recursive(depth: i32) -> i32 {
    if depth == 0 { return 1; }
    return await ready_recursive(depth - 1);
}
async fn checked_recursive(depth: i32) -> i32 {
    if depth == 0 { await cancellation_point(); return 1; }
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
    if depth == 0 { await cancellation_point(); return 1; }
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
            const auto program = cancellation_program(R"(
async fn checked() -> i32 { await cancellation_point(); return 7; }
async fn ready(value: i32) -> i32 => value;
async fn probe() -> i32 => await ready(await checked());
)");
            // Traversal enters the outer await before visiting its argument's await.
            check_completion(program, "probe", true, std::array {false, true});
        };

    "Async cancellation: static factories publish only selected producer flows"_test =
        [] static noexcept {
            const auto program = cancellation_program(R"(
async fn ready() -> i32 => 7;
async fn checked() -> i32 { await cancellation_point(); return 7; }
fn factory(const accepting: bool) {
    const if accepting { return checked(); }
    return ready();
}
async fn ready_probe() -> i32 => await factory(false);
async fn checked_probe() -> i32 => await factory(true);
)");
            expect_equal(program.static_instances().size(), 2uz);
            check_completion(program, "ready_probe", false, std::array {false});
            check_completion(program, "checked_probe", true, std::array {true});
        };
});

} // namespace
