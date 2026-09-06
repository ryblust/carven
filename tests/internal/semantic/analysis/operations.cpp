module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.operations;

import :compiler.request;
import :diagnostics.code;
import :diagnostics.sink;
import :frontend.ast.control;
import :frontend.ast.expr;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.literal;
import :frontend.program.parse;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.operations;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
    return std::move(*result);
}

auto begin_compilation(SourceManager& sources, DiagnosticSink& diagnostics) noexcept
    -> ProgramDraft {
    const auto source = sources.append_virtual("operations.cv", "");
    REQUIRE(source.has_value());
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = *source,
            .module_path = path("operations"),
        },
    };
    auto syntax = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(syntax.has_value());
    return ProgramDraft::begin(std::move(*syntax), diagnostics);
}

struct OperationFixture final {
    SourceManager sources;
    DiagnosticSink diagnostics;
    ProgramDraft compilation;

    OperationFixture() noexcept
        : sources(),
          diagnostics(),
          compilation(begin_compilation(sources, diagnostics)) {}
};

auto binary_initializer(const SyntaxTree& tree, std::size_t index) noexcept
    -> const ASTBinaryExpr& {
    const auto ast = tree.view();
    const auto& declaration =
        get<ASTVariableDecl>(ast.statement(function_body(tree).statements[index]));
    return get<ASTBinaryExpr>(ast.expression(*declaration.initializer));
}

auto add_numeric_enum(ProgramDraft& compilation, TypeID underlying) noexcept -> TypeID {
    const auto provenance_module = compilation.provenance_module_at(0uz);
    const auto origin = compilation.append_source_origin(
        compilation.module_source(provenance_module),
        Span::at(0u)
    );
    const auto module_id = compilation.reserve_module_declaration();
    const auto enumeration = compilation.reserve_enum_declaration();
    compilation.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = provenance_module,
            .origin = origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {enumeration},
        }
    );
    compilation.define_declaration(
        enumeration,
        ConstructionEnumDeclaration {
            .module_id = module_id,
            .name = compilation.intern_spelling("Number"),
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .representation =
                ConstructionNumericEnumRepresentation {
                    .underlying_type = underlying,
                },
            .cases = {},
            .capabilities = NominalCapabilities {.equality = true},
        }
    );
    return compilation.intern_type(
        CanonicalType {
            .value = EnumTypeValue {.enumeration = enumeration},
        }
    );
}

} // namespace

TEST_CASE("Semantic operations: AST operators have one exact SemIR mapping") {
    const auto prefix_mappings = std::array {
        std::pair {ASTPrefixOperator::LogicalNot, UnaryOperator::LogicalNot},
        std::pair {ASTPrefixOperator::Negate, UnaryOperator::Negate},
        std::pair {ASTPrefixOperator::BitwiseNot, UnaryOperator::BitwiseNot},
    };
    for (const auto [ast, expected] : prefix_mappings) {
        CHECK_EQ(semantic_operator(ast), expected);
    }

    const auto binary_mappings = std::array {
        std::pair {ASTBinaryOperator::BitwiseOr, BinaryOperator::BitwiseOr},
        std::pair {ASTBinaryOperator::BitwiseXor, BinaryOperator::BitwiseXor},
        std::pair {ASTBinaryOperator::BitwiseAnd, BinaryOperator::BitwiseAnd},
        std::pair {ASTBinaryOperator::Equal, BinaryOperator::Equal},
        std::pair {ASTBinaryOperator::NotEqual, BinaryOperator::NotEqual},
        std::pair {ASTBinaryOperator::Less, BinaryOperator::Less},
        std::pair {ASTBinaryOperator::LessEqual, BinaryOperator::LessEqual},
        std::pair {ASTBinaryOperator::Greater, BinaryOperator::Greater},
        std::pair {ASTBinaryOperator::GreaterEqual, BinaryOperator::GreaterEqual},
        std::pair {ASTBinaryOperator::LeftShift, BinaryOperator::LeftShift},
        std::pair {ASTBinaryOperator::RightShift, BinaryOperator::RightShift},
        std::pair {ASTBinaryOperator::Add, BinaryOperator::Add},
        std::pair {ASTBinaryOperator::Subtract, BinaryOperator::Subtract},
        std::pair {ASTBinaryOperator::Multiply, BinaryOperator::Multiply},
        std::pair {ASTBinaryOperator::Divide, BinaryOperator::Divide},
        std::pair {ASTBinaryOperator::Remainder, BinaryOperator::Remainder},
    };
    for (const auto [ast, expected] : binary_mappings) {
        const auto operation = semantic_operator(ast);
        REQUIRE(operation.has_value());
        CHECK_EQ(*operation, expected);
    }
    CHECK_FALSE(semantic_operator(ASTBinaryOperator::LogicalOr).has_value());
    CHECK_FALSE(semantic_operator(ASTBinaryOperator::LogicalAnd).has_value());
}

TEST_CASE("Semantic operations: contextual binary operand planning is syntax-authoritative") {
    const auto tree = parse_valid(
        "fn plans() {"
        " let left_numeric = 1 + typed;"
        " let right_numeric = typed + 1;"
        " let both_numeric = 1 + 2;"
        " let independent = left + right;"
        " let left_case = .Ready == state;"
        " let right_case = state == .Ready;"
        " let payload_case = .Value(1) == state;"
        "}"
    );
    const auto ast = tree.view();
    CHECK_EQ(
        binary_operand_plan(ast, binary_initializer(tree, 0uz)),
        BinaryOperandPlan::LeftExpectedFromRight
    );
    CHECK_EQ(
        binary_operand_plan(ast, binary_initializer(tree, 1uz)),
        BinaryOperandPlan::RightExpectedFromLeft
    );
    CHECK_EQ(
        binary_operand_plan(ast, binary_initializer(tree, 2uz)),
        BinaryOperandPlan::RightExpectedFromLeft
    );
    CHECK_EQ(
        binary_operand_plan(ast, binary_initializer(tree, 3uz)),
        BinaryOperandPlan::Independent
    );
    CHECK_EQ(
        binary_operand_plan(ast, binary_initializer(tree, 4uz)),
        BinaryOperandPlan::LeftExpectedFromRight
    );
    CHECK_EQ(
        binary_operand_plan(ast, binary_initializer(tree, 5uz)),
        BinaryOperandPlan::RightExpectedFromLeft
    );
    CHECK_EQ(
        binary_operand_plan(ast, binary_initializer(tree, 6uz)),
        BinaryOperandPlan::LeftExpectedFromRight
    );
}

TEST_CASE("Semantic operations: decisions carry their stable diagnostic classification") {
    auto fixture = OperationFixture();
    auto& compilation = fixture.compilation;
    const auto boolean = compilation.intern_builtin_type(BuiltinType::Bool);
    const auto i32 = compilation.intern_builtin_type(BuiltinType::I32);
    const auto i64 = compilation.intern_builtin_type(BuiltinType::I64);
    const auto f32 = compilation.intern_builtin_type(BuiltinType::F32);
    const auto text = compilation.intern_builtin_type(BuiltinType::Str);
    const auto numeric_enum = add_numeric_enum(compilation, i32);

    CHECK(binary_operator_requires_equality(ASTBinaryOperator::Equal));
    CHECK_FALSE(binary_operator_requires_equality(ASTBinaryOperator::Add));
    CHECK_EQ(operator_result_builtin(OperatorResult::Boolean), BuiltinType::Bool);
    CHECK_FALSE(operator_result_builtin(OperatorResult::Operand).has_value());

    CHECK_EQ(
        select_contextual_numeric_type(
            compilation,
            i32,
            ConstructionTypeRef {i64},
            NumericSuffix::None
        ),
        i64
    );
    CHECK_EQ(
        select_contextual_numeric_type(
            compilation,
            i32,
            ConstructionTypeRef {f32},
            NumericSuffix::None
        ),
        i32
    );
    CHECK_EQ(
        select_contextual_numeric_type(
            compilation,
            i32,
            ConstructionTypeRef {i64},
            NumericSuffix::I32
        ),
        i32
    );

    const auto unary =
        decide_unary_operator(compilation, UnaryOperator::Negate, ConstructionTypeRef {boolean});
    REQUIRE_FALSE(unary.has_value());
    CHECK_EQ(unary.error().code, DiagnosticCode::TypePrefixNumeric);
    CHECK_EQ(unary.error().message, "arithmetic negation requires a numeric operand");

    const auto incompatible = decide_binary_operator(
        compilation,
        BinaryOperator::Less,
        ConstructionTypeRef {i32},
        ConstructionTypeRef {text},
        false,
        true
    );
    REQUIRE_FALSE(incompatible.has_value());
    CHECK_EQ(incompatible.error().code, DiagnosticCode::TypeBinary);

    const auto unsupported_equality = decide_binary_operator(
        compilation,
        BinaryOperator::Equal,
        ConstructionTypeRef {numeric_enum},
        ConstructionTypeRef {numeric_enum},
        true,
        false
    );
    REQUIRE_FALSE(unsupported_equality.has_value());
    CHECK_EQ(unsupported_equality.error().code, DiagnosticCode::TypeEqualityUnsupported);

    const auto ordered = decide_binary_operator(
        compilation,
        BinaryOperator::Less,
        ConstructionTypeRef {text},
        ConstructionTypeRef {text},
        true,
        true
    );
    REQUIRE_FALSE(ordered.has_value());
    CHECK_EQ(ordered.error().code, DiagnosticCode::TypeBinaryOrdered);

    const auto logical = decide_binary_operator(
        compilation,
        ASTBinaryOperator::LogicalAnd,
        ConstructionTypeRef {boolean},
        ConstructionTypeRef {boolean},
        true,
        true
    );
    REQUIRE(logical.has_value());
    CHECK_EQ(*logical, OperatorResult::Boolean);

    const auto cast =
        decide_cast(compilation, ConstructionTypeRef {i32}, ConstructionTypeRef {i64}, false);
    REQUIRE(cast.has_value());
    CHECK_EQ(*cast, CastKind::IntegerToInteger);
    const auto invalid_cast =
        decide_cast(compilation, ConstructionTypeRef {text}, ConstructionTypeRef {boolean}, false);
    REQUIRE_FALSE(invalid_cast.has_value());
    CHECK_EQ(invalid_cast.error().code, DiagnosticCode::TypeCast);
    const auto numeric_enum_cast = decide_cast(
        compilation,
        ConstructionTypeRef {numeric_enum},
        ConstructionTypeRef {i32},
        true
    );
    REQUIRE(numeric_enum_cast.has_value());
    CHECK_EQ(*numeric_enum_cast, CastKind::EnumToInteger);

    const auto method =
        decide_text_method(compilation, ConstructionTypeRef {text}, "is_empty", 0uz);
    REQUIRE(method.has_value());
    REQUIRE(method->has_value());
    CHECK_EQ(**method, TextIntrinsic::IsEmpty);
    CHECK_EQ(text_intrinsic_result(**method), BuiltinType::Bool);
    const auto non_text_method =
        decide_text_method(compilation, ConstructionTypeRef {i32}, "len", 0uz);
    REQUIRE(non_text_method.has_value());
    CHECK_FALSE(non_text_method->has_value());
    const auto property_as_method =
        decide_text_method(compilation, ConstructionTypeRef {text}, "bytes", 0uz);
    REQUIRE_FALSE(property_as_method.has_value());
    CHECK_EQ(property_as_method.error().code, DiagnosticCode::TypeStrMethod);
    const auto method_arity =
        decide_text_method(compilation, ConstructionTypeRef {text}, "len", 1uz);
    REQUIRE_FALSE(method_arity.has_value());
    CHECK_EQ(method_arity.error().code, DiagnosticCode::TypeStrMethodArity);
    const auto method_as_property = decide_text_property("len");
    REQUIRE_FALSE(method_as_property.has_value());
    CHECK_EQ(method_as_property.error().code, DiagnosticCode::TypeStrProperty);
    const auto property = decide_text_property("bytes");
    REQUIRE(property.has_value());
    CHECK_EQ(text_intrinsic_result(*property), BuiltinType::StrBytesView);
}

TEST_CASE("Semantic operations: construction types expose their exact recursive shape") {
    auto fixture = OperationFixture();
    auto& compilation = fixture.compilation;
    const auto i32 = compilation.intern_builtin_type(BuiltinType::I32);
    const auto text_view = compilation.intern_builtin_type(BuiltinType::StrBytesView);
    const auto failure = compilation.add_empty_failure_term();
    const auto first_array = compilation.append_construction_type(
        ConstructionType {
            .value = ConstructionArrayTypeValue {
                .element = i32,
                .extent = 4u,
            },
        }
    );
    const auto second_array = compilation.append_construction_type(
        ConstructionType {
            .value = ConstructionArrayTypeValue {
                .element = i32,
                .extent = 4u,
            },
        }
    );
    const auto callable = compilation.append_construction_type(
        ConstructionType {
            .value = ConstructionCallableViewTypeValue {
                .parameters = {{.access = AccessMode::Read, .type = i32}},
                .result = i32,
                .failures = failure,
            },
        }
    );
    const auto callable_array = compilation.append_construction_type(
        ConstructionType {
            .value = ConstructionArrayTypeValue {
                .element = callable,
                .extent = 2u,
            },
        }
    );

    CHECK(type_shapes_compatible(
        compilation,
        ConstructionTypeRef {first_array},
        ConstructionTypeRef {second_array}
    ));
    CHECK(type_supports_equality(compilation, ConstructionTypeRef {first_array}));
    CHECK_FALSE(type_supports_equality(compilation, ConstructionTypeRef {callable}));
    CHECK(type_contains_callable_view(compilation, ConstructionTypeRef {callable}));
    CHECK(type_contains_callable_view(compilation, ConstructionTypeRef {callable_array}));
    CHECK(builtin_type_supports_equality(BuiltinType::Str));
    CHECK_FALSE(builtin_type_supports_equality(BuiltinType::StrBytesView));
    CHECK_FALSE(type_supports_equality(compilation, ConstructionTypeRef {text_view}));
}

TEST_CASE("Semantic operations: evaluator failures retain stable diagnostic boundaries") {
    auto fixture = OperationFixture();
    auto& compilation = fixture.compilation;
    const auto text = compilation.intern_builtin_type(BuiltinType::Str);
    const auto text_value = ConstantFact {
        .type = text,
        .value = StringConstant {.value = compilation.intern_spelling("abc")},
    };
    const auto non_constant_view = evaluate_text_intrinsic_constant_value(
        compilation,
        TextIntrinsic::Bytes,
        text_value,
        compilation.intern_builtin_type(BuiltinType::StrBytesView)
    );
    REQUIRE_FALSE(non_constant_view.has_value());
    CHECK_EQ(non_constant_view.error(), ConstantEvaluationFailure::UnsupportedOperation);

    const auto divide_by_zero =
        constant_evaluation_diagnostic(ConstantEvaluationFailure::DivideByZero);
    REQUIRE(divide_by_zero.has_value());
    CHECK_EQ(divide_by_zero->code, DiagnosticCode::ConstDivideByZero);
    CHECK_FALSE(
        constant_evaluation_diagnostic(ConstantEvaluationFailure::OperandNotConstant).has_value()
    );
}
