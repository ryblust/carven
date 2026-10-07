module carven:test.internal.semantic.analysis.operations;

import :diagnostics.code;
import :diagnostics.sink;
import :frontend.ast.control;
import :frontend.ast.expr;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.literal;
import :frontend.program.parse;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.evaluation.operation;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.sequence;
import :semantic.semir.type;
import :semantic.visibility;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    require(result.has_value());
    return std::move(*result);
}

auto begin_compilation(SourceManager& sources, DiagnosticSink& diagnostics) noexcept
    -> ProgramDraft {
    const auto source = sources.append_virtual("operations.cv", "");
    require(source.has_value());
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *source,
            .module_path = path("operations"),
        },
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    require(syntax.has_value());
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
        EnumDeclaration {
            .module_id = module_id,
            .name = compilation.intern_spelling("Number"),
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .representation =
                NumericEnumRepresentation {
                    .underlying_type = underlying,
                },
            .cases = {},
            .supports_equality = true,
        }
    );
    return compilation.intern_type(
        CanonicalType {
            .value = EnumTypeValue {.enumeration = enumeration},
        }
    );
}

const TestSuite suite([] static noexcept {
    "Semantic sequences: shape kind is independent of a static extent"_test = [] static noexcept {
        auto fixture = OperationFixture();
        auto& draft = fixture.compilation;
        const auto element = draft.builtin_type(BuiltinType::I32);
        const auto array = draft.intern_type({.value = ArrayTypeValue {element, 0u}});
        const auto slice = draft.intern_type({.value = SliceTypeValue {element}});
        const auto owner = draft.intern_type({.value = OwnedSequenceTypeValue {element}});
        const auto array_shape = sequence_shape(draft, array);
        const auto slice_shape = sequence_shape(draft, slice);
        const auto owner_shape = sequence_shape(draft, owner);
        require(array_shape.has_value());
        require(slice_shape.has_value());
        require(owner_shape.has_value());
        expect_equal(array_shape->kind, SequenceShapeKind::Array);
        require(array_shape->extent.has_value());
        expect_equal(*array_shape->extent, 0u);
        expect_equal(slice_shape->kind, SequenceShapeKind::Slice);
        expect(!slice_shape->extent.has_value());
        expect_equal(owner_shape->kind, SequenceShapeKind::OwnedSequence);
        expect(!owner_shape->extent.has_value());
        expect(owner_shape->element == ConstructionTypeRef(element));
        const auto push = decide_sequence_method(draft, owner, "push", 1uz);
        require(push.has_value());
        require(push->has_value());
        expect_equal(**push, SequenceIntrinsic::Push);
        expect(!decide_sequence_method(draft, owner, "push", 0uz).has_value());
        expect(!decide_sequence_method(draft, owner, "as_slice", 0uz).has_value());
        const auto on_slice = decide_sequence_method(draft, slice, "push", 1uz);
        require(on_slice.has_value());
        expect(!on_slice->has_value());
        expect_equal(
            sequence_intrinsic_contract(SequenceIntrinsic::Push).receiver_access,
            AccessMode::Write
        );
        expect_equal(
            sequence_intrinsic_contract(SequenceIntrinsic::Len).receiver_access,
            AccessMode::Read
        );
    };

    "Semantic operations: AST operators have one exact SemIR mapping"_test = [] static noexcept {
        const auto prefix_mappings = std::array {
            std::pair {ASTPrefixOperator::LogicalNot, UnaryOperator::LogicalNot},
            std::pair {ASTPrefixOperator::Negate, UnaryOperator::Negate},
            std::pair {ASTPrefixOperator::BitwiseNot, UnaryOperator::BitwiseNot},
        };
        for (const auto [ast, expected] : prefix_mappings) {
            expect(((semantic_operator(ast)) == (expected)))
                .note("semantic_operator(ast) == expected");
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
            if (!expect(operation.has_value())) {
                return;
            }
            expect(((*operation) == (expected))).note("*operation == expected");
        }
        expect(!(semantic_operator(ASTBinaryOperator::LogicalOr).has_value()));
        expect(!(semantic_operator(ASTBinaryOperator::LogicalAnd).has_value()));
    };

    "Semantic operations: contextual binary operand planning is syntax-authoritative"_test =
        [] static noexcept {
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
            expect_equal(
                binary_operand_plan(ast, binary_initializer(tree, 0uz)),
                BinaryOperandPlan::LeftExpectedFromRight
            );
            expect_equal(
                binary_operand_plan(ast, binary_initializer(tree, 1uz)),
                BinaryOperandPlan::RightExpectedFromLeft
            );
            expect_equal(
                binary_operand_plan(ast, binary_initializer(tree, 2uz)),
                BinaryOperandPlan::RightExpectedFromLeft
            );
            expect_equal(
                binary_operand_plan(ast, binary_initializer(tree, 3uz)),
                BinaryOperandPlan::Independent
            );
            expect_equal(
                binary_operand_plan(ast, binary_initializer(tree, 4uz)),
                BinaryOperandPlan::LeftExpectedFromRight
            );
            expect_equal(
                binary_operand_plan(ast, binary_initializer(tree, 5uz)),
                BinaryOperandPlan::RightExpectedFromLeft
            );
            expect_equal(
                binary_operand_plan(ast, binary_initializer(tree, 6uz)),
                BinaryOperandPlan::LeftExpectedFromRight
            );
        };

    "Semantic operations: decisions carry their stable diagnostic classification"_test =
        [] static noexcept {
            auto fixture = OperationFixture();
            auto& compilation = fixture.compilation;
            const auto boolean = compilation.builtin_type(BuiltinType::Bool);
            const auto i32 = compilation.builtin_type(BuiltinType::I32);
            const auto i64 = compilation.builtin_type(BuiltinType::I64);
            const auto f32 = compilation.builtin_type(BuiltinType::F32);
            const auto text = compilation.builtin_type(BuiltinType::Str);
            const auto numeric_enum = add_numeric_enum(compilation, i32);

            expect(binary_operator_requires_equality(ASTBinaryOperator::Equal));
            expect(!(binary_operator_requires_equality(ASTBinaryOperator::Add)));
            expect(operator_result_builtin(OperatorResult::Boolean) == BuiltinType::Bool)
                .note("Boolean operator result selects the Bool builtin");
            expect(!(operator_result_builtin(OperatorResult::Operand).has_value()));

            expect((select_contextual_numeric_type(
                        compilation,
                        i32,
                        ConstructionTypeRef {i64},
                        NumericSuffix::None
                    )
                    == i64))
                .note(
                    "select_contextual_numeric_type(\n                    compilation,\n                    i32,\n                    Constru..."
                );
            expect((select_contextual_numeric_type(
                        compilation,
                        i32,
                        ConstructionTypeRef {f32},
                        NumericSuffix::None
                    )
                    == i32))
                .note(
                    "select_contextual_numeric_type(\n                    compilation,\n                    i32,\n                    Constru..."
                );
            expect((select_contextual_numeric_type(
                        compilation,
                        i32,
                        ConstructionTypeRef {i64},
                        NumericSuffix::I32
                    )
                    == i32))
                .note(
                    "select_contextual_numeric_type(\n                    compilation,\n                    i32,\n                    Constru..."
                );

            const auto unary = decide_unary_operator(
                compilation,
                UnaryOperator::Negate,
                ConstructionTypeRef {boolean}
            );
            if (!expect(!(unary.has_value()))) {
                return;
            }
            expect_equal(unary.error().code, DiagnosticCode::TypePrefixNumeric);
            expect_equal(
                unary.error().message,
                std::string_view("arithmetic negation requires a numeric operand")
            );

            const auto incompatible = decide_binary_operator(
                compilation,
                BinaryOperator::Less,
                ConstructionTypeRef {i32},
                ConstructionTypeRef {text},
                false,
                true
            );
            if (!expect(!(incompatible.has_value()))) {
                return;
            }
            expect_equal(incompatible.error().code, DiagnosticCode::TypeBinary);

            const auto unsupported_equality = decide_binary_operator(
                compilation,
                BinaryOperator::Equal,
                ConstructionTypeRef {numeric_enum},
                ConstructionTypeRef {numeric_enum},
                true,
                false
            );
            if (!expect(!(unsupported_equality.has_value()))) {
                return;
            }
            expect_equal(
                unsupported_equality.error().code,
                DiagnosticCode::TypeEqualityUnsupported
            );

            const auto ordered = decide_binary_operator(
                compilation,
                BinaryOperator::Less,
                ConstructionTypeRef {text},
                ConstructionTypeRef {text},
                true,
                true
            );
            if (!expect(!(ordered.has_value()))) {
                return;
            }
            expect_equal(ordered.error().code, DiagnosticCode::TypeBinaryOrdered);

            const auto logical = decide_binary_operator(
                compilation,
                ASTBinaryOperator::LogicalAnd,
                ConstructionTypeRef {boolean},
                ConstructionTypeRef {boolean},
                true,
                true
            );
            if (!expect(logical.has_value())) {
                return;
            }
            expect_equal(*logical, OperatorResult::Boolean);

            const auto cast = decide_cast(
                compilation,
                ConstructionTypeRef {i32},
                ConstructionTypeRef {i64},
                false
            );
            if (!expect(cast.has_value())) {
                return;
            }
            expect_equal(*cast, CastKind::IntegerToInteger);
            const auto invalid_cast = decide_cast(
                compilation,
                ConstructionTypeRef {text},
                ConstructionTypeRef {boolean},
                false
            );
            if (!expect(!(invalid_cast.has_value()))) {
                return;
            }
            expect_equal(invalid_cast.error().code, DiagnosticCode::TypeCast);
            const auto numeric_enum_cast = decide_cast(
                compilation,
                ConstructionTypeRef {numeric_enum},
                ConstructionTypeRef {i32},
                true
            );
            if (!expect(numeric_enum_cast.has_value())) {
                return;
            }
            expect_equal(*numeric_enum_cast, CastKind::EnumToInteger);

            const auto method =
                decide_text_method(compilation, ConstructionTypeRef {text}, "is_empty", 0uz);
            if (!expect(method.has_value())) {
                return;
            }
            if (!expect(method->has_value())) {
                return;
            }
            expect_equal(**method, TextIntrinsic::IsEmpty);
            expect(((text_intrinsic_contract(**method).result)
                    == (TextIntrinsicType {BuiltinType::Bool})))
                .note(
                    "text_intrinsic_contract(**method).result == TextIntrinsicType {BuiltinType::Bool}"
                );
            const auto non_text_method =
                decide_text_method(compilation, ConstructionTypeRef {i32}, "len", 0uz);
            if (!expect(non_text_method.has_value())) {
                return;
            }
            expect(!(non_text_method->has_value()));
            const auto property_as_method =
                decide_text_method(compilation, ConstructionTypeRef {text}, "bytes", 0uz);
            if (!expect(!(property_as_method.has_value()))) {
                return;
            }
            expect_equal(property_as_method.error().code, DiagnosticCode::TypeMethodCall);
            const auto method_arity =
                decide_text_method(compilation, ConstructionTypeRef {text}, "len", 1uz);
            if (!expect(!(method_arity.has_value()))) {
                return;
            }
            expect_equal(method_arity.error().code, DiagnosticCode::TypeMethodCallArity);
            const auto method_as_property = decide_text_property("len");
            if (!expect(!(method_as_property.has_value()))) {
                return;
            }
            expect_equal(method_as_property.error().code, DiagnosticCode::TypeTextProperty);
            const auto property = decide_text_property("bytes");
            if (!expect(property.has_value())) {
                return;
            }
            expect(
                std::holds_alternative<TextIntrinsicShape>(
                    text_intrinsic_contract(*property).result
                )
            );
        };

    "Semantic operations: construction types expose their exact recursive shape"_test =
        [] static noexcept {
            auto fixture = OperationFixture();
            auto& compilation = fixture.compilation;
            const auto i32 = compilation.builtin_type(BuiltinType::I32);
            const auto text_view = compilation.intern_type(
                {.value = SliceTypeValue {.element = compilation.builtin_type(BuiltinType::U8)}}
            );
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
                        .parameters =
                            {{.stage = ParameterStage::Runtime,
                              .access = AccessMode::Read,
                              .type = i32}},
                        .result = i32,
                        .failures = failure,
                    },
                }
            );
            const auto staged_callable = compilation.append_construction_type(
                ConstructionType {
                    .value = ConstructionCallableViewTypeValue {
                        .parameters =
                            {{.stage = ParameterStage::Static,
                              .access = AccessMode::Read,
                              .type = i32}},
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

            expect(type_shapes_compatible(
                compilation,
                ConstructionTypeRef {first_array},
                ConstructionTypeRef {second_array}
            ));
            expect(type_supports_equality(compilation, ConstructionTypeRef {first_array}));
            expect(!(type_supports_equality(compilation, ConstructionTypeRef {callable})));
            expect(!type_shapes_compatible(compilation, callable, staged_callable));
            expect(type_contains_callable_view(compilation, ConstructionTypeRef {callable}));
            expect(type_contains_callable_view(compilation, ConstructionTypeRef {callable_array}));
            expect(builtin_type_supports_equality(BuiltinType::Str));
            expect(!(type_supports_equality(compilation, ConstructionTypeRef {text_view})));
        };

    "Semantic operations: evaluator failures retain stable diagnostic boundaries"_test =
        [] static noexcept {
            auto fixture = OperationFixture();
            auto& compilation = fixture.compilation;
            const auto text = compilation.builtin_type(BuiltinType::Str);
            const auto text_value = ConstantFact {
                .type = text,
                .value = StringConstant {.value = compilation.intern_spelling("abc")},
            };
            const auto non_constant_view = evaluate_text_intrinsic_constant_value(
                compilation,
                TextIntrinsic::Bytes,
                text_value,
                compilation.intern_type(
                    {.value = SliceTypeValue {.element = compilation.builtin_type(BuiltinType::U8)}}
                )
            );
            if (!expect(!(non_constant_view.has_value()))) {
                return;
            }
            expect_equal(
                non_constant_view.error(),
                ConstantEvaluationFailure::UnsupportedOperation
            );

            const auto divide_by_zero =
                constant_evaluation_diagnostic(ConstantEvaluationFailure::DivideByZero);
            if (!expect(divide_by_zero.has_value())) {
                return;
            }
            expect_equal(divide_by_zero->code, DiagnosticCode::ConstDivideByZero);
            expect(!(constant_evaluation_diagnostic(ConstantEvaluationFailure::OperandNotConstant)
                         .has_value()));
        };

    "Floating queries: receiver domains and arity are checked"_test = [] static noexcept {
        auto fixture = OperationFixture();
        auto& draft = fixture.compilation;
        for (const auto kind : {BuiltinType::F32, BuiltinType::F64}) {
            const auto selected =
                decide_float_method(draft, draft.builtin_type(kind), "is_finite", 0);
            require(selected.has_value() && selected->has_value());
            expect(**selected == FloatIntrinsic::IsFinite);
            const auto invalid =
                decide_float_method(draft, draft.builtin_type(kind), "is_finite", 1);
            require(!invalid.has_value());
            expect_equal(invalid.error().code, DiagnosticCode::TypeMethodCallArity);
        }
        const auto invalid =
            decide_float_method(draft, draft.builtin_type(BuiltinType::I32), "is_finite", 0);
        require(invalid.has_value());
        expect(!invalid->has_value());
        expect_diagnostic(
            analyze_test_errors("fn invalid(value: f64) { value.is_finite(1); }"),
            DiagnosticCode::TypeMethodCallArity
        );
    };

    "Semantic equality: shared enum payload dependencies propagate unsupported leaves"_test =
        [] static noexcept {
            for (const auto supported : {true, false}) {
                auto source = std::string(
                    supported ? "enum N0 { Value(i32) }\n"
                              : "struct Record { value: i32 } enum N0 { Value(Record) }\n"
                );
                for (auto index = 1uz; index < 28uz; ++index) {
                    source +=
                        std::format("enum N{} {{ Pair(N{}, N{}) }}\n", index, index - 1, index - 1);
                }
                const auto program = analyze_test_program(source);
                for (const auto [id, declaration] : program.declarations().enumerations()) {
                    static_cast<void>(id);
                    expect(((declaration.supports_equality) == (supported)))
                        .note(
                            "declaration.supports_equality == supported",
                            "supported = ",
                            supported
                        );
                }
            }
        };
});

} // namespace
