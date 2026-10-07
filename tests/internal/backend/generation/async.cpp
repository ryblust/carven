module carven:test.internal.backend.generation.async;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.provenance;
import :test.harness.framework;
import std;

namespace {

auto analyze_async_generation_source(std::string text) noexcept -> SemIRProgram {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("analysis.cv", std::move(text));
    const auto standard = sources.append_virtual("async.cv", "");
    require(source.has_value() && standard.has_value());
    const auto source_view = sources.view(*source);
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *source,
            .module_path = *CanonicalModulePath::from_value("analysis")
        },
        SourceModuleInput {
            .source_id = *standard,
            .module_path = *CanonicalModulePath::from_value("crafts.carven.std.async")
        },
    };
    auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
    require(parsed.has_value()).note("source = ", source_view.text);
    auto analyzed = analyze(std::move(*parsed));
    require(analyzed.has_value()).note("source = ", source_view.text);
    return std::move(analyzed->value);
}

struct AsyncFusionSummary final {
    std::size_t bodies = 0;
    std::size_t coroutines = 0;
    std::size_t operations = 0;
    std::size_t completion_storage = 0;
    std::size_t deferred_storage = 0;
    std::size_t mutable_storage = 0;
    std::size_t character_checks = 0;
    std::size_t cold_calls = 0;
    std::size_t observations = 0;
    std::size_t successors = 0;
    std::size_t dereferences = 0;
};

struct AsyncFusionQuery final {
    const TargetUnit& unit;
    AsyncFusionSummary& summary;
    const std::flat_set<std::string>& factories;
    bool measured = false;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool {
        const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
        const auto* definition =
            function ? std::get_if<TargetFreeFunctionDefinition>(&function->form) : nullptr;
        measured = definition && function->name.components().back().spelling() == "probe";
        if (measured) {
            ++summary.bodies;
            summary.coroutines += definition->execution == TargetCallableExecution::Coroutine;
        }
        return true;
    }

    auto leave_declaration(const TargetDecl&) noexcept -> bool {
        measured = false;
        return true;
    }

    auto operation_type(TargetTypeID id) const noexcept -> bool {
        const auto* type = std::get_if<TargetIntrinsicType>(&unit.type(id).value);
        return type && type->symbol == TargetSymbol::RuntimeAsyncOperation;
    }

    auto completion_storage(TargetTypeID id) const noexcept -> bool {
        const auto* type = std::get_if<TargetIntrinsicType>(&unit.type(id).value);
        if (type == nullptr) {
            return false;
        }
        return type->symbol == TargetSymbol::RuntimeAsyncCompletion
            || (type->symbol == TargetSymbol::RuntimeDeferredResult
                && std::ranges::any_of(
                    type->type_argument_ids,
                    [&](TargetTypeID argument) noexcept { return completion_storage(argument); }
                ));
    }

    auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool {
        if (measured) {
            summary.mutable_storage += variable.binding == TargetVariableBinding::MutableValue;
            summary.operations += operation_type(variable.type);
            summary.completion_storage += completion_storage(variable.type);
            const auto* type = std::get_if<TargetIntrinsicType>(&unit.type(variable.type).value);
            summary.deferred_storage +=
                type != nullptr && type->symbol == TargetSymbol::RuntimeDeferredResult;
        }
        return true;
    }

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
        if (!measured) {
            return true;
        }
        summary.observations += std::holds_alternative<TargetCoAwaitExpr>(expression.value);
        if (const auto* prefix = std::get_if<TargetPrefixExpr>(&expression.value)) {
            summary.dereferences += prefix->op == TargetPrefixOperator::Dereference;
        }
        if (const auto* constructed = std::get_if<TargetConstructionExpr>(&expression.value)) {
            summary.operations += operation_type(constructed->type);
        }
        if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
            if (const auto* intrinsic =
                    std::get_if<TargetIntrinsicNameExpr>(&call->callee->value)) {
                summary.character_checks +=
                    intrinsic->symbol == TargetSymbol::RuntimeCheckedUnicodeScalar;
            }
            if (const auto* name = std::get_if<TargetNameExpr>(&call->callee->value)) {
                const auto source_name = name->name.components().back().spelling();
                summary.cold_calls += source_name == "leaf"
                    || source_name == "other"
                    || factories.contains(std::string(source_name));
                summary.successors += source_name == "after";
            }
        }
        return true;
    }
};

const TestSuite suite([] static noexcept {
    "Generation async: source control and result delivery produce sealed native bodies"_test =
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view source;
            };
            const auto cases = std::array {
                Case {
                    .name = "cold leaf and wrapper",
                    .source = "async fn leaf() -> i32 => 7; async fn empty() {} "
                              "fn cold() => leaf(); async fn main() { let value = "
                              "await cold(); print(value); }"
                },
                Case {
                    .name = "child construction after awaited argument",
                    .source = "async fn leaf() -> i32 => 7; "
                              "async fn identity(value: i32) -> i32 => value; "
                              "async fn main() { async let child = identity(await leaf()); "
                              "print(await child); }"
                },
                Case {
                    .name = "structured return with child",
                    .source =
                        "async fn leaf() -> i32 => 7; async fn probe(flag: bool) -> i32 { "
                        "return if flag { async let child = leaf(); await child } "
                        "else { await leaf() }; } async fn main() { print(await probe(true)); }"
                },
                Case {
                    .name = "break leaves suspending step unentered",
                    .source = "async fn leaf() -> i32 => 7; async fn main() { "
                              "for var index: i32 = 0; index < 2; index += await leaf() { "
                              "break; } print(await leaf()); }"
                },
                Case {
                    .name = "return leaves suspending step unentered",
                    .source = "async fn leaf() -> i32 => 7; async fn probe() -> i32 { "
                              "for var index: i32 = 0; index < 2; index += await leaf() { "
                              "return 2; } return 3; } async fn main() { print(await probe()); }"
                },
                Case {
                    .name = "suspending step after continue",
                    .source = "async fn leaf() -> i32 => 7; async fn main() { "
                              "for var index: i32 = 0; index < 2; index += await leaf() { "
                              "continue; } }"
                },
                Case {
                    .name = "loop header child survives step and closes on break",
                    .source = "async fn leaf() -> i32 => 7; async fn main() { "
                              "for async let child = leaf(); true; { print(await child); break; } }"
                },
                Case {
                    .name = "implicit child close keeps an initializer in coroutine context",
                    .source =
                        "import std::async using {cancel}; async fn leaf() -> i32 => 7; "
                        "async fn probe(flag: bool) -> i32 { let value = if flag { "
                        "async let child = leaf(); cancel(child); 9 } else { 3 }; return value; } "
                        "async fn main() { print(await probe(true)); }"
                },
                Case {
                    .name = "observed lexical child",
                    .source = "async fn leaf() -> i32 => 7; async fn probe() -> i32 { "
                              "async let child = leaf(); return await child; } async fn "
                              "main() { let value = await probe(); print(value); }"
                },
            };
            each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_async_generation_source(std::string(input.source)),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("async_generation")}
                );
                expect_greater(compilation.target().artifact_count(), 0uz);
                for (const auto artifact : compilation.target().artifacts()) {
                    // Lowering seals native scopes, exits, and coroutine contexts.
                    static_cast<void>(lower_artifact(compilation, artifact.id));
                }
            });
        };

    "Generation async: failure handlers retain only necessary relay storage"_test =
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view source;
                std::size_t relay_storage;
            };
            const auto cases = std::array {
                Case {
                    .name = "synchronous failure with suspending handler",
                    .source = "import std::async using yield_once; struct Failure { value: i32 } "
                              "fn fail() -> i32 throw Failure { throw Failure { value: 7 }; } "
                              "async fn probe() -> i32 { try { return fail()?; } catch { "
                              "Failure(error) => { await yield_once(); return error.value; }, } }",
                    .relay_storage = 0uz
                },
                Case {
                    .name = "protected children close before a suspending handler",
                    .source =
                        "import std::async using yield_once; struct Failure { value: i32 } "
                        "async fn child(&finished: i32) { await yield_once(); finished = 1; } "
                        "async fn probe() -> i32 { var finished: i32 = 0; try { "
                        "async let pending = child(&finished); throw Failure { value: 7 }; "
                        "} catch { Failure(error) => { await yield_once(); "
                        "return finished + error.value; }, } }",
                    .relay_storage = 1uz
                },
            };
            each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_async_generation_source(std::string(input.source)),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("async_handler")}
                );
                struct Query final {
                    const TargetUnit& unit;
                    std::size_t& relay_storage;
                    std::size_t& bodies;
                    bool measured;

                    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool {
                        const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
                        measured = function
                            && std::holds_alternative<TargetFreeFunctionDefinition>(function->form)
                            && function->name.components().back().spelling() == "probe";
                        bodies += measured;
                        return true;
                    }

                    auto leave_declaration(const TargetDecl&) noexcept -> bool {
                        measured = false;
                        return true;
                    }

                    auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool {
                        if (measured) {
                            const auto* type =
                                std::get_if<TargetIntrinsicType>(&unit.type(variable.type).value);
                            relay_storage +=
                                type != nullptr && type->symbol == TargetSymbol::StdOptional;
                        }
                        return true;
                    }
                };
                auto relay_storage = 0uz;
                auto bodies = 0uz;
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    auto query = Query {
                        .unit = unit,
                        .relay_storage = relay_storage,
                        .bodies = bodies,
                        .measured = false,
                    };
                    if (!expect(traverse_target_unit(unit.sections(), query))) {
                        return;
                    }
                }
                expect_equal(bodies, 1uz);
                expect_equal(relay_storage, input.relay_storage);
            });
        };

    "Generation async: fresh scalar dependencies execute in the caller coroutine"_test =
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view source;
                std::size_t observations;
                bool completes = true;
            };
            const auto cases = std::array {
                Case {
                    .name = "independent returns and binding identities",
                    .source = "async fn leaf(value: i32) -> i32 { let selected = value + 1; "
                              "if value < 0 { return selected; } return selected + 10; } "
                              "async fn other(value: i32) -> i32 { let selected = value * 2; "
                              "return selected; } async fn probe() -> i32 { "
                              "var selected: i32 = 40; selected += await leaf(-2); "
                              "selected += await leaf(2); selected += await other(3); "
                              "return selected; }",
                    .observations = 0uz
                },
                Case {
                    .name = "arguments finish before the fresh body starts",
                    .source =
                        "fn argument(&trace: i32, &backing: i32) -> i32 { "
                        "trace += 1; backing = 9; return 5; } "
                        "async fn leaf(snapshot: i32, extra: i32) -> i32 => snapshot + extra; "
                        "async fn probe() -> i32 { var trace: i32 = 0; "
                        "var backing: i32 = 3; let result = await leaf(backing, "
                        "argument(&trace, &backing)); return result + trace; }",
                    .observations = 0uz
                },
                Case {
                    .name = "caught failure keeps a callee exit inside the caller",
                    .source = "struct Failure { value: i32 } "
                              "async fn fail() -> i32 throw Failure { "
                              "throw Failure { value: 7 }; } "
                              "async fn leaf() -> i32 { try { return await fail()?; } "
                              "catch { Failure(error) => { return error.value; }, } } "
                              "async fn probe() -> i32 => await leaf();",
                    .observations = 1uz
                },
                Case {
                    .name = "noncompleting dependency leaves its consumer unreachable",
                    .source = "async fn leaf() { while {} } fn after() {} "
                              "async fn probe() { await leaf(); after(); }",
                    .observations = 0uz,
                    .completes = false
                },
                Case {
                    .name = "factory maps repeated reordered formals and a constant",
                    .source = "async fn leaf(a: i32, b: i32, c: i32, extra: i32) -> i32 "
                              "=> a * 100 + b * 10 + c + extra; "
                              "fn factory(first: i32, unused: i32, last: i32) { "
                              "return leaf(last, first, first, 7); } "
                              "async fn probe() -> i32 => await factory(1, 2, 3);",
                    .observations = 0uz
                },
                Case {
                    .name = "static factory selects the ready dependency",
                    .source = "import std::async using yield_once; "
                              "async fn leaf(value: i32) -> i32 => value + 1; "
                              "async fn other(value: i32) -> i32 { await yield_once(); "
                              "return value + 1; } "
                              "private fn factory(value: i32, const deferred: bool) { "
                              "const if deferred { return other(value); } "
                              "else { return leaf(value); } } "
                              "async fn probe() -> i32 => await factory(7, false);",
                    .observations = 0uz
                },
                Case {
                    .name = "static factory selects the yielding dependency",
                    .source = "import std::async using yield_once; "
                              "async fn leaf(value: i32) -> i32 => value + 1; "
                              "async fn other(value: i32) -> i32 { await yield_once(); "
                              "return value + 1; } "
                              "private fn factory(value: i32, const deferred: bool) { "
                              "const if deferred { return other(value); } "
                              "else { return leaf(value); } } "
                              "async fn probe() -> i32 => await factory(7, true);",
                    .observations = 1uz
                },
                Case {
                    .name = "async static input selects a ready residual body",
                    .source = "import std::async using yield_once; "
                              "private async fn leaf(value: i32, const deferred: bool) -> i32 { "
                              "const if deferred { await yield_once(); } return value + 1; } "
                              "async fn probe(value: i32) -> i32 => await leaf(value, false);",
                    .observations = 0uz
                },
                Case {
                    .name = "async static input preserves the selected yield",
                    .source = "import std::async using yield_once; "
                              "private async fn leaf(value: i32, const deferred: bool) -> i32 { "
                              "const if deferred { await yield_once(); } return value + 1; } "
                              "async fn probe(value: i32) -> i32 => await leaf(value, true);",
                    .observations = 1uz
                },
                Case {
                    .name = "real yield stays in the caller coroutine",
                    .source = "import std::async using yield_once; "
                              "async fn leaf(value: i32) -> i32 { await yield_once(); "
                              "return value + 1; } async fn probe() -> i32 => await leaf(7);",
                    .observations = 1uz
                },
            };
            each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_async_generation_source(std::string(input.source)),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("async_fusion")}
                );
                auto factories = std::flat_set<std::string> {"factory"};
                for (const auto& instance : compilation.semantic().static_instances()) {
                    const auto& function =
                        compilation.semantic().declarations().function(instance.function);
                    const auto name = compilation.semantic().provenance().spelling(function.name);
                    if (name == "factory" || name == "leaf" || name == "other") {
                        factories.emplace(compilation.target()
                                              .names()
                                              .callable_identifier(instance.callable)
                                              .spelling());
                    }
                }
                auto summary = AsyncFusionSummary();
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    auto query =
                        AsyncFusionQuery {.unit = unit, .summary = summary, .factories = factories};
                    if (!expect(traverse_target_unit(unit.sections(), query))) {
                        return;
                    }
                }
                expect_equal(summary.bodies, 1uz);
                expect_equal(summary.coroutines, 1uz);
                expect_equal(summary.operations, 0uz);
                expect_equal(summary.cold_calls, 0uz);
                expect_equal(summary.successors, 0uz);
                expect_equal(summary.observations, input.observations);
                if (input.observations == 0) {
                    // Embedded scalar results need neither completion nor construction state.
                    expect_equal(summary.completion_storage, 0uz);
                    expect_equal(summary.deferred_storage, 0uz);
                }
                if (!input.completes) {
                    // A native co_return marker may remain after the infinite body.
                    expect_equal(summary.dereferences, 0uz);
                }
            });
        };

    "Generation async: fused scalar storage survives cleanup and suspension without ownership state"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_async_generation_source(R"(
import std::async using yield_once;
async fn leaf(value: i32) -> i32 {
    var text: String = "abc";
    if value < 0 { return text.len() as i32; }
    await yield_once();
    return value + text.len() as i32;
}
fn factory(value: i32) => leaf(value);
async fn other(value: bool) -> char {
    if value { return 'a'; }
    return 'b';
}
async fn probe(value: i32) -> i32 {
    let selected = await factory(value);
    let letter = await other(selected > 0);
    await yield_once();
    if letter == 'a' { return selected; }
    return 0;
}
)"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("async_scalar_storage")}
            );
            const auto factories = std::flat_set<std::string> {"factory"};
            auto summary = AsyncFusionSummary();
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query =
                    AsyncFusionQuery {.unit = unit, .summary = summary, .factories = factories};
                if (!expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            expect_equal(summary.bodies, 1uz);
            expect_equal(summary.coroutines, 1uz);
            expect_equal(summary.operations, 0uz);
            expect_equal(summary.cold_calls, 0uz);
            expect_equal(summary.completion_storage, 0uz);
            expect_equal(summary.deferred_storage, 0uz);
            expect_equal(summary.observations, 2uz);
        };

    "Generation async: fusion follows discarded success demand while checking characters"_test =
        [] static noexcept {
            struct Case final {
                std::string_view source;
                std::size_t mutable_storage;
                std::size_t character_checks;
                std::size_t observations;
                std::size_t successors;
            };
            const auto cases = std::array {
                Case {
                    R"(
import std::async using yield_once;
fn after() -> i32 => 1;
async fn leaf(value: i32) -> i32 {
    if value < 0 { return after(); }
    await yield_once();
    return after();
}
fn factory(value: i32, unused: i32) => leaf(value);
async fn other(value: bool) -> bool => value;
async fn nested(value: i32) -> i32 => await leaf(value);
async fn probe() { await factory(1, 2); await other(true); await nested(3); }
)",
                    0uz,
                    0uz,
                    2uz,
                    4uz
                },
                Case {
                    R"(
async fn leaf(value: i32) -> i32 => value;
fn factory(value: i32) => leaf(value);
async fn probe() -> i32 => await factory(1);
)",
                    1uz,
                    0uz,
                    0uz,
                    0uz
                },
                Case {
                    R"(
import std::async using yield_once;
async fn leaf(value: bool) -> char {
    if value { return 'a'; }
    await yield_once();
    return 'b';
}
fn factory(value: bool) => leaf(value);
async fn probe(value: bool) { await factory(value); }
)",
                    1uz,
                    1uz,
                    1uz,
                    0uz
                },
                Case {
                    R"(
fn after() -> i32 => 1;
async fn leaf() -> i32 { while {} }
async fn probe() { await leaf(); after(); }
)",
                    0uz,
                    0uz,
                    0uz,
                    0uz
                },
            };
            for (const auto& input : cases) {
                const auto compilation = PlannedCompilation::build(
                    analyze_async_generation_source(std::string(input.source)),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("async_discarded_success")}
                );
                const auto factories = std::flat_set<std::string> {"factory"};
                auto summary = AsyncFusionSummary();
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    auto query =
                        AsyncFusionQuery {.unit = unit, .summary = summary, .factories = factories};
                    if (!expect(traverse_target_unit(unit.sections(), query))
                             .note("source = ", input.source)) {
                        return;
                    }
                }
                expect_equal(summary.mutable_storage, input.mutable_storage)
                    .note("source = ", input.source);
                expect_equal(summary.character_checks, input.character_checks)
                    .note("source = ", input.source);
                expect_equal(summary.observations, input.observations)
                    .note("source = ", input.source);
                expect_equal(summary.successors, input.successors).note("source = ", input.source);
                expect_equal(summary.operations, 0uz).note("source = ", input.source);
                expect_equal(summary.cold_calls, 0uz).note("source = ", input.source);
                expect_equal(summary.deferred_storage, 0uz).note("source = ", input.source);
            }
        };

    "Generation async: static instances publish runtime inputs and residual cancellation"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_async_generation_source(R"(import std::async using cancellation_point;
private async fn leaf(value: i32, const checked: bool) -> i32 {
    const if checked { await cancellation_point(); } return value;
}
async fn probe(value: i32) -> i32 {
    let ready = leaf(value, false); let checked = leaf(value, true);
    return (await ready) + (await checked);
})"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("async_static_instance")}
            );
            expect_equal(compilation.semantic().static_instances().size(), 2uz);
            auto checked_instances = 0uz;
            auto ready_instances = 0uz;
            for (const auto& instance : compilation.semantic().static_instances()) {
                const auto& signature = compilation.semantic().callable_signatures().signature(
                    compilation.semantic().declarations().callable(instance.callable).signature
                );
                expect_equal(signature.execution, CallableExecutionKind::Async);
                if (!expect_equal(signature.parameters.size(), 1uz)) {
                    return;
                }
                expect_equal(signature.parameters.front().stage, ParameterStage::Runtime);
                expect(compilation.semantic()
                           .declarations()
                           .body_for_callable(instance.callable)
                           .has_value());
                const auto cancelled =
                    compilation.semantic().may_complete_cancelled(instance.callable);
                checked_instances += cancelled;
                ready_instances += !cancelled;
            }
            expect_equal(checked_instances, 1uz);
            expect_equal(ready_instances, 1uz);
            // Stored instances retain ordinary Operation consumption and native lowering.
            for (const auto artifact : compilation.target().artifacts()) {
                static_cast<void>(lower_artifact(compilation, artifact.id));
            }
        };

    "Generation async: only possible cancelled completions retain a conditional exit"_test =
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view source;
                std::size_t observations;
                std::size_t conditional_exits;
            };
            const auto cases = std::array {
                Case {
                    .name = "fused ready dependency has no cancelled exit",
                    .source = "async fn ready(value: i32) -> i32 => value; "
                              "async fn probe() -> i32 => await ready(7);",
                    .observations = 0uz,
                    .conditional_exits = 0uz
                },
                Case {
                    .name = "stored yield has no cancelled exit",
                    .source = "import std::async using yield_once; "
                              "async fn probe() { let operation = yield_once(); "
                              "let moved = &&operation; await moved; }",
                    .observations = 1uz,
                    .conditional_exits = 0uz
                },
                Case {
                    .name = "factory checkpoint retains the observed cancelled exit",
                    .source = "import std::async using cancellation_point; "
                              "fn factory() { let operation = cancellation_point(); "
                              "return &&operation; } "
                              "async fn probe() { let operation = factory(); "
                              "let moved = &&operation; await moved; }",
                    .observations = 1uz,
                    .conditional_exits = 1uz
                },
                Case {
                    .name = "nested operand retains its own accepting exits",
                    .source = "import std::async using cancellation_point; "
                              "async fn checked() -> i32 { await cancellation_point(); "
                              "return 7; } async fn ready(value: i32) -> i32 => value; "
                              "async fn probe() -> i32 => await ready(await checked());",
                    .observations = 2uz,
                    .conditional_exits = 2uz
                },
            };
            each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_async_generation_source(std::string(input.source)),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("async_cancellation")}
                );
                struct Query final {
                    std::size_t observations;
                    std::size_t conditional_exits;

                    auto enter_expression(
                        const TargetExpr& expression,
                        TargetExpressionRole
                    ) noexcept -> bool {
                        observations += std::holds_alternative<TargetCoAwaitExpr>(expression.value);
                        return true;
                    }

                    auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                        conditional_exits += std::holds_alternative<TargetIfStmt>(statement.value);
                        return true;
                    }
                };
                auto query = Query {.observations = 0uz, .conditional_exits = 0uz};
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    if (!expect(traverse_target_unit(unit.sections(), query))) {
                        return;
                    }
                }
                // These sources have no conditionals or nominal failure receivers.
                expect_equal(query.observations, input.observations);
                expect_equal(query.conditional_exits, input.conditional_exits);
            });
        };
    "Generation async: awaited operand values remain available without retained backing"_test =
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view source;
                std::size_t observations;
            };
            const auto cases = std::array {
                Case {
                    .name = "fused final call argument",
                    .source = "fn combine(first: i32, last: i32) -> i32 => first + last; "
                              "async fn leaf() -> i32 => 7; "
                              "async fn probe() -> i32 { let value = combine(3, await leaf()); "
                              "return value; }",
                    .observations = 0uz
                },
                Case {
                    .name = "native arithmetic operand",
                    .source = "import(cpp) async fn native_value() -> i32; "
                              "async fn probe() -> i32 { let value = 3 + await native_value(); "
                              "return value; }",
                    .observations = 1uz
                },
                Case {
                    .name = "borrowed factory argument before stored consumption",
                    .source = "import \"context.hpp\"; "
                              "import(cpp) async fn native_value(context: ::Context, "
                              "marker: i32) -> i32; "
                              "fn factory(context: ::Context, marker: i32) "
                              "=> native_value(context, marker); "
                              "async fn leaf() -> i32 => 7; "
                              "async fn probe(context: ::Context) -> i32 { "
                              "let operation = factory(context, await leaf()); "
                              "return await operation; }",
                    .observations = 1uz
                },
            };
            each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_async_generation_source(std::string(input.source)),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("async_operand_value")}
                );
                struct Query final {
                    std::size_t bodies;
                    std::size_t observations;
                    bool measured;

                    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool {
                        const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
                        measured = function
                            && std::holds_alternative<TargetFreeFunctionDefinition>(function->form)
                            && function->name.components().back().spelling() == "probe";
                        bodies += measured;
                        return true;
                    }

                    auto leave_declaration(const TargetDecl&) noexcept -> bool {
                        measured = false;
                        return true;
                    }

                    auto enter_expression(
                        const TargetExpr& expression,
                        TargetExpressionRole
                    ) noexcept -> bool {
                        if (!measured) {
                            return true;
                        }
                        observations += std::holds_alternative<TargetCoAwaitExpr>(expression.value);
                        return true;
                    }
                };
                auto query = Query {
                    .bodies = 0uz,
                    .observations = 0uz,
                    .measured = false,
                };
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    if (!expect(traverse_target_unit(unit.sections(), query))) {
                        return;
                    }
                }
                expect_equal(query.bodies, 1uz);
                expect_equal(query.observations, input.observations);
            });
        };

    "Generation async: native boundaries retain source borrowing and cold carriers"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_async_generation_source(R"(import "context.hpp";
export struct IoError { code: i32 }
import(cpp) async fn native_read(context: ::Context) -> i32 throw IoError;
fn factory(context: ::Context) => native_read(context);
export(cpp) async fn probe(context: ::Context) -> i32 throw IoError {
    let operation = factory(context);
    return await operation?;
})"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("async_native_boundary")}
            );
            struct Query final {
                const TargetUnit& unit;
                std::size_t& borrowed_signatures;
                std::size_t& native_forwarders;

                auto enter_declaration(const TargetDecl& declaration) noexcept -> bool {
                    const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
                    if (function == nullptr) {
                        return true;
                    }
                    const auto name = function->name.components().back().spelling();
                    if (name != "native_read" && name != "factory" && name != "probe") {
                        return true;
                    }
                    if (!expect_equal(function->parameters.size(), 1uz)) {
                        return false;
                    }
                    const auto* reference = std::get_if<TargetReferenceType>(
                        &unit.type(function->parameters.front().type).value
                    );
                    if (!expect(reference != nullptr)) {
                        return false;
                    }
                    expect(reference->const_qualified);
                    expect(!reference->rvalue);
                    ++borrowed_signatures;
                    const auto* result =
                        std::get_if<TargetIntrinsicType>(&unit.type(function->result).value);
                    if (!expect(result != nullptr)) {
                        return false;
                    }
                    expect_equal(result->symbol, TargetSymbol::RuntimeAsyncOperation);
                    if (name == "native_read") {
                        if (const auto* definition =
                                std::get_if<TargetFreeFunctionDefinition>(&function->form)) {
                            // Native imports forward a cold carrier without another activation.
                            expect_equal(definition->execution, TargetCallableExecution::Ordinary);
                            ++native_forwarders;
                        }
                    }
                    return true;
                }
            };
            auto borrowed_signatures = 0uz;
            auto native_forwarders = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {
                    .unit = unit,
                    .borrowed_signatures = borrowed_signatures,
                    .native_forwarders = native_forwarders,
                };
                if (!expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            expect_greater(borrowed_signatures, 2uz);
            expect_equal(native_forwarders, 1uz);
        };
    "Generation async: self tail awaits reuse scalar iterations with closed lifetimes"_test = [] static noexcept {
        struct Case final {
            std::string_view name;
            std::string_view source;
            bool loops;
        };
        const auto cases = std::array {
            Case {
                "direct",
                "async fn probe(n: i32) -> i32 { if n == 0 { return 7; } return await probe(n - 1); }",
                true
            },
            Case {
                "structured return",
                "async fn probe(n: i32) -> i32 { return if n == 0 { 7 } else { await probe(n - 1) }; }",
                true
            },
            Case {
                "match return",
                "async fn probe(n: i32) -> i32 { return match n { 0 => 7, _ => await probe(n - 1), }; }",
                true
            },
            Case {
                "nested loop",
                "async fn probe(n: i32) -> i32 { if n == 0 { return 7; } for var i = 0; i < 1; ++i { return await probe(n - 1); } return 0; }",
                true
            },
            Case {
                "explicit yield",
                "import std::async using yield_once; async fn probe(n: i32) -> i32 { await yield_once(); if n == 0 { return 7; } return await probe(n - 1); }",
                true
            },
            Case {
                "non-tail",
                "async fn probe(n: i32) -> i32 { if n == 0 { return 7; } return 1 + await probe(n - 1); }",
                false
            },
            Case {
                "saved operation",
                "async fn probe(n: i32) -> i32 { if n == 0 { return 7; } let next = probe(n - 1); return await next; }",
                false
            },
            Case {
                "write helper",
                "fn changed(&n: i32) { n -= 1; } async fn probe(n: i32) -> i32 { var next = n; changed(&next); if n == 0 { return 7; } return await probe(next); }",
                false
            },
            Case {
                "nested actual await",
                "async fn actual(n: i32) -> i32 => n - 1; async fn probe(n: i32) -> i32 { if n == 0 { return 7; } return await probe(await actual(n)); }",
                false
            },
            Case {
                "character validation",
                "async fn probe(n: i32) -> char { if n == 0 { return 'a'; } return await probe(n - 1); }",
                false
            },
            Case {
                "outward failure",
                "struct Error {} async fn probe(n: i32) -> i32 throw Error { if n < 0 { throw Error {}; } if n == 0 { return 7; } return await probe(n - 1)?; }",
                false
            },
            Case {
                "cancelled completion",
                "import std::async using cancellation_point; async fn probe(n: i32) -> i32 { await cancellation_point(); if n == 0 { return 7; } return await probe(n - 1); }",
                false
            },
            Case {
                "conditional cleanup",
                "async fn condition(n: i32) -> bool => n > 0; async fn probe(n: i32) -> i32 { if n == 0 { return 7; } return if await condition(n) { await probe(n - 1) } else { 0 }; }",
                false
            },
        };
        each(cases, &Case::name, [](const Case& input) static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_async_generation_source(std::string(input.source)),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("async_tail")}
            );
            auto summary = AsyncFusionSummary {};
            const auto names = std::flat_set<std::string> {"probe"};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = AsyncFusionQuery {unit, summary, names};
                require(traverse_target_unit(unit.sections(), query));
            }
            expect_equal(summary.bodies, 1uz);
            // The selected edge creates no recursive cold carrier. Lowering
            // also seals all native loop transfers and initialization scopes.
            if (input.loops) {
                expect_equal(summary.cold_calls, 0uz);
            } else {
                expect_greater(summary.cold_calls, 0uz);
            }
        });
    };
});

} // namespace
