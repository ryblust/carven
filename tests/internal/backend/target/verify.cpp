module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.target.verify;

import :artifacts;
import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.raw;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :backend.target.verify;
import std;

namespace {

auto identifier(std::string_view spelling) noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling(spelling);
}

auto source_attribution(TargetAttributionKind kind = TargetAttributionKind::SourceOwned) noexcept
    -> TargetAttribution {
    return {
        .kind = kind,
        .origin = TargetSourceOrigin {.display_origin = "fixture.cv", .line = 1},
        .reason = std::nullopt,
    };
}

auto synthetic_attribution() noexcept -> TargetAttribution {
    return {
        .kind = TargetAttributionKind::SourceExpansion,
        .origin = std::nullopt,
        .reason = TargetSyntheticReason::ControlNormalization,
    };
}

auto test_root(std::vector<TargetItemID> items = {}) noexcept -> TargetUnitRoot {
    return {
        .logical_path = "fixture.cpp",
        .role = GeneratedArtifactRole::TestEntry,
        .source_mapping = ArtifactSourceMappingPolicy::SourceAttributed,
        .directive_groups =
            {{.directives = {{.bytes = "#include <carven/std/testing/testing.hpp>"}}}},
        .sections = {.preamble = {}, .body = std::move(items), .epilogue = {}},
    };
}

class TargetUnitFixture final {
public:
    auto append_type(TargetType value) noexcept -> TargetTypeID {
        const auto id = TargetTypeID::from_index(static_cast<std::uint32_t>(types.size()));
        types.push_back(std::move(value));
        return id;
    }

    auto append_intrinsic_type(TargetSymbol symbol = TargetSymbol::Int) noexcept -> TargetTypeID {
        return append_type({
            .value = TargetIntrinsicType {.symbol = symbol, .type_argument_ids = {}},
            .const_qualified = false,
        });
    }

    auto append_expression(TargetExprValue value) noexcept -> TargetExprID {
        const auto id = TargetExprID::from_index(static_cast<std::uint32_t>(expressions.size()));
        expressions.push_back({.value = std::move(value)});
        return id;
    }

    auto append_boolean(bool value = true) noexcept -> TargetExprID {
        return append_expression(TargetLiteralExpr {.value = value});
    }

    auto append_statement(TargetStmtValue value, TargetAttribution attribution) noexcept
        -> TargetStmtID {
        const auto id = TargetStmtID::from_index(static_cast<std::uint32_t>(statements.size()));
        statements.push_back({
            .value = std::move(value),
            .attribution = std::move(attribution),
        });
        return id;
    }

    auto append_item(TargetItemValue value, TargetAttribution attribution) noexcept
        -> TargetItemID {
        const auto id = TargetItemID::from_index(static_cast<std::uint32_t>(items.size()));
        items.push_back({
            .value = std::move(value),
            .attribution = std::move(attribution),
        });
        return id;
    }

    auto append_function(TargetTypeID result, std::vector<TargetStmtID> body) noexcept
        -> TargetItemID {
        return append_item(
            TargetDecl {TargetFunctionDecl {
                .name = TargetName(identifier("fixture")),
                .parameters = {},
                .result = result,
                .body = std::move(body),
                .declaration_only = false,
                .inline_specifier = false,
            }},
            source_attribution()
        );
    }

    auto append_variable(TargetTypeID type, TargetExprID initializer) noexcept -> TargetItemID {
        return append_item(
            TargetDecl {TargetVariableDecl {
                .type = type,
                .name = TargetName(identifier("value")),
                .initializer = initializer,
                .inline_specifier = false,
                .constexpr_specifier = false,
            }},
            source_attribution()
        );
    }

    auto append_raw_item(
        TargetAttribution attribution = source_attribution(TargetAttributionKind::RawSource)
    ) noexcept -> TargetItemID {
        return append_item(
            TargetRawFragment {.bytes = "static_assert(true);"},
            std::move(attribution)
        );
    }

    auto publish(std::vector<TargetItemID> root_items) noexcept -> void {
        root = test_root(std::move(root_items));
    }

    auto verify() const noexcept -> std::expected<void, TargetUnitViolation> {
        return validate_target_unit({
            .types = types,
            .expressions = expressions,
            .statements = statements,
            .items = items,
            .root = root,
        });
    }

    std::vector<TargetType> types;
    std::vector<TargetExpr> expressions;
    std::vector<TargetStmt> statements;
    std::vector<TargetItem> items;
    TargetUnitRoot root = test_root();
};

auto require_violation(const TargetUnitFixture& fixture, TargetUnitViolationKind kind) noexcept
    -> void {
    const auto result = fixture.verify();
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().kind, kind);
}

} // namespace

TEST_CASE("Target unit validation: accepts a complete uniquely owned graph") {
    auto fixture = TargetUnitFixture();
    const auto result_type = fixture.append_intrinsic_type();
    const auto result = fixture.append_boolean();
    const auto returned =
        fixture.append_statement(TargetReturnStmt {.expression = result}, source_attribution());
    fixture.publish({fixture.append_function(result_type, {returned})});

    CHECK(fixture.verify().has_value());
}

TEST_CASE("Target unit validation: rejects inconsistent artifact metadata") {
    auto fixture = TargetUnitFixture();
    fixture.root.role = GeneratedArtifactRole::Interface;
    require_violation(fixture, TargetUnitViolationKind::InvalidStructure);

    fixture.root.role = GeneratedArtifactRole::TestEntry;
    fixture.root.directive_groups.front().directives.front().bytes = "runtime include";
    require_violation(fixture, TargetUnitViolationKind::InvalidStructure);

    fixture.root.directive_groups.front().directives.front().bytes = "#pragma once";
    require_violation(fixture, TargetUnitViolationKind::InvalidStructure);
}

TEST_CASE("Target unit builder: clones produce independent expression occurrences") {
    auto builder = TargetUnitBuilder();
    const auto type = builder.intern_type({
        .value =
            TargetIntrinsicType {
                .symbol = TargetSymbol::Bool,
                .type_argument_ids = {},
            },
        .const_qualified = false,
    });
    const auto prototype = builder.append_expression({
        .value = TargetLiteralExpr {.value = true},
    });
    const auto clone = builder.clone_expression_occurrence(prototype);
    const auto equality = builder.append_expression({
        .value = TargetBinaryExpr {
            .left = prototype,
            .op = TargetBinaryOperator::Equal,
            .right = clone,
        },
    });
    const auto item = builder.append_item({
        .value = TargetDecl {TargetVariableDecl {
            .type = type,
            .name = TargetName(identifier("value")),
            .initializer = equality,
            .inline_specifier = false,
            .constexpr_specifier = false,
        }},
        .attribution = source_attribution(),
    });
    const auto unit = std::move(builder).finish(test_root({item}));

    CHECK_NE(prototype.index(), clone.index());
    CHECK_EQ(unit.expressions().size(), 3);
}

TEST_CASE("Target unit builder: composite clones split occurrences and share types") {
    auto builder = TargetUnitBuilder();
    const auto type = builder.intern_type({
        .value =
            TargetIntrinsicType {
                .symbol = TargetSymbol::Bool,
                .type_argument_ids = {},
            },
        .const_qualified = false,
    });
    const auto original_leaf = builder.append_expression({
        .value = TargetLiteralExpr {.value = true},
    });
    const auto original_initializer = builder.append_expression({
        .value = TargetStaticCastExpr {
            .type = type,
            .operand_id = original_leaf,
        },
    });
    const auto original_statement = builder.append_statement({
        .value =
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .name = identifier("nested"),
                .type = type,
                .initializer = original_initializer,
                .maybe_unused = false,
            },
        .attribution = source_attribution(),
    });
    const auto prototype = builder.append_expression({
        .value = TargetLambdaExpr {
            .reason = TargetIIFEReason::ConditionalExpression,
            .body = {original_statement},
        },
    });
    const auto clone = builder.clone_expression_occurrence(prototype);
    const auto first = builder.append_item({
        .value = TargetDecl {TargetVariableDecl {
            .type = type,
            .name = TargetName(identifier("first")),
            .initializer = prototype,
            .inline_specifier = false,
            .constexpr_specifier = false,
        }},
        .attribution = source_attribution(),
    });
    const auto second = builder.append_item({
        .value = TargetDecl {TargetVariableDecl {
            .type = type,
            .name = TargetName(identifier("second")),
            .initializer = clone,
            .inline_specifier = false,
            .constexpr_specifier = false,
        }},
        .attribution = source_attribution(),
    });

    const auto unit = std::move(builder).finish(test_root({first, second}));

    CHECK_NE(prototype.index(), clone.index());
    const auto& original_lambda = std::get<TargetLambdaExpr>(unit.expression(prototype).value);
    const auto& cloned_lambda = std::get<TargetLambdaExpr>(unit.expression(clone).value);
    REQUIRE_EQ(original_lambda.body.size(), 1);
    REQUIRE_EQ(cloned_lambda.body.size(), 1);
    const auto cloned_statement = cloned_lambda.body.front();
    CHECK_NE(original_statement.index(), cloned_statement.index());

    const auto& original_variable =
        std::get<TargetVariableStmt>(unit.statement(original_statement).value);
    const auto& cloned_variable =
        std::get<TargetVariableStmt>(unit.statement(cloned_statement).value);
    CHECK_EQ(original_variable.type, type);
    CHECK_EQ(cloned_variable.type, type);
    CHECK_NE(original_variable.initializer.index(), cloned_variable.initializer.index());

    const auto& original_cast =
        std::get<TargetStaticCastExpr>(unit.expression(original_variable.initializer).value);
    const auto& cloned_cast =
        std::get<TargetStaticCastExpr>(unit.expression(cloned_variable.initializer).value);
    CHECK_EQ(original_cast.type, type);
    CHECK_EQ(cloned_cast.type, type);
    CHECK_NE(original_cast.operand_id.index(), cloned_cast.operand_id.index());
    CHECK_EQ(unit.types().size(), 1);

    const auto& original_attribution = unit.statement(original_statement).attribution;
    const auto& cloned_attribution = unit.statement(cloned_statement).attribution;
    CHECK_EQ(original_attribution.kind, cloned_attribution.kind);
    REQUIRE(original_attribution.origin.has_value());
    REQUIRE(cloned_attribution.origin.has_value());
    CHECK_EQ(
        original_attribution.origin->display_origin,
        cloned_attribution.origin->display_origin
    );
    CHECK_EQ(original_attribution.origin->line, cloned_attribution.origin->line);
    CHECK_EQ(original_attribution.reason, cloned_attribution.reason);
    CHECK_EQ(unit.items().size(), 2);
}

TEST_CASE("Target unit validation: root item references are range checked") {
    auto fixture = TargetUnitFixture();
    fixture.publish({TargetItemID::from_index(0)});

    require_violation(fixture, TargetUnitViolationKind::InvalidReference);
}

TEST_CASE("Target unit validation: item occurrences have one owner") {
    auto fixture = TargetUnitFixture();
    const auto item = fixture.append_raw_item();
    fixture.publish({item, item});

    require_violation(fixture, TargetUnitViolationKind::InvalidOwnership);
}

TEST_CASE("Target unit validation: statement occurrences have one owner") {
    auto fixture = TargetUnitFixture();
    const auto result_type = fixture.append_intrinsic_type();
    const auto statement = fixture.append_statement(
        TargetExprStmt {.expression = fixture.append_boolean()},
        source_attribution()
    );
    fixture.publish({fixture.append_function(result_type, {statement, statement})});

    require_violation(fixture, TargetUnitViolationKind::InvalidOwnership);
}

TEST_CASE("Target unit validation: every expression ID is one syntax occurrence") {
    auto fixture = TargetUnitFixture();
    const auto type = fixture.append_intrinsic_type();
    const auto operand = fixture.append_boolean();
    const auto expression = fixture.append_expression(
        TargetBinaryExpr {
            .left = operand,
            .op = TargetBinaryOperator::Equal,
            .right = operand,
        }
    );
    fixture.publish({fixture.append_variable(type, expression)});

    require_violation(fixture, TargetUnitViolationKind::InvalidOwnership);
}

TEST_CASE("Target unit validation: occurrence graphs are acyclic") {
    auto fixture = TargetUnitFixture();
    const auto type = fixture.append_intrinsic_type();
    const auto expression = fixture.append_expression(
        TargetPrefixExpr {
            .op = TargetPrefixOperator::LogicalNot,
            .operand_id = TargetExprID::from_index(0),
        }
    );
    fixture.publish({fixture.append_variable(type, expression)});

    require_violation(fixture, TargetUnitViolationKind::InvalidCycle);
}

TEST_CASE("Target unit validation: statement graphs are acyclic") {
    auto fixture = TargetUnitFixture();
    const auto result_type = fixture.append_intrinsic_type();
    const auto block = fixture.append_statement(
        TargetBlockStmt {
            .statements = {TargetStmtID::from_index(0)},
            .scoped = true,
        },
        source_attribution()
    );
    fixture.publish({fixture.append_function(result_type, {block})});

    require_violation(fixture, TargetUnitViolationKind::InvalidCycle);
}

TEST_CASE("Target unit validation: expression references are range checked") {
    auto fixture = TargetUnitFixture();
    const auto type = fixture.append_intrinsic_type();
    const auto expression = fixture.append_expression(
        TargetPrefixExpr {
            .op = TargetPrefixOperator::LogicalNot,
            .operand_id = TargetExprID::from_index(7),
        }
    );
    fixture.publish({fixture.append_variable(type, expression)});

    require_violation(fixture, TargetUnitViolationKind::InvalidReference);
}

TEST_CASE("Target unit validation: type references are range checked") {
    auto fixture = TargetUnitFixture();
    const auto array_type = fixture.append_type({
        .value =
            TargetArrayType {
                .element_type_id = TargetTypeID::from_index(7),
                .extent = TargetArrayExtent {.magnitude = 17},
            },
        .const_qualified = false,
    });
    const auto initializer = fixture.append_boolean();
    fixture.publish({fixture.append_variable(array_type, initializer)});

    require_violation(fixture, TargetUnitViolationKind::InvalidReference);
}

TEST_CASE("Target unit validation: shared array types own value extents") {
    auto fixture = TargetUnitFixture();
    const auto array_type = fixture.append_type({
        .value =
            TargetArrayType {
                .element_type_id = TargetTypeID::from_index(1),
                .extent = TargetArrayExtent {.magnitude = 17},
            },
        .const_qualified = false,
    });
    static_cast<void>(fixture.append_intrinsic_type());
    const auto first = fixture.append_boolean();
    const auto second = fixture.append_boolean();
    fixture.publish({
        fixture.append_variable(array_type, first),
        fixture.append_variable(array_type, second),
    });

    CHECK(fixture.verify().has_value());
}

TEST_CASE("Target unit validation: source-owned nodes require an origin") {
    auto fixture = TargetUnitFixture();
    const auto item = fixture.append_raw_item({
        .kind = TargetAttributionKind::SourceOwned,
        .origin = std::nullopt,
        .reason = std::nullopt,
    });
    fixture.publish({item});

    require_violation(fixture, TargetUnitViolationKind::InvalidAttribution);
}

TEST_CASE("Target unit validation: typed for headers own expression occurrences") {
    auto fixture = TargetUnitFixture();
    const auto result_type = fixture.append_intrinsic_type();
    const auto shared = fixture.append_boolean();
    const auto loop = fixture.append_statement(
        TargetForStmt {
            .initializer =
                TargetForInitializer {
                    .value = TargetExprStmt {.expression = shared},
                },
            .condition = std::nullopt,
            .steps = {TargetForStep {
                .value = TargetExprStmt {.expression = shared},
            }},
            .body = {},
        },
        source_attribution()
    );
    fixture.publish({fixture.append_function(result_type, {loop})});

    require_violation(fixture, TargetUnitViolationKind::InvalidOwnership);
}

TEST_CASE("Target unit validation: every arena node is root reachable") {
    auto fixture = TargetUnitFixture();
    static_cast<void>(fixture.append_boolean());
    fixture.publish({fixture.append_raw_item()});

    require_violation(fixture, TargetUnitViolationKind::OrphanNode);
}

TEST_CASE("Target unit validation: hidden function bodies and conflicting forms are rejected") {
    struct MalformedMemberFunction final {
        bool declaration_only;
        bool defaulted;
        bool has_body;
    };
    const auto cases = std::array {
        MalformedMemberFunction {
            .declaration_only = true,
            .defaulted = true,
            .has_body = false,
        },
        MalformedMemberFunction {
            .declaration_only = true,
            .defaulted = false,
            .has_body = true,
        },
        MalformedMemberFunction {
            .declaration_only = false,
            .defaulted = true,
            .has_body = true,
        },
    };
    for (const auto malformed : cases) {
        auto fixture = TargetUnitFixture();
        const auto result_type = fixture.append_intrinsic_type();
        auto body = std::vector<TargetStmtID>();
        if (malformed.has_body) {
            body.push_back(fixture.append_statement(
                TargetReturnStmt {.expression = fixture.append_boolean()},
                source_attribution()
            ));
        }
        const auto structure = fixture.append_item(
            TargetDecl {TargetStructDecl {
                .name = identifier("Malformed"),
                .members = {TargetMemberFunctionDecl {
                    .name = identifier("member"),
                    .parameters = {},
                    .result = result_type,
                    .body = std::move(body),
                    .static_specifier = false,
                    .constexpr_specifier = false,
                    .friend_specifier = false,
                    .declaration_only = malformed.declaration_only,
                    .defaulted = malformed.defaulted,
                    .result_reference = false,
                    .const_qualified = false,
                }},
            }},
            source_attribution()
        );
        fixture.publish({structure});

        require_violation(fixture, TargetUnitViolationKind::InvalidStructure);
    }
}

TEST_CASE("Target unit validation: target jump checks remain active") {
    auto fixture = TargetUnitFixture();
    const auto result_type = fixture.append_intrinsic_type();
    const auto jump = fixture.append_statement(
        TargetGotoStmt {
            .label = identifier("missing"),
            .role = TargetJumpRole::FailureTransfer,
        },
        synthetic_attribution()
    );
    fixture.publish({fixture.append_function(result_type, {jump})});

    require_violation(fixture, TargetUnitViolationKind::InvalidControl);
}
