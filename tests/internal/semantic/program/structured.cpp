module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.program.structured;

import :semantic.analysis.analyzer;
import :semantic.analysis.builder;
import :semantic.analysis.validation.invariants;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.place;
import :semantic.hir.stmt;
import :semantic.hir.type;
import :semantic.visibility;
import :source.manager;
import :source.module_path;
import :source.provenance;
import :source.text;
import std;

namespace {

auto fixture_path() noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value("fixture");
    REQUIRE(result.has_value());
    return std::move(*result);
}

auto fixture_provenance() noexcept -> CompilationProvenance {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("fixture.cv", "");
    REQUIRE(source.has_value());
    auto builder = CompilationProvenanceBuilder();
    const auto snapshot = builder.intern_source_snapshot(sources.view(*source));
    static_cast<void>(builder.append_module({
        .source_id = snapshot,
        .path = fixture_path(),
    }));
    return std::move(builder).finish();
}

class SemanticFixture final {
public:
    SemanticFixture() noexcept
        : builder(fixture_provenance()),
          origin(builder.append_origin({
              .source_id = ProgramSourceID::from_index(0),
              .span = Span::from_bounds(0, 0),
              .parent_origin_id = std::nullopt,
          })),
          root_scope(builder.append_scope(std::nullopt)),
          boolean(
              builder.intern_type({.value = HIRBuiltinTypeValue {.kind = HIRBuiltinType::Bool}})
          ),
          integer(
              builder.intern_type({.value = HIRBuiltinTypeValue {.kind = HIRBuiltinType::I32}})
          ),
          module_id(ProgramModuleID::from_index(0)) {
        const auto added_module = builder.append_module({
            .items = {},
        });
        REQUIRE_EQ(added_module, module_id);
    }

    auto expression(
        HIRExprValue value,
        HIRTypeID type,
        std::vector<HIRTypeID> outward_failures = {}
    ) noexcept -> HIRExprID {
        const auto id = builder.append_expression({
            .origin = origin,
            .type = type,
            .constant = std::nullopt,
            .value = std::move(value),
        });
        const auto empty_failure_set = builder.intern_failure_set({});
        const auto outward_failure_set = builder.intern_failure_set(std::move(outward_failures));
        expression_facts.push_back({
            .pending_failure_set = empty_failure_set,
            .outward_failure_set = outward_failure_set,
            .evaluation_failure_set = outward_failure_set,
            .exits_test = false,
            .place_use = std::nullopt,
            .evaluation_effect = {},
            .attempt = std::nullopt,
        });
        return id;
    }

    auto boolean_literal(bool value = true) noexcept -> HIRExprID {
        return expression(
            HIRLiteralExpr {.value = HIRBooleanLiteralValue {.value = value}},
            boolean
        );
    }

    auto block(
        SemanticScopeID scope,
        std::vector<HIRStmtID> statements = {},
        std::optional<HIRExprID> result = std::nullopt
    ) noexcept -> HIRBlockID {
        const auto id = builder.append_block({
            .origin = origin,
            .scope = scope,
            .statements = std::move(statements),
            .result = result,
        });
        block_facts.push_back({
            .outward_failure_set = builder.intern_failure_set({}),
            .exits_test = false,
        });
        return id;
    }

    auto publish_test(HIRBlockID body) noexcept -> TestID {
        const auto test =
            builder.append_test(origin, builder.intern_string("fixture"), root_scope, body);
        builder.hir_module(module_id).items.push_back(test);
        commit_declarations();
        return test;
    }

    auto local_place(
        HIRTypeID type,
        SemanticScopeID scope,
        bool write_eligible,
        std::uint32_t order = 0
    ) noexcept -> SemanticPlaceID {
        const auto symbol = builder.append_symbol({
            .name = builder.intern_string("value"),
            .module_id = std::nullopt,
            .role = SemanticSymbolRole::Local,
            .parent = std::nullopt,
        });
        builder.adopt_symbol_type(symbol, type);
        builder.bind_symbol(symbol, {.scope = scope, .declaration_order = order});
        builder.define_symbol_binding(symbol, SemanticBindingRole::Owner, write_eligible);
        builder.derive_places();
        return *builder.symbol(symbol).place;
    }

    auto top_symbol(std::string_view name, std::optional<SymbolID> parent = std::nullopt) noexcept
        -> SymbolID {
        return builder.append_symbol({
            .name = builder.intern_string(name),
            .module_id = module_id,
            .role = SemanticSymbolRole::Module,
            .parent = parent,
        });
    }

    auto nominal_type(std::string_view name) noexcept -> HIRTypeID {
        const auto symbol = top_symbol(name);
        const auto structure = StructID::from_index(static_cast<std::uint32_t>(structures.size()));
        const auto type = builder.intern_type({
            .value = HIRStructTypeValue {.structure = structure},
        });
        builder.adopt_symbol_type(symbol, type);
        structures.push_back({
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .name = builder.intern_string(name),
            .fields = {},
            .symbol = symbol,
            .supports_equality = true,
        });
        builder.hir_module(module_id).items.push_back(structure);
        return type;
    }

    auto commit_declarations() noexcept -> void {
        if (declarations_committed) {
            return;
        }
        builder.publish_declaration_contracts(
            std::move(functions),
            std::move(structures),
            std::move(enumerations),
            std::move(enum_cases)
        );
        declarations_committed = true;
    }

    auto verify() noexcept -> std::expected<void, SemanticProgramError> {
        return verify_semantic_program(builder);
    }

    auto verify_structure() noexcept -> std::expected<void, SemanticProgramError> {
        return verify_semantic_structure(builder);
    }

    auto commit_control_facts() noexcept -> void {
        builder.publish_control_facts(std::move(expression_facts), std::move(block_facts));
    }

    SemanticConstruction builder;
    ProgramOriginID origin;
    SemanticScopeID root_scope;
    HIRTypeID boolean;
    HIRTypeID integer;
    ProgramModuleID module_id;
    std::vector<HIRFunctionDecl> functions;
    std::vector<HIRStructDecl> structures;
    std::vector<HIREnumDecl> enumerations;
    std::vector<HIREnumCase> enum_cases;
    std::vector<HIRExpressionFacts> expression_facts;
    std::vector<HIRBlockFacts> block_facts;
    bool declarations_committed = false;
};

auto require_error(SemanticFixture& fixture, SemanticProgramErrorKind kind) noexcept -> void {
    fixture.commit_control_facts();
    const auto result = fixture.verify();
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().kind, kind);
}

} // namespace

TEST_CASE("Semantic invariants: every occurrence has exactly one structural owner") {
    auto fixture = SemanticFixture();
    const auto statement = fixture.builder.append_statement({
        .origin = fixture.origin,
        .value = HIRBreakStmt {},
    });
    const auto root = fixture.block(fixture.root_scope, {statement, statement});
    fixture.publish_test(root);
    require_error(fixture, SemanticProgramErrorKind::InvalidOwnership);
}

TEST_CASE("Semantic invariants: TestID and BodyID ownership is one-to-one") {
    auto fixture = SemanticFixture();
    const auto root = fixture.block(fixture.root_scope);
    const auto test = fixture.publish_test(root);
    const auto body = fixture.builder.test(test).body;
    CHECK_EQ(fixture.builder.body(body).root, root);

    fixture.builder.hir_module(fixture.module_id).items.push_back(test);
    require_error(fixture, SemanticProgramErrorKind::InvalidOwnership);
}

TEST_CASE("Semantic structure: every completed body is claimed") {
    auto fixture = SemanticFixture();
    const auto callable =
        fixture.builder.append_callable({}, fixture.integer, {}, HIRFailureContractKind::Inferred);
    const auto root = fixture.block(fixture.root_scope);
    fixture.builder.define_callable_body(callable, {}, fixture.root_scope, root);
    fixture.commit_declarations();

    const auto result = fixture.verify_structure();
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().kind, SemanticProgramErrorKind::InvalidOwnership);
}

TEST_CASE("Semantic structure: body root scopes are unique") {
    auto fixture = SemanticFixture();
    const auto first_root = fixture.block(fixture.root_scope);
    const auto second_root = fixture.block(fixture.root_scope);
    const auto first = fixture.builder.append_test(
        fixture.origin,
        fixture.builder.intern_string("first"),
        fixture.root_scope,
        first_root
    );
    const auto second = fixture.builder.append_test(
        fixture.origin,
        fixture.builder.intern_string("second"),
        fixture.root_scope,
        second_root
    );
    fixture.builder.hir_module(fixture.module_id).items.push_back(first);
    fixture.builder.hir_module(fixture.module_id).items.push_back(second);
    fixture.commit_declarations();

    const auto result = fixture.verify_structure();
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().kind, SemanticProgramErrorKind::InvalidOwnership);
}

TEST_CASE("Semantic structure: closure root is a strict descendant of its enclosing scope") {
    auto fixture = SemanticFixture();
    const auto sibling = fixture.builder.append_scope(std::nullopt);
    const auto closure_root = fixture.block(sibling);
    const auto callable =
        fixture.builder.append_callable({}, fixture.integer, {}, HIRFailureContractKind::Inferred);
    fixture.builder.define_callable_body(callable, {}, sibling, closure_root);
    const auto closure = fixture.expression(
        HIRClosureExpr {
            .captures = {},
            .result = fixture.integer,
            .callable = callable,
        },
        fixture.integer
    );
    const auto outer_root = fixture.block(fixture.root_scope, {}, closure);
    fixture.publish_test(outer_root);

    const auto result = fixture.verify_structure();
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().kind, SemanticProgramErrorKind::InvalidScope);
}

TEST_CASE("Semantic invariants: expression cycles are rejected") {
    auto fixture = SemanticFixture();
    const auto expression = fixture.expression(
        HIRUnaryExpr {
            .op = HIRUnaryExpr::Operator::LogicalNot,
            .operand_id = HIRExprID::from_index(0),
        },
        fixture.boolean
    );
    const auto root = fixture.block(fixture.root_scope, {}, expression);
    fixture.publish_test(root);
    require_error(fixture, SemanticProgramErrorKind::InvalidOwnership);
}

TEST_CASE("Semantic invariants: invalid child identities are diagnosed without indexing them") {
    auto fixture = SemanticFixture();
    const auto expression = fixture.expression(
        HIRUnaryExpr {
            .op = HIRUnaryExpr::Operator::LogicalNot,
            .operand_id = HIRExprID::from_index(7),
        },
        fixture.boolean
    );
    const auto root = fixture.block(fixture.root_scope, {}, expression);
    fixture.publish_test(root);
    require_error(fixture, SemanticProgramErrorKind::InvalidReference);
}

TEST_CASE("Semantic invariants: expression forms agree with their resolved type") {
    auto fixture = SemanticFixture();
    const auto expression = fixture.expression(HIRArrayExpr {.element_ids = {}}, fixture.boolean);
    const auto root = fixture.block(fixture.root_scope, {}, expression);
    fixture.publish_test(root);
    require_error(fixture, SemanticProgramErrorKind::InvalidType);
}

TEST_CASE("Semantic invariants: structured control cannot enter a sibling scope") {
    auto fixture = SemanticFixture();
    const auto sibling = fixture.builder.append_scope(std::nullopt);
    const auto branch = fixture.block(sibling);
    const auto conditional = fixture.builder.append_statement({
        .origin = fixture.origin,
        .value = HIRIfStmt {
            .branches = {{.condition = fixture.boolean_literal(), .body = branch}},
            .else_branch = std::nullopt,
        },
    });
    const auto root = fixture.block(fixture.root_scope, {conditional});
    fixture.publish_test(root);
    require_error(fixture, SemanticProgramErrorKind::InvalidScope);
}

TEST_CASE("Semantic invariants: place projections preserve resolved types") {
    auto fixture = SemanticFixture();
    const auto place = fixture.local_place(fixture.boolean, fixture.root_scope, true);
    const auto expression = fixture.boolean_literal();
    fixture.expression_facts[expression.index()].place_use = SemanticPlaceUse {
        .root = place,
        .projections = {SemanticIndexProjection {.result_type = fixture.boolean}},
        .access = SemanticPlaceAccess::Read,
    };
    const auto root = fixture.block(fixture.root_scope, {}, expression);
    fixture.publish_test(root);
    require_error(fixture, SemanticProgramErrorKind::InvalidPlace);
}

TEST_CASE("Semantic invariants: callable contracts only reference published types") {
    auto fixture = SemanticFixture();
    static_cast<void>(
        fixture.builder
            .append_callable({}, HIRTypeID::from_index(99), {}, HIRFailureContractKind::Inferred)
    );
    const auto root = fixture.block(fixture.root_scope);
    fixture.publish_test(root);
    require_error(fixture, SemanticProgramErrorKind::InvalidContract);
}

TEST_CASE("Semantic invariants: every closed function contract has one body") {
    auto fixture = SemanticFixture();
    const auto symbol = fixture.top_symbol("missing_body");
    const auto function = FunctionID::from_index(0);
    const auto callable =
        fixture.builder.append_callable({}, fixture.integer, {}, HIRFailureContractKind::Inferred);
    fixture.builder.adopt_symbol_type(symbol, fixture.builder.intern_function_type(callable));
    fixture.functions.push_back({
        .origin = fixture.origin,
        .visibility = DeclarationVisibility::Module,
        .name = fixture.builder.intern_string("missing_body"),
        .callable = callable,
        .result = fixture.integer,
        .result_origin = fixture.origin,
        .symbol = symbol,
        .entry_point = std::nullopt,
    });
    fixture.builder.hir_module(fixture.module_id).items.push_back(function);
    fixture.commit_declarations();
    require_error(fixture, SemanticProgramErrorKind::InvalidContract);
}

TEST_CASE("Semantic invariants: an enum case has exactly one enum owner") {
    auto fixture = SemanticFixture();
    const auto first_symbol = fixture.top_symbol("First");
    const auto second_symbol = fixture.top_symbol("Second");
    const auto first = EnumID::from_index(0);
    const auto second = EnumID::from_index(1);
    const auto case_symbol = fixture.top_symbol("Value", first_symbol);
    const auto enum_case = EnumCaseID::from_index(0);
    const auto first_type = fixture.builder.intern_type({
        .value = HIREnumTypeValue {.enumeration = first},
    });
    const auto second_type = fixture.builder.intern_type({
        .value = HIREnumTypeValue {.enumeration = second},
    });
    fixture.builder.adopt_symbol_type(first_symbol, first_type);
    fixture.builder.adopt_symbol_type(second_symbol, second_type);
    fixture.builder.adopt_symbol_type(case_symbol, first_type);
    fixture.enum_cases.push_back({
        .owner = first,
        .name = fixture.builder.intern_string("Value"),
        .payload_types = {},
        .constant = std::nullopt,
        .symbol = case_symbol,
        .origin = fixture.origin,
    });
    fixture.enumerations.push_back({
        .origin = fixture.origin,
        .visibility = DeclarationVisibility::Module,
        .name = fixture.builder.intern_string("First"),
        .underlying_type = fixture.integer,
        .cases = {enum_case},
        .profile = HIREnumProfile::Numeric,
        .symbol = first_symbol,
        .supports_equality = true,
    });
    fixture.enumerations.push_back({
        .origin = fixture.origin,
        .visibility = DeclarationVisibility::Module,
        .name = fixture.builder.intern_string("Second"),
        .underlying_type = fixture.integer,
        .cases = {enum_case},
        .profile = HIREnumProfile::Numeric,
        .symbol = second_symbol,
        .supports_equality = true,
    });
    fixture.builder.hir_module(fixture.module_id).items.push_back(first);
    fixture.builder.hir_module(fixture.module_id).items.push_back(second);
    fixture.commit_declarations();
    require_error(fixture, SemanticProgramErrorKind::InvalidOwnership);
}

TEST_CASE("Semantic invariants: pattern bindings agree with their subject type") {
    auto fixture = SemanticFixture();
    const auto arm_scope = fixture.builder.append_scope(fixture.root_scope);
    const auto place = fixture.local_place(fixture.integer, arm_scope, false);
    const auto symbol = *fixture.builder.place(place).symbol;
    const auto pattern = fixture.builder.append_pattern({
        .origin = fixture.origin,
        .value = HIRBindingPattern {
            .target = HIRNamedBindingTarget {.symbol = symbol},
            .type = fixture.integer,
        },
    });
    const auto arm_body = fixture.block(arm_scope);
    const auto match = fixture.builder.append_statement({
        .origin = fixture.origin,
        .value = HIRMatchStmt {
            .subject = fixture.boolean_literal(),
            .arms = {{
                .scope = arm_scope,
                .pattern = pattern,
                .guard = std::nullopt,
                .body = arm_body,
            }},
            .exhaustive = true,
        },
    });
    const auto root = fixture.block(fixture.root_scope, {match});
    fixture.publish_test(root);
    require_error(fixture, SemanticProgramErrorKind::InvalidType);
}

TEST_CASE("Semantic invariants: failure sets contain only normalized nominal members") {
    auto fixture = SemanticFixture();
    const auto expression = fixture.expression(
        HIRLiteralExpr {.value = HIRBooleanLiteralValue {.value = true}},
        fixture.boolean,
        {fixture.boolean}
    );
    const auto root = fixture.block(fixture.root_scope, {}, expression);
    fixture.publish_test(root);
    require_error(fixture, SemanticProgramErrorKind::InvalidContract);
}

TEST_CASE("Semantic invariants: evaluation effects reference canonical place sets") {
    SUBCASE("duplicate place") {
        auto fixture = SemanticFixture();
        const auto place = fixture.local_place(fixture.boolean, fixture.root_scope, true);
        const auto expression = fixture.boolean_literal();
        fixture.expression_facts[expression.index()].evaluation_effect.reads = {place, place};
        const auto root = fixture.block(fixture.root_scope, {}, expression);
        fixture.publish_test(root);
        require_error(fixture, SemanticProgramErrorKind::InvalidContract);
    }

    SUBCASE("unknown place") {
        auto fixture = SemanticFixture();
        const auto expression = fixture.boolean_literal();
        fixture.expression_facts[expression.index()].evaluation_effect.takes = {
            SemanticPlaceID::from_index(99),
        };
        const auto root = fixture.block(fixture.root_scope, {}, expression);
        fixture.publish_test(root);
        require_error(fixture, SemanticProgramErrorKind::InvalidPlace);
    }
}

TEST_CASE("Semantic invariants: try only publishes failures unhandled by its protected body") {
    auto fixture = SemanticFixture();
    const auto protected_failure = fixture.nominal_type("ProtectedFailure");
    const auto unrelated_failure = fixture.nominal_type("UnrelatedFailure");
    const auto protected_scope = fixture.builder.append_scope(fixture.root_scope);
    const auto protected_body = fixture.block(protected_scope);
    fixture.block_facts[protected_body.index()].outward_failure_set =
        fixture.builder.intern_failure_set({protected_failure});
    const auto attempt = fixture.expression(
        HIRTryExpr {
            .body = protected_body,
            .arms = {},
        },
        fixture.boolean,
        {unrelated_failure}
    );
    fixture.expression_facts[attempt.index()].attempt = HIRTryFacts {
        .arms = {},
        .unhandled_failure_set = fixture.builder.intern_failure_set({unrelated_failure}),
    };
    const auto root = fixture.block(fixture.root_scope, {}, attempt);
    fixture.publish_test(root);
    require_error(fixture, SemanticProgramErrorKind::InvalidContract);
}
