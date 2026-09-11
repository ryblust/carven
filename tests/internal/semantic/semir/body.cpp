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
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.table;
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
    builder.finish_declaration_heads();
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
    const auto lifetime =
        body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, prepared.origin);
    const auto parameter = body.add_parameter(
        prepared.parameter_name,
        prepared.boolean_type,
        lifetime,
        AccessMode::Read,
        prepared.origin
    );
    return BodyFixture {
        .builder = std::move(body),
        .lifetime = lifetime,
        .parameter = parameter.binding,
        .failures = prepared.builder.add_empty_failure_term()
    };
}

auto boolean_expression(PreparedFunction& prepared, const BodyFixture& body) noexcept
    -> SemanticExpression {
    return SemanticExpression {
        .type = BodyType(prepared.boolean_type),
        .lifetime = body.lifetime,
        .origin = prepared.origin,
        .constant = std::nullopt,
        .failures = BodyFailures(body.failures),
        .exits_test = false,
        .category = SemanticValueCategory::Value,
        .value = SemConstant {
            .constant = prepared.builder.intern_constant(
                {.type = prepared.boolean_type, .value = BooleanConstant {.value = true}}
            )
        },
    };
}

auto finish_body(
    PreparedFunction& prepared,
    BodyFixture&& body,
    std::vector<SemanticStatement> statements,
    std::optional<SemanticExpression> result
) noexcept -> BodyID {
    auto draft = std::move(body.builder)
                     .finish(
                         SemanticRegion {
                             .lifetime = body.lifetime,
                             .origin = prepared.origin,
                             .statements = std::move(statements),
                             .result = std::move(result),
                             .failures = BodyFailures(body.failures),
                             .exits_test = false,
                         }
                     );
    const auto id = draft.id;
    prepared.builder.add_body_draft(std::move(draft));
    return id;
}

template<typename MakeExpression>
auto rejects_expression(std::string_view scenario, MakeExpression make_expression) noexcept
    -> bool {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto prepared = prepare_function(sources, diagnostics);
    auto body = body_fixture(prepared);
    auto invalid = std::invoke(make_expression, prepared, body);
    auto statements = std::vector<SemanticStatement>();
    statements.push_back(
        SemanticStatement {
            .origin = prepared.origin,
            .lifetime = body.lifetime,
            .value = SemExpressionStatement {.expression = std::move(invalid)}
        }
    );
    auto result = boolean_expression(prepared, body);
    [[maybe_unused]] const auto resolved =
        finish_body(prepared, std::move(body), std::move(statements), std::move(result));
    return expect_termination(scenario, [&] noexcept {
        static_cast<void>(std::move(prepared.builder).finish());
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
    [[maybe_unused]] const auto resolved =
        finish_body(prepared, std::move(body), {}, std::move(result));
    const auto body_id = resolved;
    auto finished = std::move(prepared.builder).finish();
    REQUIRE(finished.has_value());
    const auto program = std::move(*finished);
    const auto& published = program.bodies().body(body_id);
    CHECK_EQ(published.inputs().parameters, std::vector {parameter});
    REQUIRE(published.region().result.has_value());
    const auto* binding = std::get_if<SemBinding>(&published.region().result->value);
    REQUIRE(binding != nullptr);
    CHECK_EQ(binding->binding, parameter);
    CHECK_EQ(program.declarations().body_for_callable(prepared.callable), body_id);
    CHECK(diagnostics.empty());
}

TEST_CASE("SemIR body invariant: result agrees with callable contract") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto prepared = prepare_function(sources, diagnostics);
    auto body = body_fixture(prepared);
    [[maybe_unused]] const auto resolved = finish_body(prepared, std::move(body), {}, std::nullopt);
    CHECK(expect_termination("structured-result-contract", [&] noexcept {
        static_cast<void>(std::move(prepared.builder).finish());
    }));
}

TEST_CASE("SemIR body invariant: unary binary and cast operations retain type contracts") {
    SUBCASE("unary") {
        CHECK(rejects_expression(
            "structured-unary-contract",
            [](PreparedFunction& prepared, BodyFixture& body) static noexcept {
                auto operand = boolean_expression(prepared, body);
                auto result = boolean_expression(prepared, body);
                result.value = SemUnary {
                    .operation = UnaryOperator::Negate,
                    .operand = OwnedSemanticExpression(std::move(operand))
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
                result.value = SemBinary {
                    .left = OwnedSemanticExpression(std::move(left)),
                    .operation = BinaryOperator::Add,
                    .right = OwnedSemanticExpression(std::move(right))
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
                result.value = SemCast {
                    .operand = OwnedSemanticExpression(std::move(operand)),
                    .kind = CastKind::IntegerToInteger
                };
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
    [[maybe_unused]] const auto resolved =
        finish_body(first, std::move(first_body), {}, std::move(result));
    CHECK(expect_termination("structured-foreign-binding", [&] noexcept {
        static_cast<void>(std::move(first.builder).finish());
    }));
}

TEST_CASE("SemIR body invariant: test operations cannot occur in an ordinary function") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto prepared = prepare_function(sources, diagnostics);
    auto body = body_fixture(prepared);
    auto statements = std::vector<SemanticStatement>();
    statements.push_back(
        SemanticStatement {
            .origin = prepared.origin,
            .lifetime = body.lifetime,
            .value = SemTestReport {
                .kind = TestReportKind::Fail,
                .condition = std::nullopt,
                .message = std::nullopt,
                .condition_source = std::nullopt
            }
        }
    );
    auto result = boolean_expression(prepared, body);
    [[maybe_unused]] const auto resolved =
        finish_body(prepared, std::move(body), std::move(statements), std::move(result));
    CHECK(expect_termination("structured-test-operation-owner", [&] noexcept {
        static_cast<void>(std::move(prepared.builder).finish());
    }));
}

TEST_CASE("SemIR body: external calls require their declared result query") {
    CHECK(rejects_expression(
        "semir-cpp-call-result",
        [](PreparedFunction& prepared, const BodyFixture& body) static noexcept {
            auto expression = boolean_expression(prepared, body);
            expression.value = SemCppCall {
                .callee =
                    CppNameReference {
                        .context_module = prepared.module_id,
                        .lookup = CppNameLookup::Global,
                        .components = {"native", "value"}
                    },
                .arguments = {}
            };
            return expression;
        }
    ));
}

TEST_CASE("SemIR body invariant: every nested fact is resolved before delivery") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto prepared = prepare_function(sources, diagnostics);
    auto body = body_fixture(prepared);
    const auto completed = prepared.builder.intern_failure_set({});
    auto region = SemanticRegion {
        .lifetime = body.lifetime,
        .origin = prepared.origin,
        .statements = {},
        .result = std::nullopt,
        .failures = BodyFailures(completed),
        .exits_test = false,
    };
    auto attempt = boolean_expression(prepared, body);
    attempt.failures = BodyFailures(completed);
    attempt.value = SemTry {
        .body = OwnedSemanticRegion(std::move(region)),
        .protected_failures = BodyFailures(completed),
        .residual_failures = BodyFailures(completed),
        .arms = {},
    };
    auto* nested = std::get_if<SemTry>(&attempt.value);
    REQUIRE(nested != nullptr);
    auto scenario = std::string_view();
    SUBCASE("completed tree is accepted") {}
    SUBCASE("nested region must be resolved") {
        nested->body->failures = BodyFailures(body.failures);
        scenario = "nested region";
    }
    SUBCASE("catch metadata must be resolved") {
        nested->residual_failures = BodyFailures(body.failures);
        scenario = "catch metadata";
    }
    SUBCASE("expression type must be resolved") {
        attempt.type = BodyType(prepared.builder.append_construction_type(
            {.value = ConstructionArrayTypeValue {.element = prepared.boolean_type, .extent = 1u}}
        ));
        scenario = "expression type";
    }
    auto draft = std::move(body.builder)
                     .finish(
                         SemanticRegion {
                             .lifetime = body.lifetime,
                             .origin = prepared.origin,
                             .statements = {},
                             .result = std::move(attempt),
                             .failures = BodyFailures(completed),
                             .exits_test = false,
                         }
                     );
    const auto identity = draft.lifetime_regions.owner();
    auto deliver = [&] noexcept {
        return SemIRBody({
            .id = draft.id,
            .kind = draft.kind,
            .provenance_identity = draft.provenance_identity,
            .inputs = {},
            .lifetime_regions = std::move(draft.lifetime_regions),
            .bindings = MutableBodyTable<LocalBinding, LocalBindingID>(identity).seal(),
            .patterns = MutableBodyTable<Pattern, PatternID>(identity).seal(),
            .region = std::move(draft.region),
        });
    };
    if (!scenario.empty()) {
        CHECK(expect_termination(scenario, [&] noexcept { static_cast<void>(deliver()); }));
    } else {
        const auto finalized = deliver();
        CHECK_EQ(finalized.identity(), identity);
    }
}
