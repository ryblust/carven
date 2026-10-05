module carven:test.internal.backend.generation.observation;

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

struct ObservationQuery final {
    bool measured;
    std::size_t bodies;
    std::size_t locals;
    std::size_t pure_calls;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool;
    auto leave_declaration(const TargetDecl& declaration) noexcept -> bool;
    auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole role) noexcept -> bool;
};

auto ObservationQuery::enter_declaration(const TargetDecl& declaration) noexcept -> bool {
    const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
    measured = function != nullptr
        && function->name.components().back().spelling() == "probe"
        && std::holds_alternative<TargetFreeFunctionDefinition>(function->form);
    bodies += measured;
    return true;
}

auto ObservationQuery::leave_declaration(const TargetDecl&) noexcept -> bool {
    measured = false;
    return true;
}

auto ObservationQuery::visit_variable(const TargetVariableStmt&) noexcept -> bool {
    locals += measured;
    return true;
}

auto ObservationQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    if (!measured) {
        return true;
    }
    if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
        if (const auto* name =
                std::get_if<TargetNameExpr>(&template_primary_expression(*call->callee).value)) {
            pure_calls += name->name.components().back().spelling() == "pure";
        }
    }
    return true;
}

const TestSuite suite([] static noexcept {
    "Generation: unexposed snapshot owners need no operand storage"_test = [] static noexcept {
        struct Case final {
            std::string_view name;
            std::string_view source;
            std::size_t locals;
            std::size_t pure_calls;
        };
        const auto cases = std::array {
            Case {
                .name = "immutable local before call",
                .source = "fn probe(input: i32) -> i32 { let value = input; "
                          "return pair(value, pure(3)); }",
                .locals = 1uz,
                .pure_calls = 1uz
            },
            Case {
                .name = "unmodified mutable local before call",
                .source = "fn probe(input: i32) -> i32 { var value = input; "
                          "return pair(value, pure(3)); }",
                .locals = 1uz,
                .pure_calls = 1uz
            },
            Case {
                .name = "Take parameter before call",
                .source = "fn probe(&&value: i32) -> i32 => pair(value, pure(3));",
                .locals = 0uz,
                .pure_calls = 1uz
            },
            Case {
                .name = "Read parameter before effect",
                .source = "fn effect() -> i32 { println(2); return 2; } "
                          "fn probe(value: i32) -> i32 => pair(value, effect());",
                .locals = 0uz,
                .pure_calls = 0uz
            },
            Case {
                .name = "Write parameter before effect",
                .source = "fn effect() -> i32 { println(2); return 2; } "
                          "fn probe(&value: i32) -> i32 => pair(value, effect());",
                .locals = 1uz,
                .pure_calls = 0uz
            },
            Case {
                .name = "disjoint local write",
                .source = "fn bump(&target: i32) -> i32 { target += 1; return target; } "
                          "fn probe(input: i32) -> i32 { let value = input; var other = 2; "
                          "return pair(value, bump(&other)); }",
                .locals = 2uz,
                .pure_calls = 0uz
            },
            Case {
                .name = "checked division",
                .source = "fn probe(input: i32, divisor: i32) -> i32 { let value = input; "
                          "return pair(value, 100 / divisor); }",
                .locals = 1uz,
                .pure_calls = 0uz
            },
            Case {
                .name = "Write access retains an earlier snapshot",
                .source = "fn bump(&target: i32) -> i32 { target += 1; return target; } "
                          "fn probe(input: i32) -> i32 { var value = input; "
                          "return pair(value, bump(&value)); }",
                .locals = 2uz,
                .pure_calls = 0uz
            },
        };
        each(cases, &Case::name, [](const Case& input) static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn pure(input: i32) -> i32 => input; "
                    "fn pair(first: i32, second: i32) -> i32 => first + second; "
                    + std::string(input.source)
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("observation")}
            );
            auto query = ObservationQuery {
                .measured = false,
                .bodies = 0uz,
                .locals = 0uz,
                .pure_calls = 0uz,
            };
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), query));
            }
            expect_equal(query.bodies, 1uz);
            expect_equal(query.locals, input.locals);
            expect_equal(query.pure_calls, input.pure_calls);
        });
    };

    "Generation: builtin pointer observations need no temporary storage"_test = [] static noexcept {
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(
                "fn present(p: ptr<i32>) -> bool { return p != nullptr; }\n"
                "fn read(p: ptr<i32>) -> i32 {\n"
                "  if p == nullptr { return 0; }\n"
                "  return *p;\n"
                "}\n"
            ),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("pointer_observation")}
        );

        struct Query final {
            std::size_t comparisons;
            std::size_t dereferences;

            auto enter_statement(const TargetStmt& statement) const noexcept -> bool {
                expect(!(std::holds_alternative<TargetVariableStmt>(statement.value)));
                return true;
            }

            auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                -> bool {
                expect(!(std::holds_alternative<TargetLambdaExpr>(expression.value)));
                if (const auto* binary = std::get_if<TargetBinaryExpr>(&expression.value)) {
                    comparisons += binary->op == TargetBinaryOperator::Equal
                        || binary->op == TargetBinaryOperator::NotEqual;
                }
                if (const auto* prefix = std::get_if<TargetPrefixExpr>(&expression.value)) {
                    dereferences += prefix->op == TargetPrefixOperator::Dereference;
                }
                return true;
            }
        };

        auto query = Query {.comparisons = 0uz, .dereferences = 0uz};
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            expect(traverse_target_unit(unit.sections(), query));
        }
        expect(query.comparisons == 2uz);
        expect(query.dereferences == 1uz);
    };

    "Generation: explicit writable source pointers retain their access contract"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn writable(p: ptr<&i32>) -> ptr<i32> { let saved = p; return saved; }\n"
                    "fn readonly(p: ptr<i32>) -> ptr<i32> { let saved = p; return saved; }\n"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("source_pointer_access")}
            );

            struct Query final {
                const TargetUnit& unit;
                std::size_t writable;
                std::size_t readonly;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        const auto* pointer =
                            std::get_if<TargetPointerType>(&unit.type(variable->type).value);
                        if (!expect(pointer != nullptr)) {
                            return false;
                        }
                        const auto& pointee = unit.type(pointer->pointee);
                        const auto* intrinsic = std::get_if<TargetIntrinsicType>(&pointee.value);
                        const auto constant = pointee.const_qualified
                            || (intrinsic != nullptr
                                && intrinsic->symbol == TargetSymbol::StdAddConst);
                        writable += !constant;
                        readonly += constant;
                        expect(variable->binding == TargetVariableBinding::ConstValue);
                    }
                    return true;
                }
            };

            auto writable = 0uz;
            auto readonly = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit, .writable = 0uz, .readonly = 0uz};
                expect(traverse_target_unit(unit.sections(), query));
                writable += query.writable;
                readonly += query.readonly;
            }
            expect(writable == 1uz);
            expect(readonly == 1uz);
        };

    "Generation: builtin Read snapshots use unqualified value factory results"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            fn advance(&value: i32) -> i32 { value += 1; return value; }
            fn format(&value: i32) -> String {
                var output = String {};
                return f"{if true {
                    output.append_format(f"{value}/{advance(&value)}");
                    value
                } else { 0 }}";
            }
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("read_snapshot")}
            );

            struct Query final {
                const TargetUnit& unit;
                std::size_t scalar_factories;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    const auto* lambda = std::get_if<TargetLambdaExpr>(&expression.value);
                    if (lambda != nullptr) {
                        const auto& result = unit.type(lambda->result);
                        const auto* type = std::get_if<TargetIntrinsicType>(&result.value);
                        if (type != nullptr && type->symbol == TargetSymbol::StdInt32) {
                            ++scalar_factories;
                            expect(!(result.const_qualified));
                        }
                    }
                    return true;
                }
            };

            auto scalar_factories = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit, .scalar_factories = 0uz};
                if (!expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
                scalar_factories += query.scalar_factories;
            }
            expect(scalar_factories > 0uz);
        };

    "Generation: pure Read arguments need no borrowed temporary storage in match arms"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            struct Record { value: i32 }
            enum Kind { Invalid }
            struct Failure { record: Record, kind: Kind }
            fn failure(record: Record, kind: Kind) -> Failure => { record: record, kind: kind };
            fn probe(value: i32) throw Failure {
                match value {
                    0 if value >= 0 => throw failure({ value: value }, .Invalid),
                    _ => {},
                }
            }
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("read_value_temporaries")}
            );

            struct Query final {
                const TargetUnit& unit;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        if (const auto* type = std::get_if<TargetIntrinsicType>(
                                &unit.type(variable->type).value
                            )) {
                            expect(type->symbol != TargetSymbol::RuntimeDeferredResult);
                        }
                    }
                    return true;
                }
            };

            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                if (!expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
        };
});

} // namespace
