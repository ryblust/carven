module carven:semantic.analysis.expr.constant;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.interpret;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.program;
import :support.invariant;
import std;

template<typename Scope>
class ConstantExpressionSite final {
public:
    using Value = ConstantExpressionResult;
    using Result = Value;

    ConstantExpressionSite(
        ProgramDraft& program,
        ProgramModuleID module,
        ASTView syntax,
        Scope& scope
    ) noexcept
        : program(program),
          module(module),
          ast(syntax),
          scope(scope) {
        if (syntax.source_id() != program.syntax_tree(module).view().source_id()) {
            invariant_violation("constant expression mixed a module with another syntax tree");
        }
    }

    auto draft() noexcept -> ProgramDraft& { return program; }

    auto syntax() const noexcept -> ASTView { return ast; }

    auto fail(Span span, DiagnosticCode code, std::string message) noexcept -> AnalysisFailure {
        return program.diagnostics().error(DiagnosticBuilder(code, std::move(message))
                                               .primary(locate(ast.source_id(), span))
                                               .build());
    }

    auto read(ASTExprID id, std::optional<ConstructionTypeRef> expected) noexcept
        -> AnalysisResult<Value> {
        return interpret_expression(*this, id, expected);
    }

    auto present(const Value& value) const noexcept -> bool {
        return std::holds_alternative<ConstantID>(value);
    }

    auto unavailable() const noexcept -> Value { return NotConstant {}; }

    auto type(const Value& value) const noexcept -> ConstructionTypeRef {
        return program.constant_copy(std::get<ConstantID>(value)).type;
    }

    auto known(const Value& value) const noexcept -> std::optional<ConstantID> {
        const auto* constant = std::get_if<ConstantID>(&value);
        return constant == nullptr ? std::nullopt : std::optional(*constant);
    }

    auto external(ConstructionTypeRef type) const noexcept -> bool {
        const auto* concrete = std::get_if<TypeID>(&type);
        return concrete != nullptr
            && std::holds_alternative<CppTypeValue>(program.type_copy(*concrete).value);
    }

    auto supports_equality(ConstructionTypeRef type) noexcept -> bool {
        return scope.supports_equality(type);
    }

    auto numeric_enum(ConstructionTypeRef type) noexcept -> bool {
        const auto* concrete = std::get_if<TypeID>(&type);
        return concrete != nullptr && scope.is_numeric_enum(*concrete);
    }

    auto resolve_type(ASTTypeID type) noexcept -> AnalysisResult<ConstructionTypeRef> {
        return scope.resolve_type(type);
    }

    auto c_string(std::string_view, Span) const noexcept -> Value { return unavailable(); }

    auto constant(ConstantID value, Span) const noexcept -> Value { return value; }

    struct OperandExecution final {};

    auto enter_operand_execution(bool) const noexcept -> OperandExecution { return {}; }

    auto finish_unary(
        UnaryOperator,
        ConstructionTypeRef,
        Value,
        std::optional<ConstantID> known,
        Span
    ) const noexcept -> Value {
        return result(known);
    }

    auto finish_binary(
        BinaryOperator,
        ConstructionTypeRef,
        Value,
        Value,
        std::optional<ConstantID> known,
        Span
    ) const noexcept -> Value {
        return result(known);
    }

    auto finish_cast(
        CastKind,
        ConstructionTypeRef,
        Value,
        std::optional<ConstantID> known,
        Span
    ) const noexcept -> Value {
        return result(known);
    }

    auto finish_short_circuit(
        bool,
        Value,
        Value,
        std::optional<ConstantID> known,
        Span
    ) const noexcept -> Value {
        return result(known);
    }

    auto external_unary(UnaryOperator, Value, Span) const noexcept -> Value {
        return unavailable();
    }

    auto external_binary(BinaryOperator, Value, Value, Span) const noexcept -> Value {
        return unavailable();
    }

    auto external_cast(ConstructionTypeRef, Value, Span) const noexcept -> Value {
        return unavailable();
    }

    auto extension(const ASTNameExpr& name, Span, std::optional<ConstructionTypeRef>) noexcept
        -> AnalysisResult<Value> {
        auto resolved =
            scope.resolve_name(program.source_slice_copy(module, name.name_span), name.name_span);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        if (!resolved->constant.has_value()) {
            return unavailable();
        }
        const auto fact = program.constant_copy(*resolved->constant);
        if (ConstructionTypeRef {fact.type} != resolved->type) {
            invariant_violation("constant name resolution returned a mismatched type and value");
        }
        return *resolved->constant;
    }

    template<typename Form>
    auto extension(const Form&, Span, std::optional<ConstructionTypeRef>) const noexcept
        -> AnalysisResult<Value> {
        static_assert(
            std::same_as<Form, ASTCppNameExpr>
            || std::same_as<Form, ASTArrayExpr>
            || std::same_as<Form, ASTConstructionExpr>
            || std::same_as<Form, ASTAccessExpr>
            || std::same_as<Form, ASTIndexExpr>
            || std::same_as<Form, ASTPropagationExpr>
            || std::same_as<Form, ASTIfForm>
            || std::same_as<Form, ASTLambdaExpr>
            || std::same_as<Form, ASTMatchForm>
            || std::same_as<Form, ASTTryForm>
        );
        return unavailable();
    }

    auto spelling(Span span) const noexcept -> std::string {
        return program.source_slice_copy(module, span);
    }

    auto resolve_enum_qualifier(ASTExprID id) noexcept -> AnalysisResult<std::optional<TypeID>> {
        return scope.resolve_enum_qualifier(id);
    }

    auto resolve_enum_case(TypeID type, std::string_view name, Span span) noexcept
        -> AnalysisResult<ResolvedEnumCase> {
        return scope.resolve_enum_case(type, name, span);
    }

    auto invalid_enum_qualifier(Span) const noexcept -> Value { return unavailable(); }

    auto convert_argument(Value& value, ConstructionTypeRef expected, Span span) noexcept
        -> AnalysisResult<void> {
        if (!type_shapes_compatible(program, type(value), expected)) {
            return std::unexpected(
                fail(span, DiagnosticCode::TypeMismatch, "expression has an incompatible type")
            );
        }
        return {};
    }

    auto enum_constructor(TypeID, const ResolvedEnumCase&, Span span) noexcept
        -> AnalysisResult<Value> {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeEnumCaseArity,
            "payload enum case must be called with its payload"
        ));
    }

    auto finish_enum_case(
        TypeID,
        EnumCaseID,
        const std::vector<Value>&,
        std::optional<ConstantID> known,
        Span
    ) const noexcept -> Value {
        return result(known);
    }

    auto finish_text(
        TextIntrinsic,
        TypeID,
        Value,
        std::optional<ConstantID> known,
        Span
    ) const noexcept -> Value {
        return result(known);
    }

    auto member(const ASTMemberExpr&, Value, Span) const noexcept -> Value { return unavailable(); }

    auto member_call(const ASTCallExpr&, const ASTMemberExpr&, Value, Span) const noexcept
        -> Value {
        return unavailable();
    }

    auto call(const ASTCallExpr&, Span) const noexcept -> Value { return unavailable(); }

    auto admits(const ASTExpr& expression) const noexcept -> bool {
        return std::visit(
            [&](const auto& form) noexcept {
                using Form = std::remove_cvref_t<decltype(form)>;
                if constexpr (std::same_as<Form, ASTLiteral>
                              || std::same_as<Form, ASTGroupExpr>
                              || std::same_as<Form, ASTNameExpr>
                              || std::same_as<Form, ASTContextualCaseExpr>
                              || std::same_as<Form, ASTPrefixExpr>
                              || std::same_as<Form, ASTBinaryExpr>
                              || std::same_as<Form, ASTCastExpr>) {
                    return true;
                } else if constexpr (std::same_as<Form, ASTMemberExpr>) {
                    return form.op == ASTMemberOperator::Scope;
                } else if constexpr (std::same_as<Form, ASTCallExpr>) {
                    const auto& callee = ast.expression(form.callee).value;
                    return std::holds_alternative<ASTContextualCaseExpr>(callee)
                        || std::holds_alternative<ASTMemberExpr>(callee);
                } else {
                    return false;
                }
            },
            expression.value
        );
    }

private:
    auto result(std::optional<ConstantID> known) const noexcept -> Value {
        return known.has_value() ? Value {*known} : unavailable();
    }

    ProgramDraft& program;
    ProgramModuleID module;
    ASTView ast;
    Scope& scope;
};

template<typename Scope>
auto evaluate_constant_expression(
    ProgramDraft& draft,
    ProgramModuleID module,
    ASTView syntax,
    Scope& scope,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected = std::nullopt
) noexcept -> AnalysisResult<ConstantExpressionResult> {
    auto site = ConstantExpressionSite(draft, module, syntax, scope);
    auto result = site.read(expression, expected);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    if (site.present(*result) && expected.has_value()) {
        auto checked =
            site.convert_argument(*result, *expected, syntax.expression(expression).span);
        if (!checked.has_value()) {
            return std::unexpected(checked.error());
        }
    }
    return result;
}

template<typename Scope>
auto evaluate_array_extent(
    ProgramDraft& draft,
    ProgramModuleID module,
    ASTView syntax,
    Scope& scope,
    ASTExprID expression
) noexcept -> AnalysisResult<std::uint64_t> {
    auto site = ConstantExpressionSite(draft, module, syntax, scope);
    auto value = site.read(expression, std::nullopt);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    const auto fact = site.present(*value)
        ? std::optional(draft.constant_copy(std::get<ConstantID>(*value)))
        : std::nullopt;
    const auto* integer = fact.has_value() ? std::get_if<IntegerConstant>(&fact->value) : nullptr;
    const auto span = syntax.expression(expression).span;
    if (integer == nullptr) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::ConstArrayExtent,
            "array extent must be a constant integer"
        ));
    }
    if (integer->negative()) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::ConstNegativeArrayExtent,
            "array extent cannot be negative"
        ));
    }
    return integer->magnitude();
}
