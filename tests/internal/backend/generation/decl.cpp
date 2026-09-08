module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.decl;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.lowering.body.decl;
import :backend.target;
import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

auto name(std::string_view spelling) noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling(spelling);
}

auto reference(std::string_view spelling) noexcept -> TargetExpr {
    return {.value = TargetNameExpr {.name = TargetName(name(spelling))}};
}

auto literal() noexcept -> TargetExpr {
    return {.value = TargetLiteralExpr {.value = true}};
}

auto local(std::string_view spelling, TargetTypeID type) noexcept -> TargetStmt {
    return target_lowering_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = true,
            .name = name(spelling),
            .type = type,
            .initializer = literal(),
        }
    );
}

auto read(std::string_view spelling) noexcept -> TargetStmt {
    return target_lowering_statement(TargetDiscardStmt {.expression = reference(spelling)});
}

auto assignment(std::string_view spelling) noexcept -> TargetStmt {
    return target_lowering_statement(
        TargetAssignmentStmt {
            .target = reference(spelling),
            .op = TargetAssignmentOperator::Assign,
            .value = literal()
        }
    );
}

struct DeclarationFlags final {
    std::vector<bool> result;

    auto visit_variable(const auto& variable) noexcept -> bool {
        result.push_back(variable.maybe_unused);
        return true;
    }
};

auto flags(const std::vector<TargetStmt>& statements) noexcept -> std::vector<bool> {
    auto query = DeclarationFlags();
    CHECK(traverse_target_statements(statements, query));
    return query.result;
}

auto boolean_type(TargetUnitBuilder& builder) noexcept -> TargetTypeID {
    return builder.intern_type({
        .value = TargetIntrinsicType {.symbol = TargetSymbol::Bool, .type_argument_ids = {}},
        .const_qualified = false,
    });
}

} // namespace

TEST_CASE("Declarations: reads clear attributes while writes retain names") {
    auto builder = TargetUnitBuilder();
    const auto type = boolean_type(builder);
    auto body = std::vector<TargetStmt>();
    body.push_back(local("returned", type));
    body.push_back(local("written", type));
    body.push_back(local("updated", type));
    body.push_back(local("unused", type));
    body.push_back(assignment("written"));
    body.push_back(assignment("parameter"));
    body.push_back(target_lowering_statement(
        TargetUpdateStmt {.op = TargetUpdateOperator::Increment, .target = reference("updated")}
    ));
    body.push_back(
        target_lowering_statement(TargetReturnStmt {.expression = reference("returned")})
    );
    const auto parameters = std::array {name("parameter"), name("absent")};
    CHECK(finish_body_declarations(body, parameters, {}) == std::vector<bool> {true, false});
    CHECK(flags(body) == std::vector<bool> {false, true, true, true});
}

TEST_CASE("Declarations: sibling declarations and lambda parameters have lexical identities") {
    auto builder = TargetUnitBuilder();
    const auto type = boolean_type(builder);
    auto body = std::vector<TargetStmt>();
    body.push_back(local("outer", type));
    body.push_back(local("shadowed", type));
    auto lambda_body = std::vector<TargetStmt>();
    lambda_body.push_back(read("outer"));
    lambda_body.push_back(read("shadowed"));
    body.push_back(target_lowering_statement(
        TargetExprStmt {
            .expression = {
                .value = TargetLambdaExpr {
                    .parameters = {{.name = name("shadowed"), .type = type}},
                    .result = type,
                    .body = std::move(lambda_body)
                }
            }
        }
    ));
    auto left = std::vector<TargetStmt>();
    left.push_back(local("same", type));
    left.push_back(read("same"));
    auto right = std::vector<TargetStmt>();
    right.push_back(local("same", type));
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back({.condition = literal(), .body = std::move(left)});
    body.push_back(target_lowering_statement(
        TargetIfStmt {.branches = std::move(branches), .else_body = std::move(right)}
    ));
    static_cast<void>(finish_body_declarations(body, {}, {}));
    CHECK(flags(body) == std::vector<bool> {false, true, false, true});
}

TEST_CASE("Declarations: loop bindings and steps use their own visibility") {
    auto builder = TargetUnitBuilder();
    const auto type = boolean_type(builder);
    auto body = std::vector<TargetStmt>();
    body.push_back(local("step", type));
    auto loop_body = std::vector<TargetStmt>();
    loop_body.push_back(local("step", type));
    loop_body.push_back(read("index"));
    auto steps = std::vector<TargetForStep>();
    steps.push_back({.value = TargetDiscardStmt {.expression = reference("step")}});
    body.push_back(target_lowering_statement(
        TargetForStmt {
            .initializer =
                TargetForInitializer {
                    .value =
                        TargetVariableStmt {
                            .binding = TargetVariableBinding::MutableValue,
                            .maybe_unused = true,
                            .name = name("index"),
                            .type = type,
                            .initializer = literal()
                        }
                },
            .condition = literal(),
            .steps = std::move(steps),
            .body = std::move(loop_body)
        }
    ));
    body.push_back(local("range", type));
    auto iteration = std::vector<TargetStmt>();
    iteration.push_back(read("range"));
    body.push_back(target_lowering_statement(
        TargetRangeForStmt {
            .binding = TargetVariableBinding::ConstReference,
            .maybe_unused = true,
            .name = name("range"),
            .type = type,
            .range = reference("range"),
            .body = std::move(iteration)
        }
    ));
    static_cast<void>(finish_body_declarations(body, {}, {}));
    CHECK(flags(body) == std::vector<bool> {false, false, true, false, false});
}

TEST_CASE("Declarations: final generated bodies own parameter and local use facts") {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(
            "fn consume(value: i32) {} "
            "fn forward(parameter: i32) { let local = parameter; return consume(local); } "
            "fn inactive(parameter: i32) { if false { consume(parameter); } } "
            "fn only_write(&parameter: i32) { parameter = 2; } "
            "fn unused_local() { let retained = 2; } "
            "struct Stop {} fn pair(&first: i32, second: i32) {} "
            "fn terminal(&parameter: i32) throw Stop { "
            "return pair(&parameter, if true { throw Stop {}; } else { throw Stop {}; }); } "
        ),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("declarations")}
    );

    struct Query final {
        std::size_t definitions = 0;
        std::size_t locals = 0;

        auto enter_declaration(const TargetDecl& declaration) noexcept -> bool {
            const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
            if (function == nullptr
                || !std::holds_alternative<TargetFreeFunctionDefinition>(function->form)) {
                return true;
            }
            ++definitions;
            const auto spelling = function->name.components().back().spelling();
            if (spelling == "forward" || spelling == "only_write") {
                REQUIRE(function->parameters.size() == 1uz);
                CHECK(function->parameters.front().name.has_value());
            } else if (spelling == "inactive" || spelling == "consume") {
                REQUIRE(function->parameters.size() == 1uz);
                CHECK_FALSE(function->parameters.front().name.has_value());
            }
            return true;
        }

        auto enter_statement(const TargetStmt& statement) noexcept -> bool {
            if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                ++locals;
                CHECK(variable->maybe_unused == (variable->name.spelling() != "local"));
            }
            return true;
        }
    };

    auto query = Query();
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        CHECK(traverse_target_unit(unit.sections(), query));
    }
    CHECK(query.definitions == 7uz);
    CHECK(query.locals == 3uz);
}
