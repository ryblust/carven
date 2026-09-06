module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.body;

import :compiler.request;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.resolve;
import :semantic.analysis.ownership;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.harness.death;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
    return std::move(*result);
}

struct PreparedFunction final {
    ProgramDraft builder;
    CallableID callable;
    ModuleID module_id;
    ProgramOriginID origin;
    ProgramSpellingID parameter_name;
    TypeID boolean_type;
};

auto prepare_function(SourceManager& sources, DiagnosticSink& diagnostics) noexcept
    -> PreparedFunction {
    const auto source = sources.append_virtual("semir-function-body.cv", "");
    REQUIRE(source.has_value());
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = *source,
            .module_path = path("semir.function_body"),
        },
    };
    auto syntax = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(syntax.has_value());
    auto builder = ProgramDraft::begin(std::move(*syntax), diagnostics);
    const auto provenance_module = builder.provenance_module_at(0uz);
    const auto source_id = builder.module_source(provenance_module);
    const auto origin = builder.append_source_origin(source_id, Span::at(0u));
    const auto parameter_name = builder.intern_spelling("value");
    const auto boolean = builder.intern_builtin_type(BuiltinType::Bool);
    const auto module_id = builder.reserve_module_declaration();
    const auto function = builder.reserve_function_declaration();
    const auto callable = builder.reserve_callable_declaration();
    const auto failures = builder.add_empty_failure_term();
    builder.define_callable_contract(
        callable,
        ConstructionCallableContract {
            .parameters =
                {
                    ConstructionCallableParameter {
                        .access = AccessMode::Read,
                        .type = boolean,
                    },
                },
            .result = boolean,
            .failures = failures,
            .policy = FailureContractPolicy::Declared,
        }
    );
    builder.define_declaration(
        function,
        FunctionDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("identity"),
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .callable = callable,
            .entry_point = std::nullopt,
            .cpp_export_origin = std::nullopt,
        }
    );
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = provenance_module,
            .origin = origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {ModuleItem {function}},
        }
    );
    builder.finish_declarations();
    return PreparedFunction {
        .builder = std::move(builder),
        .callable = callable,
        .module_id = module_id,
        .origin = origin,
        .parameter_name = parameter_name,
        .boolean_type = boolean,
    };
}

struct BodyFixture final {
    BodyBuilder builder;
    ScopeID scope;
    LifetimeRegionID lifetime;
    LocalBindingID parameter;
    FailureTermID failures;
};

auto body_fixture(PreparedFunction& prepared) noexcept -> BodyFixture {
    auto reservation = prepared.builder.reserve_body(BodyKind::Function);
    prepared.builder.complete_callable(
        prepared.callable,
        FunctionBodyImplementation {.body = reservation.id()}
    );
    auto body = BodyBuilder(std::move(reservation), prepared.builder);
    const auto scope = body.add_scope(std::nullopt, prepared.origin);
    const auto lifetime =
        body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, prepared.origin);
    const auto parameter = body.add_parameter(
        prepared.parameter_name,
        prepared.boolean_type,
        scope,
        lifetime,
        AccessMode::Read,
        prepared.origin
    );
    return BodyFixture {
        .builder = std::move(body),
        .scope = scope,
        .lifetime = lifetime,
        .parameter = parameter.binding,
        .failures = prepared.builder.add_empty_failure_term()
    };
}

auto boolean_expression(PreparedFunction& prepared, const BodyFixture& body) noexcept
    -> DraftExpression {
    return DraftExpression {
        .type = prepared.boolean_type,
        .lifetime = body.lifetime,
        .origin = prepared.origin,
        .constant = std::nullopt,
        .failures = body.failures,
        .exits_test = false,
        .category = SemanticValueCategory::Value,
        .value = SemLiteral {.value = BooleanLiteral {.value = true}},
    };
}

auto finish_body(
    PreparedFunction& prepared,
    BodyFixture&& body,
    std::vector<DraftStatement> statements,
    std::optional<DraftExpression> result
) noexcept -> SemIRBody {
    auto draft = std::move(body.builder)
                     .finish(
                         DraftRegion {
                             .scope = body.scope,
                             .lifetime = body.lifetime,
                             .origin = prepared.origin,
                             .statements = std::move(statements),
                             .result = std::move(result),
                             .failures = body.failures,
                             .exits_test = false,
                         }
                     );
    REQUIRE(prepared.builder.solve_construction().has_value());
    return resolve_body(std::move(draft), prepared.builder);
}

template<typename MakeExpression>
auto rejects_expression(std::string_view scenario, MakeExpression make_expression) noexcept
    -> bool {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto prepared = prepare_function(sources, diagnostics);
    auto body = body_fixture(prepared);
    auto invalid = std::invoke(make_expression, prepared, body);
    auto statements = std::vector<DraftStatement>();
    statements.push_back(
        DraftStatement {
            .origin = prepared.origin,
            .lifetime = body.lifetime,
            .value = SemExpressionStatement<ConstructionTypeRef, FailureTermID> {
                .expression = std::move(invalid)
            }
        }
    );
    auto result = boolean_expression(prepared, body);
    auto resolved =
        finish_body(prepared, std::move(body), std::move(statements), std::move(result));
    return expect_termination(scenario, [&] noexcept {
        static_cast<void>(analyze_body_batch({&resolved, 1uz}, prepared.builder));
    });
}

} // namespace

TEST_CASE("SemIR body: publication preserves structured parameters and result") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto prepared = prepare_function(sources, diagnostics);
    auto body = body_fixture(prepared);
    const auto parameter = body.parameter;
    auto result = boolean_expression(prepared, body);
    result.category = SemanticValueCategory::Place;
    result.value = SemBinding {.binding = parameter};
    auto resolved = finish_body(prepared, std::move(body), {}, std::move(result));
    const auto body_id = resolved.id();
    REQUIRE(analyze_body_batch({&resolved, 1uz}, prepared.builder).has_value());
    auto bodies = std::vector<SemIRBody>();
    bodies.push_back(std::move(resolved));
    prepared.builder.publish_bodies(std::move(bodies));
    const auto program = std::move(prepared.builder).seal();
    const auto& published = program.bodies().body(body_id);
    CHECK_EQ(published.inputs().parameters, std::vector {parameter});
    REQUIRE(published.region().result.has_value());
    const auto* binding = std::get_if<SemBinding>(&published.region().result->value);
    REQUIRE(binding != nullptr);
    CHECK_EQ(binding->binding, parameter);
    CHECK_EQ(program.body_for_callable(prepared.callable), body_id);
    CHECK(diagnostics.empty());
}

TEST_CASE("SemIR body invariant: result agrees with callable contract") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto prepared = prepare_function(sources, diagnostics);
    auto body = body_fixture(prepared);
    auto resolved = finish_body(prepared, std::move(body), {}, std::nullopt);
    CHECK(expect_termination("structured-result-contract", [&] noexcept {
        static_cast<void>(analyze_body_batch({&resolved, 1uz}, prepared.builder));
    }));
}

TEST_CASE("SemIR body invariant: unary binary and cast operations retain type contracts") {
    SUBCASE("unary") {
        CHECK(rejects_expression(
            "structured-unary-contract",
            [](PreparedFunction& prepared, BodyFixture& body) static noexcept {
                auto operand = boolean_expression(prepared, body);
                auto result = boolean_expression(prepared, body);
                result.value = SemUnary<ConstructionTypeRef, FailureTermID> {
                    .operation = UnaryOperator::Negate,
                    .operand = OwnedSemanticExpression<ConstructionTypeRef, FailureTermID>(
                        std::move(operand)
                    )
                };
                return result;
            }
        ));
    }
    SUBCASE("binary") {
        CHECK(rejects_expression(
            "structured-binary-contract",
            [](PreparedFunction& prepared, BodyFixture& body) static noexcept {
                auto left = boolean_expression(prepared, body);
                auto right = boolean_expression(prepared, body);
                auto result = boolean_expression(prepared, body);
                result.value = SemBinary<ConstructionTypeRef, FailureTermID> {
                    .left = OwnedSemanticExpression<ConstructionTypeRef, FailureTermID>(
                        std::move(left)
                    ),
                    .operation = BinaryOperator::Add,
                    .right = OwnedSemanticExpression<ConstructionTypeRef, FailureTermID>(
                        std::move(right)
                    )
                };
                return result;
            }
        ));
    }
    SUBCASE("cast") {
        CHECK(rejects_expression(
            "structured-cast-contract",
            [](PreparedFunction& prepared, BodyFixture& body) static noexcept {
                auto operand = boolean_expression(prepared, body);
                auto result = boolean_expression(prepared, body);
                result.value = SemCast<ConstructionTypeRef, FailureTermID> {
                    .operand = OwnedSemanticExpression<ConstructionTypeRef, FailureTermID>(
                        std::move(operand)
                    ),
                    .kind = CastKind::IntegerToInteger
                };
                return result;
            }
        ));
    }
}

TEST_CASE("SemIR body invariant: scalar literals are representable") {
    SUBCASE("integer range") {
        CHECK(rejects_expression(
            "structured-integer-range",
            [](PreparedFunction& prepared, BodyFixture& body) static noexcept {
                auto result = boolean_expression(prepared, body);
                result.type = prepared.builder.intern_builtin_type(BuiltinType::U8);
                result.value = SemLiteral {
                    .value = IntegerLiteral {.value = IntegerConstant::from_signed(256)}
                };
                return result;
            }
        ));
    }
    SUBCASE("Unicode scalar") {
        CHECK(rejects_expression(
            "structured-unicode-scalar",
            [](PreparedFunction& prepared, BodyFixture& body) static noexcept {
                auto result = boolean_expression(prepared, body);
                result.type = prepared.builder.intern_builtin_type(BuiltinType::Char);
                result.value = SemLiteral {.value = CharacterLiteral {.scalar = 0xd800u}};
                return result;
            }
        ));
    }
}

TEST_CASE("SemIR body invariant: body-local references reject foreign owners") {
    auto first_sources = SourceManager();
    auto second_sources = SourceManager();
    auto first_diagnostics = DiagnosticSink();
    auto second_diagnostics = DiagnosticSink();
    auto first = prepare_function(first_sources, first_diagnostics);
    auto second = prepare_function(second_sources, second_diagnostics);
    auto first_body = body_fixture(first);
    const auto second_body = body_fixture(second);
    auto result = boolean_expression(first, first_body);
    result.value = SemBinding {.binding = second_body.parameter};
    result.category = SemanticValueCategory::Place;
    auto resolved = finish_body(first, std::move(first_body), {}, std::move(result));
    CHECK(expect_termination("structured-foreign-binding", [&] noexcept {
        static_cast<void>(analyze_body_batch({&resolved, 1uz}, first.builder));
    }));
}

TEST_CASE("SemIR body invariant: test operations cannot occur in an ordinary function") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto prepared = prepare_function(sources, diagnostics);
    auto body = body_fixture(prepared);
    auto statements = std::vector<DraftStatement>();
    statements.push_back(
        DraftStatement {
            .origin = prepared.origin,
            .lifetime = body.lifetime,
            .value = SemTestReport<ConstructionTypeRef, FailureTermID> {
                .kind = TestReportKind::Fail,
                .condition = std::nullopt,
                .message = std::nullopt,
                .condition_source = std::nullopt
            }
        }
    );
    auto result = boolean_expression(prepared, body);
    auto resolved =
        finish_body(prepared, std::move(body), std::move(statements), std::move(result));
    CHECK(expect_termination("structured-test-operation-owner", [&] noexcept {
        static_cast<void>(analyze_body_batch({&resolved, 1uz}, prepared.builder));
    }));
}
