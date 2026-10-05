module carven:test.internal.backend.generation.decl;

import :backend.generation.linkage;
import :backend.generation.names;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.realization.decl;
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
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

auto name(std::string_view spelling) noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling(spelling);
}

auto reference(TargetLocalID id) noexcept -> TargetExpr {
    return {.value = TargetLocalExpr {.local = id}};
}

auto literal() noexcept -> TargetExpr {
    return {.value = TargetLiteralExpr {.value = true}};
}

auto local(TargetLocalID id, TargetTypeID type) noexcept -> TargetStmt {
    return target_lowering_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = true,
            .local = id,
            .type = type,
            .initializer = literal(),
        }
    );
}

auto read(TargetLocalID id) noexcept -> TargetStmt {
    return target_lowering_statement(TargetDiscardStmt {.expression = reference(id)});
}

auto assignment(TargetLocalID id) noexcept -> TargetStmt {
    return target_lowering_statement(
        TargetAssignmentStmt {
            .target = reference(id),
            .op = TargetAssignmentOperator::Assign,
            .value = literal()
        }
    );
}

auto jump(std::string_view label) noexcept -> TargetStmt {
    return target_lowering_statement(
        TargetGotoStmt {.label = name(label), .role = TargetJumpRole::ForLoopContinue}
    );
}

auto exit(std::string_view label) noexcept -> TargetStmt {
    return target_lowering_statement(
        TargetLabelStmt {.label = name(label), .role = TargetJumpRole::ForLoopContinue}
    );
}

auto conditional_jump(std::string_view label) noexcept -> TargetStmt {
    auto body = std::vector<TargetStmt>();
    body.push_back(jump(label));
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back({.condition = literal(), .body = std::move(body)});
    return target_lowering_statement(
        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
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
    expect(traverse_target_statements(statements, query));
    return query.result;
}

auto boolean_type(TargetUnitBuilder& builder) noexcept -> TargetTypeID {
    return builder.intern_type({
        .value = TargetIntrinsicType {.symbol = TargetSymbol::Bool, .type_argument_ids = {}},
        .const_qualified = false,
    });
}

const TestSuite suite([] static noexcept {
    "Names: content spelling preserves identity and source separation"_test = [] static noexcept {
        const auto stems = std::array {"CarvenQuery", "CarvenDisplay", "carven_constant"};
        each(
            stems,
            [](const auto& stem) static noexcept { return stem; },
            [](const auto& stem) static noexcept {
                const auto source = source_target_identifier(stem);
                expect(source.spelling() != stem);
                expect(
                    std::format("{}_1234567890123456", source.spelling())
                    != std::format("{}_1234567890123456", stem)
                );
            }
        );
        const auto requests = std::array {
            TargetContentName {.preferred = "Shared", .content = "second"},
            TargetContentName {.preferred = "Shared", .content = "first"},
            TargetContentName {.preferred = "Shared", .content = "first"},
        };
        auto occupied = std::flat_set<std::string> {"Shared"};
        const auto planned = claim_content_identifiers(requests, occupied);
        if (!expect_equal(planned.size(), requests.size())) {
            return;
        }
        expect(planned[0] != planned[1]);
        expect(planned[1] == planned[2]);
        expect(planned[0].spelling() != "Shared");
        expect(planned[1].spelling() != "Shared");
        const auto reversed = std::array {requests[2], requests[1], requests[0]};
        auto reverse_occupied = std::flat_set<std::string> {"Shared"};
        const auto reverse_names = claim_content_identifiers(reversed, reverse_occupied);
        if (!expect_equal(reverse_names.size(), requests.size())) {
            return;
        }
        expect(planned[0] == reverse_names[2]);
        expect(planned[1] == reverse_names[1]);
        expect(planned[2] == reverse_names[0]);
    };

    "Declarations: reads clear attributes while writes retain names"_test = [] static noexcept {
        auto builder = TargetUnitBuilder();
        const auto type = boolean_type(builder);
        const auto absent = builder.add_local(name("absent"));
        const auto parameter = builder.add_local(name("parameter"));
        const auto unused = builder.add_local(name("unused"));
        const auto updated = builder.add_local(name("updated"));
        const auto written = builder.add_local(name("written"));
        const auto returned = builder.add_local(name("returned"));
        auto body = std::vector<TargetStmt>();
        body.push_back(local(returned, type));
        body.push_back(local(written, type));
        body.push_back(local(updated, type));
        body.push_back(local(unused, type));
        body.push_back(assignment(written));
        body.push_back(assignment(parameter));
        body.push_back(target_lowering_statement(
            TargetUpdateStmt {.op = TargetUpdateOperator::Increment, .target = reference(updated)}
        ));
        body.push_back(
            target_lowering_statement(TargetReturnStmt {.expression = reference(returned)})
        );
        const auto parameters = std::array {parameter, absent};
        expect(
            finish_body_declarations(body, parameters, {}, {}) == std::vector<bool> {true, false}
        );
        expect(flags(body) == std::vector<bool> {false, true, true, true});
    };

    "Declarations: sibling declarations and lambda parameters have lexical identities"_test =
        [] static noexcept {
            auto builder = TargetUnitBuilder();
            const auto type = boolean_type(builder);
            const auto outer = builder.add_local(name("outer"));
            const auto shadowed = builder.add_local(name("shadowed"));
            const auto parameter = builder.add_local(name("shadowed"));
            const auto left_local = builder.add_local(name("same"));
            const auto right_local = builder.add_local(name("same"));
            auto body = std::vector<TargetStmt>();
            body.push_back(local(outer, type));
            body.push_back(local(shadowed, type));
            auto lambda_body = std::vector<TargetStmt>();
            lambda_body.push_back(read(outer));
            lambda_body.push_back(read(parameter));
            body.push_back(target_lowering_statement(
                TargetExprStmt {
                    .expression = {
                        .value = TargetLambdaExpr {
                            .parameters = {{.local = parameter, .type = type}},
                            .result = type,
                            .body = std::move(lambda_body)
                        }
                    }
                }
            ));
            auto left = std::vector<TargetStmt>();
            left.push_back(local(left_local, type));
            left.push_back(read(left_local));
            auto right = std::vector<TargetStmt>();
            right.push_back(local(right_local, type));
            auto branches = std::vector<TargetIfBranch>();
            branches.push_back({.condition = literal(), .body = std::move(left)});
            body.push_back(target_lowering_statement(
                TargetIfStmt {.branches = std::move(branches), .else_body = std::move(right)}
            ));
            static_cast<void>(finish_body_declarations(body, {}, {}, {}));
            expect(flags(body) == std::vector<bool> {false, true, false, true});
        };

    "Declarations: loop bindings and steps use their own visibility"_test = [] static noexcept {
        auto builder = TargetUnitBuilder();
        const auto type = boolean_type(builder);
        const auto step = builder.add_local(name("step"));
        const auto body_step = builder.add_local(name("step"));
        const auto index = builder.add_local(name("index"));
        const auto range = builder.add_local(name("range"));
        const auto element = builder.add_local(name("range"));
        auto body = std::vector<TargetStmt>();
        body.push_back(local(step, type));
        auto loop_body = std::vector<TargetStmt>();
        loop_body.push_back(local(body_step, type));
        loop_body.push_back(read(index));
        auto steps = std::vector<TargetForStep>();
        steps.push_back({.value = TargetDiscardStmt {.expression = reference(step)}});
        body.push_back(target_lowering_statement(
            TargetForStmt {
                .initializer =
                    TargetForInitializer {
                        .value =
                            TargetVariableStmt {
                                .binding = TargetVariableBinding::MutableValue,
                                .maybe_unused = true,
                                .local = index,
                                .type = type,
                                .initializer = literal()
                            }
                    },
                .condition = literal(),
                .steps = std::move(steps),
                .body = std::move(loop_body)
            }
        ));
        body.push_back(local(range, type));
        auto iteration = std::vector<TargetStmt>();
        iteration.push_back(read(element));
        body.push_back(target_lowering_statement(
            TargetRangeForStmt {
                .binding = TargetVariableBinding::ConstReference,
                .maybe_unused = true,
                .local = element,
                .type = type,
                .range = reference(range),
                .body = std::move(iteration)
            }
        ));
        static_cast<void>(finish_body_declarations(body, {}, {}, {}));
        expect(flags(body) == std::vector<bool> {false, false, true, false, false});
    };

    "Declarations: exit restructuring preserves skipped storage scopes"_test = [] static noexcept {
        auto builder = TargetUnitBuilder();
        const auto type = boolean_type(builder);
        const auto scoped = builder.add_local(name("scoped"));
        const auto unscoped = builder.add_local(name("unscoped"));
        auto block = std::vector<TargetStmt>();
        block.push_back(local(scoped, type));
        block.push_back(read(scoped));
        auto accepted = std::vector<TargetStmt>();
        accepted.push_back(conditional_jump("scoped_exit"));
        accepted.push_back(
            target_lowering_statement(TargetBlockStmt {.statements = std::move(block)})
        );
        accepted.push_back(exit("scoped_exit"));
        static_cast<void>(finish_body_declarations(accepted, {}, {}, {}));
        require(accepted.size() == 1uz);
        const auto* conditional = std::get_if<TargetIfStmt>(&accepted.front().value);
        require(conditional != nullptr);
        require(conditional->branches.front().body.size() == 1uz);
        expect(
            std::holds_alternative<TargetBlockStmt>(
                conditional->branches.front().body.front().value
            )
        );

        auto rejected = std::vector<TargetStmt>();
        rejected.push_back(conditional_jump("unscoped_exit"));
        rejected.push_back(local(unscoped, type));
        rejected.push_back(exit("unscoped_exit"));
        rejected.push_back(read(unscoped));
        static_cast<void>(finish_body_declarations(rejected, {}, {}, {}));
        require(rejected.size() == 4uz);
        expect(std::holds_alternative<TargetVariableStmt>(rejected[1].value));
        expect(std::holds_alternative<TargetLabelStmt>(rejected[2].value));
        const auto* retained = std::get_if<TargetIfStmt>(&rejected.front().value);
        require(retained != nullptr);
        expect(
            std::holds_alternative<TargetGotoStmt>(retained->branches.front().body.front().value)
        );
    };

    "Declarations: final generated bodies own parameter and local use facts"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn consume(value: i32) {} "
                    "fn forward(parameter: i32) { let local = parameter; return consume(local); } "
                    "fn inactive(parameter: i32) { if false { consume(parameter); } } "
                    "fn only_write(&parameter: i32) { parameter = 2; } "
                    "fn effect() -> i32 => 2; fn unused_local() { let retained = effect(); } "
                    "struct Stop {} fn pair(&first: i32, second: i32) {} "
                    "fn terminal(&parameter: i32) throw Stop { "
                    "return pair(&parameter, if true { throw Stop {}; } else { throw Stop {}; }); } "
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("declarations")}
            );

            struct Query final {
                const TargetUnit* unit = nullptr;
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
                        if (!expect(function->parameters.size() == 1uz)) {
                            return false;
                        }
                        expect(function->parameters.front().local.has_value());
                    } else if (spelling == "inactive" || spelling == "consume") {
                        if (!expect(function->parameters.size() == 1uz)) {
                            return false;
                        }
                        expect(!(function->parameters.front().local.has_value()));
                    }
                    return true;
                }

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        ++locals;
                        expect(
                            variable->maybe_unused
                            == (unit->local_name(variable->local).spelling() != "local")
                        );
                    }
                    return true;
                }
            };

            auto query = Query();
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                query.unit = &unit;
                expect(traverse_target_unit(unit.sections(), query));
            }
            expect(query.definitions == 8uz);
            expect(query.locals == 2uz);
        };
});

} // namespace
