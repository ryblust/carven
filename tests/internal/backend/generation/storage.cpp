module carven:test.internal.backend.generation.storage;

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

    ct::test(
        "Generation: independent root calls initialize Outcomes without deferred storage",
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view body;
            };
            constexpr auto cases = std::array {
                Case {.name = "discard", .body = "produce(flag)?;"},
                Case {.name = "binding", .body = "let value = produce(flag)?; observe(value);"},
                Case {
                    .name = "conditional argument",
                    .body = "produce(if flag { true } else { false })?;"
                },
                Case {
                    .name = "statement argument",
                    .body = "produce(if flag { observe(1); true } else { observe(2); false })?;"
                },
            };
            ct::each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_test_program(
                        std::format(
                            "enum Error {{ Failed, }} "
                            "fn produce(flag: bool) -> i32 throw Error {{ if flag {{ return 7; }} throw Error::Failed; }} "
                            "fn observe(value: i32) {{}} "
                            "fn probe(flag: bool) throw Error {{ {} }}",
                            input.body
                        )
                    ),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("root_outcome")}
                );

                struct Query final {
                    const TargetUnit& unit;
                    std::size_t direct;
                    std::size_t deferred;

                    auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                        const auto* variable = std::get_if<TargetVariableStmt>(&statement.value);
                        if (variable == nullptr) {
                            return true;
                        }
                        const auto* type =
                            std::get_if<TargetIntrinsicType>(&unit.type(variable->type).value);
                        if (type == nullptr) {
                            return true;
                        }
                        if (type->symbol == TargetSymbol::RuntimeOutcome) {
                            ++direct;
                            ct::expect(
                                std::holds_alternative<TargetCallExpr>(variable->initializer.value)
                            );
                        }
                        if (type->symbol == TargetSymbol::RuntimeDeferredResult) {
                            if (!ct::expect(type->type_argument_ids.size() == 1uz)) {
                                return false;
                            }
                            const auto* result = std::get_if<TargetIntrinsicType>(
                                &unit.type(type->type_argument_ids.front()).value
                            );
                            deferred +=
                                result != nullptr && result->symbol == TargetSymbol::RuntimeOutcome;
                        }
                        return true;
                    }
                };

                auto direct = 0uz;
                auto deferred = 0uz;
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    auto query = Query {.unit = unit, .direct = 0uz, .deferred = 0uz};
                    ct::expect(traverse_target_unit(unit.sections(), query));
                    direct += query.direct;
                    deferred += query.deferred;
                }
                ct::expect_equal(direct, 1uz);
                ct::expect_equal(deferred, 0uz);
            });
        }
    );

    ct::test(
        "Generation: scalar predecessors in a full expression need no deferred storage",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn first() -> i32 { return 1; } "
                    "fn second() -> i32 { return 2; } "
                    "fn pair(a: i32, b: i32) -> i32 { return a * 10 + b; } "
                    "fn probe() -> i32 { return pair(first(), second()); }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("scalar_predecessors")}
            );

            struct Query final {
                const TargetUnit& unit;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        if (const auto* type = std::get_if<TargetIntrinsicType>(
                                &unit.type(variable->type).value
                            )) {
                            ct::expect(type->symbol != TargetSymbol::RuntimeDeferredResult);
                        }
                    }
                    return true;
                }
            };

            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
        }
    );

    ct::test(
        "Generation: local storage follows retained access rather than source write permission",
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view source;
                TargetVariableBinding expected;
            };

            const auto cases = std::array {
                Case {
                    .name = "read-only var",
                    .source =
                        "fn probe(value: i32) -> i32 { var storage = value; return storage; }",
                    .expected = TargetVariableBinding::ConstValue
                },
                Case {
                    .name = "assignment",
                    .source =
                        "fn probe(value: i32) -> i32 { var storage = value; storage += 1; return storage; }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "Write argument",
                    .source = "fn write(&value: i32) { value = 2; } "
                              "fn probe(value: i32) { var storage = value; write(&storage); }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "Write capture",
                    .source =
                        "fn probe(value: i32) -> i32 { var storage = value; "
                        "let read = [&storage]() -> i32 { return storage; }; return read(); }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "let transfer",
                    .source =
                        "fn probe(value: String) -> String { let storage = value; return &&storage; }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "read field",
                    .source =
                        "struct Box { value: i32 } "
                        "fn probe(value: i32) -> i32 { var storage = Box { value }; return storage.value; }",
                    .expected = TargetVariableBinding::ConstValue
                },
                Case {
                    .name = "write field",
                    .source =
                        "struct Box { value: i32 } "
                        "fn probe(value: i32) { var storage = Box { value }; storage.value = 2; }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "read element",
                    .source =
                        "fn probe(value: i32) -> i32 { var storage = [value]; return storage[0]; }",
                    .expected = TargetVariableBinding::ConstValue
                },
                Case {
                    .name = "write element",
                    .source = "fn probe(value: i32) { var storage = [value]; storage[0] = 2; }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "write iteration",
                    .source =
                        "fn probe(value: i32) { var storage = [value]; for &element in storage { element += 1; } }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "write pointee",
                    .source =
                        "fn probe(pointer: ptr<&i32>) { var storage = pointer; if storage != nullptr { *storage = 2; } }",
                    .expected = TargetVariableBinding::ConstValue
                },
                Case {
                    .name = "unreachable write does not require mutable storage",
                    .source =
                        "fn probe(value: i32) -> i32 { var storage = value; if false { storage = 2; } return storage; }",
                    .expected = TargetVariableBinding::ConstValue
                },
            };
            ct::each(cases, &Case::name, [](const Case& scenario) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_test_program(std::string(scenario.source)),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("local_storage")}
                );

                struct Query final {
                    const TargetUnit& unit;
                    TargetVariableBinding expected;
                    std::size_t owners;

                    auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                        const auto* variable = std::get_if<TargetVariableStmt>(&statement.value);
                        if (variable != nullptr
                            && unit.local_name(variable->local).spelling() == "storage") {
                            ++owners;
                            ct::expect(variable->binding == expected);
                        }
                        return true;
                    }
                };

                auto owners = 0uz;
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    auto query = Query {
                        .unit = unit,
                        .expected = scenario.expected,
                        .owners = 0uz,
                    };
                    if (!(ct::expect(traverse_target_unit(unit.sections(), query)))) {
                        return;
                    }
                    owners += query.owners;
                }
                ct::expect(owners == 1uz);
            });
        }
    );

    ct::test(
        "Generation: a folded short circuit does not defer later argument storage",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "import <string> using std::string; "
                    "fn probe() -> bool { return true; } "
                    "fn show() { println(false && probe(), "
                    "string { c\"first\" }, string { c\"second\" }); }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("folded_short_circuit")}
            );

            struct Query final {
                const TargetUnit& unit;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        if (const auto* type = std::get_if<TargetIntrinsicType>(
                                &unit.type(variable->type).value
                            )) {
                            ct::expect(type->symbol != TargetSymbol::RuntimeDeferredResult);
                        }
                    }
                    return true;
                }
            };

            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
        }
    );

    ct::test(
        "Generation: independent expression and region results use automatic outcome storage",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            struct Failure {}
            fn source(flag: bool) -> i32 throw Failure {
                if flag { throw Failure {}; }
                return 7;
            }
            fn recover(flag: bool) -> i32 => try {
                let value = source(flag)?;
                value
            } catch { Failure(_) => 0, };
            fn tail(flag: bool) -> i32 => try { source(flag)? } catch { Failure(_) => 0, };
            fn selected(flag: bool) -> i32 => if flag {
                (try { source(flag)? } catch { Failure(_) => 0, })
            } else { 0 };
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("nested_full_expression")}
            );

            struct Query final {
                const TargetUnit& unit;
                std::size_t outcomes = 0;

                auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool {
                    const auto* type =
                        std::get_if<TargetIntrinsicType>(&unit.type(variable.type).value);
                    if (type != nullptr && type->symbol == TargetSymbol::RuntimeOutcome) {
                        ++outcomes;
                    }
                    if (type != nullptr && type->symbol == TargetSymbol::RuntimeDeferredResult) {
                        const auto* contained = std::get_if<TargetIntrinsicType>(
                            &unit.type(type->type_argument_ids.front()).value
                        );
                        if (!ct::expect(contained != nullptr)) {
                            return false;
                        }
                        ct::expect(contained->symbol != TargetSymbol::RuntimeOutcome);
                    }
                    return true;
                }
            };

            auto outcomes = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
                outcomes += query.outcomes;
            }
            ct::expect(outcomes == 3uz);
        }
    );
});

} // namespace
