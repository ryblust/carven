module carven:test.internal.backend.generation.storage;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.decl;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

struct StorageSummary final {
    std::size_t deferred;
    std::size_t deferred_integers;
    std::vector<bool> deferred_strings;
    std::size_t bodies;
};

struct StorageQuery final {
    const TargetUnit& unit;
    StorageSummary& summary;
    bool measured;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool;
    auto leave_declaration(const TargetDecl& declaration) noexcept -> bool;
    auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool;
};

auto StorageQuery::enter_declaration(const TargetDecl& declaration) noexcept -> bool {
    const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
    measured = function != nullptr
        && function->name.components().back().spelling() == "probe"
        && std::holds_alternative<TargetFreeFunctionDefinition>(function->form);
    summary.bodies += measured;
    return true;
}

auto StorageQuery::leave_declaration(const TargetDecl&) noexcept -> bool {
    measured = false;
    return true;
}

auto StorageQuery::visit_variable(const TargetVariableStmt& variable) noexcept -> bool {
    if (!measured) {
        return true;
    }
    const auto* type = std::get_if<TargetIntrinsicType>(&unit.type(variable.type).value);
    if (type == nullptr) {
        return true;
    }
    const auto deferred = type->symbol == TargetSymbol::RuntimeDeferredResult;
    summary.deferred += deferred;
    if (deferred) {
        if (!ct::expect_equal(type->type_argument_ids.size(), 1uz)) {
            return false;
        }
        type = std::get_if<TargetIntrinsicType>(&unit.type(type->type_argument_ids.front()).value);
    }
    if (deferred && type != nullptr && type->symbol == TargetSymbol::StdInt32) {
        ++summary.deferred_integers;
    }
    if (type != nullptr && type->symbol == TargetSymbol::RuntimeString) {
        summary.deferred_strings.push_back(deferred);
    }
    return true;
}

auto inspect_storage(std::string source) noexcept -> StorageSummary {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(std::move(source)),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("storage_scopes")}
    );
    auto summary = StorageSummary {
        .deferred = 0uz,
        .deferred_integers = 0uz,
        .deferred_strings = {},
        .bodies = 0uz
    };
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        auto query = StorageQuery {.unit = unit, .summary = summary, .measured = false};
        ct::require(traverse_target_unit(unit.sections(), query));
    }
    ct::expect_equal(summary.bodies, 1uz);
    return summary;
}

const ct::Suite tests([] static noexcept {
    ct::test(
        "Generation: unconditional objects use ordinary storage before later control",
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view operand;
            };
            const auto cases = std::array {
                Case {.name = "scalar choice", .operand = "if flag { 1 } else { 2 }"},
                Case {
                    .name = "statement choice",
                    .operand = "if flag { observe(1); 1 } else { observe(2); 2 }"
                },
                Case {.name = "always failing call", .operand = "stop()?"},
            };
            ct::each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto summary = inspect_storage(
                    std::format(
                        "enum Error {{ Failed, }} "
                        "fn observe(value: i32) {{}} "
                        "fn stop() -> i32 throw Error {{ throw Error::Failed; }} "
                        "fn accept(text: String, value: i32) -> i32 => value; "
                        "fn probe(flag: bool) -> i32 throw Error => "
                        "accept(\"owner\" as String, {});",
                        input.operand
                    )
                );
                ct::expect_equal(summary.deferred, 0uz);
                ct::expect_equal(summary.deferred_strings, std::vector<bool> {false});
            });
        }
    );

    ct::test(
        "Generation: conditional reservations retain their position between ordinary owners",
        [] static noexcept {
            const auto summary = inspect_storage(
                "fn observe(text: String) -> bool => true; "
                "fn combine(first: bool, second: bool, text: String) -> bool => first && second; "
                "fn probe(flag: bool) -> bool => combine("
                "observe(\"first\" as String), flag && observe(\"selected\" as String), "
                "\"last\" as String);"
            );
            ct::expect_equal(summary.deferred, 1uz);
            ct::expect_equal(summary.deferred_strings, std::vector<bool> {false, true, false});
        }
    );

    ct::test(
        "Generation: builtin writer values need no deferred scalar backing",
        [] static noexcept {
            const auto summary = inspect_storage(
                "fn pure(value: i32) -> i32 => value; "
                "fn probe(flag: bool) -> bool => "
                "flag && (f\"{pure(1)}/{pure(2)}\".len() > 0);"
            );
            ct::expect_equal(summary.deferred_integers, 0uz);
        }
    );

    ct::test(
        "Generation: native Read scalar backing escapes selected evaluation scopes",
        [] static noexcept {
            const auto summary = inspect_storage(
                "import \"probe.hpp\" using probe::Observer; "
                "fn pure(value: i32) -> i32 => value; "
                "fn event() -> i32 => 5; "
                "fn probe(flag: bool) -> bool { let observer = Observer {}; return "
                "observer.observe(flag && (observer.remember(pure(3), event()) as bool)) as bool; }"
            );
            ct::expect_equal(summary.deferred_integers, 1uz);
        }
    );

    ct::test(
        "Generation: unconditional native result queries need no deferred category adapter",
        [] static noexcept {
            const auto calls =
                std::array<std::string_view, 3> {"owner()", "borrow()", "borrow_const()"};
            ct::each(calls, std::identity {}, [](std::string_view call) static noexcept {
                const auto summary = inspect_storage(
                    std::format(
                        "import \"probe.hpp\" using probe::{{ owner, borrow, borrow_const, observe }}; "
                        "fn probe(flag: bool) -> i32 => observe({}, if flag {{ 1 }} else {{ 2 }}) as i32;",
                        call
                    )
                );
                ct::expect_equal(summary.deferred, 0uz);
            });
        }
    );
});

} // namespace
