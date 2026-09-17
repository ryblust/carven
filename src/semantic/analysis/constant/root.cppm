module carven:semantic.analysis.constant.root;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.constant.evaluation;
import :semantic.analysis.expr.aggregate;
import :semantic.analysis.expr.conversion;
import :semantic.analysis.expr.interpolation;
import :semantic.analysis.expr.interpret;
import :semantic.analysis.expr.operand;
import :semantic.analysis.expr.projection;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.analysis.expr.text;
import :semantic.analysis.failure;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.evaluation.freeze;
import :semantic.evaluation.limits;
import :semantic.evaluation.shape;
import :semantic.evaluation.value;
import :semantic.semir.body;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.table;
import :support.invariant;
import std;

template<typename Scope>
class ConstantRootSite final {
public:
    using Value = SemanticExpression;
    using Selection = Value;
    static constexpr auto mode = ExpressionMode::RequiredRoot;

    struct OperandState final {
        bool completes = true;
    };

    auto module_id() const noexcept -> ProgramModuleID { return module; }

    auto permits_pointer_narrowing() const noexcept -> bool { return true; }

    auto infer_type(Value& value, Span) const noexcept -> AnalysisResult<ConstructionTypeRef> {
        return type(value);
    }

    auto resolve_construction_type(const ASTConstructionType& type) noexcept
        -> ExpressionResult<ConstructionTypeRef> {
        return scope.resolve_construction_type(type);
    }

    auto cpp_construct(const ASTConstructionExpr&, ConstructionTypeRef, Span) const noexcept
        -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto aggregate_cost(std::size_t count, Span span) noexcept -> AnalysisResult<void> {
        if (count > maximum_constant_aggregate_elements
            || count > maximum_constant_aggregate_work - aggregate_work) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::ConstLimit,
                "constant aggregate exceeds its element budget"
            ));
        }
        aggregate_work += count;
        return {};
    }

    auto aggregate_admitted(ConstructionTypeRef type, Span span) noexcept -> AnalysisResult<bool> {
        const auto* concrete = std::get_if<TypeID>(&type);
        const auto shape = concrete ? shapes.get(*concrete) : std::nullopt;
        if (!shape || !shape->supported) {
            return false;
        }
        if (shape->elements > maximum_constant_aggregate_elements) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::ConstLimit,
                "constant aggregate exceeds its element or nesting budget"
            ));
        }
        return true;
    }

    auto consume_read(OperandState&, Value value, Span) const noexcept
        -> AnalysisResult<SemanticExpression> {
        return value;
    }

    auto finish_constructed(
        ConstructionTypeRef type,
        SemanticExpressionValue value,
        OperandState,
        Span span,
        std::optional<ConstantID> = std::nullopt
    ) noexcept -> Value {
        return make(type, std::move(value), span);
    }

    ConstantRootSite(
        ProgramDraft& program,
        ProgramModuleID module,
        ASTView syntax,
        Scope& scope,
        Span root_span
    ) noexcept
        : shapes(program),
          program(program),
          module(module),
          ast(syntax),
          scope(scope),
          lifetimes(program.create_evaluation_root_identity()),
          lifetime(lifetimes.add({
              .parent = std::nullopt,
              .kind = LifetimeRegionKind::FullExpression,
              .origin = program.append_source_origin(program.module_source(module), root_span),
          })),
          empty_failures(program.intern_failure_set({})) {
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
        -> ExpressionResult<Value> {
        const auto& source = ast.expression(id);
        const auto aggregate = std::holds_alternative<ASTArrayExpr>(source.value)
            || std::holds_alternative<ASTConstructionExpr>(source.value);
        if (aggregate && aggregate_depth >= maximum_constant_aggregate_depth) {
            return std::unexpected(fail(
                source.span,
                DiagnosticCode::ConstLimit,
                "constant aggregate exceeds its nesting budget"
            ));
        }
        aggregate_depth += aggregate;
        const auto depth = AggregateDepth {.depth = aggregate_depth, .entered = aggregate};
        return interpret_expression(*this, id, expected);
    }

    auto read_array_element(
        ASTExprID id,
        std::optional<ConstructionTypeRef> expected,
        bool
    ) noexcept -> ExpressionResult<Value> {
        return read(id, expected);
    }

    auto type(const Value& value) const noexcept -> ConstructionTypeRef {
        return value.type.construction();
    }

    auto known(const Value& value) const noexcept -> std::optional<ConstantID> {
        return value.constant;
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

    auto resolve_type(ASTTypeID type) noexcept -> ExpressionResult<ConstructionTypeRef> {
        return scope.resolve_type(type);
    }

    auto c_string(std::string_view, Span) const noexcept -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto constant(ConstantID value, Span span) noexcept -> Value {
        return make(program.constant(value).type, SemConstant {.constant = value}, span);
    }

    auto enter_operand_execution(bool) const noexcept -> std::monostate { return {}; }

    auto finish_short_circuit(
        bool conjunction,
        Value left,
        Value right,
        std::optional<ConstantID>,
        Span span
    ) noexcept -> Value {
        return make(
            program.builtin_type(BuiltinType::Bool),
            SemShortCircuit {
                .left = OwnedSemanticExpression(std::move(left)),
                .operation = conjunction ? ShortCircuitOperator::And : ShortCircuitOperator::Or,
                .right = OwnedSemanticExpression(std::move(right))
            },
            span
        );
    }

    auto external_unary(UnaryOperator, Value, Span) const noexcept -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto external_binary(BinaryOperator, Value, Value, Span) const noexcept
        -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto external_cast(ConstructionTypeRef, Value, Span) const noexcept -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto extension(const ASTNameExpr& name, Span span, std::optional<ConstructionTypeRef>) noexcept
        -> ExpressionResult<Value> {
        auto resolved =
            scope.resolve_name(program.source_slice_copy(module, name.name_span), name.name_span);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        if (!resolved->has_value()) {
            return std::unexpected(ExpressionNotAdmitted {});
        }
        return constant(**resolved, span);
    }

    auto extension(
        const ASTPropagationExpr& source,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionResult<Value> {
        const auto first = pending_failures.size();
        auto operand = read(source.operand_id, expected);
        if (!operand) {
            return std::unexpected(operand.error());
        }
        auto terms =
            std::vector<FailureTermID>(pending_failures.begin() + first, pending_failures.end());
        pending_failures.erase(pending_failures.begin() + first, pending_failures.end());
        program.require_non_empty_failures(
            program.add_union_failure_term(std::move(terms)),
            program.append_source_origin(program.module_source(module), source.operator_span)
        );
        const auto result_type = type(*operand);
        return make(result_type, SemPropagate {OwnedSemanticExpression(std::move(*operand))}, span);
    }

    template<typename Form>
    auto extension(const Form&, Span, std::optional<ConstructionTypeRef>) const noexcept
        -> ExpressionResult<Value> {
        static_assert(
            std::same_as<Form, ASTCppNameExpr>
            || std::same_as<Form, ASTAccessExpr>
            || std::same_as<Form, ASTIfForm>
            || std::same_as<Form, ASTLambdaExpr>
            || std::same_as<Form, ASTMatchForm>
            || std::same_as<Form, ASTTryForm>
        );
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto extension(
        const ASTConstructionExpr& source,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> ExpressionResult<Value> {
        return construct_structure_expression(*this, source, span);
    }

    auto extension(
        const ASTArrayExpr& source,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionResult<Value> {
        return construct_array_expression(*this, source, span, expected);
    }

    auto extension(
        const ASTIndexExpr& source,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> ExpressionResult<Value> {
        return construct_index_expression(*this, source, span);
    }

    auto extension(
        const ASTInterpolationExpr& source,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> ExpressionResult<Value> {
        return construct_interpolation(*this, source, span);
    }

    auto spelling(Span span) const noexcept -> std::string {
        return program.source_slice_copy(module, span);
    }

    auto resolve_enum_qualifier(ASTExprID id) noexcept -> ExpressionResult<std::optional<TypeID>> {
        return scope.resolve_enum_qualifier(id);
    }

    auto resolve_enum_case(TypeID type, std::string_view name, Span span) noexcept
        -> ExpressionResult<ResolvedEnumCase> {
        auto selected = scope.resolve_enum_case(type, name, span);
        if (!selected) {
            return std::unexpected(selected.error());
        }
        auto prepared = scope.construction_requests().ensure_type(type, module, span);
        if (!prepared) {
            return std::unexpected(prepared.error());
        }
        return selected;
    }

    auto invalid_enum_qualifier(Span) const noexcept -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto require_invariant_storage(
        ConstructionTypeRef source,
        ConstructionTypeRef target,
        Span span
    ) noexcept -> AnalysisResult<void> {
        if (source != target) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::TypeMismatch,
                "borrowed storage requires the same element type"
            ));
        }
        return {};
    }

    auto convert_argument(Value& value, ConstructionTypeRef expected, Span span) noexcept
        -> ExpressionResult<void> {
        const auto converted = convert_intrinsic_argument(*this, value, expected, span);
        if (!converted) {
            return std::unexpected(converted.error());
        }
        if (*converted) {
            return {};
        }
        const auto source = type(value);
        if (!type_shapes_compatible(program, source, expected)) {
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

    auto known_sequence_extent(const Value& value) const noexcept -> std::optional<std::uint64_t> {
        const auto shape = sequence_shape(program, type(value));
        return shape ? shape->extent : std::nullopt;
    }

    auto dereference(const ASTPrefixExpr&, Span) const noexcept -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto external_index(Value, Value, Span) const noexcept -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto external_member(const ASTMemberExpr&, Value, Span) const noexcept
        -> ExpressionResult<Selection> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto finish_index(
        ConstructionTypeRef type,
        bool,
        IndexBoundsPolicy bounds,
        Value receiver,
        Value index,
        Span span
    ) noexcept -> Value {
        return make(
            type,
            SemIndex {
                .source = OwnedSemanticExpression(std::move(receiver)),
                .index = OwnedSemanticExpression(std::move(index)),
                .bounds = bounds
            },
            span
        );
    }

    auto finish_field(
        ConstructionTypeRef type,
        FieldProjection field,
        Value receiver,
        Span span
    ) noexcept -> Value {
        return make(
            type,
            SemField {.source = OwnedSemanticExpression(std::move(receiver)), .field = field},
            span
        );
    }

    auto member_call(const ASTCallExpr&, const ASTMemberExpr&, Value, Span) const noexcept
        -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto call(const ASTCallExpr& source, Span span) noexcept -> ExpressionResult<Value> {
        auto callee = source.callee;
        while (const auto* group = std::get_if<ASTGroupExpr>(&ast.expression(callee).value)) {
            callee = group->expression;
        }
        const auto* name = std::get_if<ASTNameExpr>(&ast.expression(callee).value);
        if (name == nullptr) {
            return std::unexpected(ExpressionNotAdmitted {});
        }
        auto selected = scope.resolve_function(spelling(name->name_span), name->name_span);
        if (!selected) {
            return std::unexpected(selected.error());
        }
        if (!*selected) {
            return std::unexpected(ExpressionNotAdmitted {});
        }
        const auto declaration = program.function_declaration_copy(**selected);
        if (!declaration.is_const) {
            return std::unexpected(ExpressionNotAdmitted {});
        }
        auto& requests = scope.construction_requests();
        auto completed = requests.ensure_function_signature(**selected, module, span);
        if (!completed) {
            return std::unexpected(completed.error());
        }
        const auto contract = program.construction_callable_contract_copy(declaration.callable);
        if (contract.parameters.size() != source.arguments.size()) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::TypeCallArity,
                "function call has the wrong number of arguments"
            ));
        }
        auto arguments = std::vector<SemCallArgument>();
        for (auto index = 0uz; index < source.arguments.size(); ++index) {
            const auto& source_argument = source.arguments[index];
            const auto& parameter = contract.parameters[index];
            const auto selected = call_argument_operand(ast, source_argument.expression);
            if (selected.access != parameter.access) {
                return std::unexpected(fail(
                    span,
                    DiagnosticCode::AccessCallMismatch,
                    "constant call argument access does not match the parameter"
                ));
            }
            auto argument = read(selected.expression, parameter.type);
            if (!argument) {
                return std::unexpected(argument.error());
            }
            auto checked = convert_argument(
                *argument,
                parameter.type,
                ast.expression(source_argument.expression).span
            );
            if (!checked) {
                return std::unexpected(checked.error());
            }
            arguments.push_back({.access = selected.access, .expression = std::move(*argument)});
        }
        const auto callee_type =
            program.intern_type({.value = FunctionTypeValue {.callable = declaration.callable}});
        auto selected_callee =
            make(callee_type, SemCallable {.callable = declaration.callable}, span);
        pending_failures.push_back(contract.failures);
        return make(
            contract.result,
            SemCall {
                .callee = OwnedSemanticExpression(std::move(selected_callee)),
                .arguments = std::move(arguments),
                .callee_failures = BodyFailures(contract.failures)
            },
            span
        );
    }

    auto admits(const ASTExpr& expression) const noexcept -> bool {
        return expression.value.visit([&](const auto& form) noexcept {
            using Form = std::remove_cvref_t<decltype(form)>;
            if constexpr (std::same_as<Form, ASTLiteral>
                          || std::same_as<Form, ASTGroupExpr>
                          || std::same_as<Form, ASTNameExpr>
                          || std::same_as<Form, ASTContextualCaseExpr>
                          || std::same_as<Form, ASTPrefixExpr>
                          || std::same_as<Form, ASTRangeExpr>
                          || std::same_as<Form, ASTBinaryExpr>
                          || std::same_as<Form, ASTCastExpr>
                          || std::same_as<Form, ASTInterpolationExpr>
                          || std::same_as<Form, ASTConstructionExpr>
                          || std::same_as<Form, ASTArrayExpr>
                          || std::same_as<Form, ASTIndexExpr>
                          || std::same_as<Form, ASTMemberExpr>
                          || std::same_as<Form, ASTPropagationExpr>
                          || std::same_as<Form, ASTCallExpr>) {
                return true;
            } else {
                return false;
            }
        });
    }

    auto evaluate(const Value& value) noexcept -> AnalysisResult<ExecutionValue> {
        if (!pending_failures.empty()) {
            program.require_empty_failures(
                program.add_union_failure_term(std::move(pending_failures)),
                value.origin,
                EmptyFailureRequirementKind::OrdinaryConsumption
            );
        }
        return evaluate_constant_root(program, scope.construction_requests(), value);
    }

private:
    template<typename Operation>
    auto make(ConstructionTypeRef type, Operation operation, Span span) noexcept -> Value {
        auto known = std::optional<ConstantID>();
        if constexpr (std::same_as<Operation, SemConstant>) {
            known = operation.constant;
        }
        return SemanticExpression {
            .type = BodyType(type),
            .lifetime = lifetime,
            .origin = program.append_source_origin(program.module_source(module), span),
            .constant = known,
            .failures = empty_failures,
            .exits_test = false,
            .category = SemanticValueCategory::Value,
            .value = std::move(operation),
        };
    }

public:
    auto read_argument(ASTExprID expression, std::optional<ConstructionTypeRef> expected) noexcept
        -> ExpressionResult<Value> {
        return read_value_argument(*this, expression, expected);
    }

private:
    ExecutionTypeShapes shapes;
    ProgramDraft& program;
    ProgramModuleID module;
    ASTView ast;
    Scope& scope;
    MutableBodyTable<LifetimeRegion, LifetimeRegionID> lifetimes;
    LifetimeRegionID lifetime;
    BodyFailures empty_failures;
    std::vector<FailureTermID> pending_failures;
    std::size_t aggregate_work = 0uz;

    struct AggregateDepth final {
        std::size_t& depth;
        bool entered;

        ~AggregateDepth() noexcept { depth -= entered; }
    };

    std::size_t aggregate_depth = 0uz;
};

template<typename Scope>
auto evaluate_constant_expression(
    ProgramDraft& draft,
    ProgramModuleID module,
    ASTView syntax,
    Scope& scope,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected = std::nullopt
) noexcept -> ExpressionResult<ConstantID> {
    auto site = ConstantRootSite(draft, module, syntax, scope, syntax.expression(expression).span);
    auto result = site.read(expression, expected);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    if (expected) {
        auto checked =
            site.convert_argument(*result, *expected, syntax.expression(expression).span);
        if (!checked) {
            return std::unexpected(checked.error());
        }
    }
    auto evaluated = site.evaluate(*result);
    if (!evaluated) {
        return std::unexpected(evaluated.error());
    }
    if (const auto constant = freeze_constant_value(draft, std::move(*evaluated))) {
        if (expected && !type_shapes_compatible(draft, draft.constant(*constant).type, *expected)) {
            return std::unexpected(site.fail(
                syntax.expression(expression).span,
                DiagnosticCode::TypeMismatch,
                "expression has an incompatible type"
            ));
        }
        return *constant;
    }
    return std::unexpected(ExpressionNotAdmitted {});
}

template<typename Scope>
auto evaluate_array_extent(
    ProgramDraft& draft,
    ProgramModuleID module,
    ASTView syntax,
    Scope& scope,
    ASTExprID expression
) noexcept -> AnalysisResult<std::uint64_t> {
    auto site = ConstantRootSite(draft, module, syntax, scope, syntax.expression(expression).span);
    auto value = site.read(expression, std::nullopt);
    if (!value) {
        if (const auto* diagnostic = std::get_if<AnalysisFailure>(&value.error())) {
            return std::unexpected(*diagnostic);
        }
    }
    auto fact = std::optional<ConstantAtom>();
    if (value) {
        auto evaluated = site.evaluate(*value);
        if (!evaluated) {
            return std::unexpected(evaluated.error());
        }
        if (const auto result = execution_atom(draft, *evaluated)) {
            fact = *result;
        }
    }
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
