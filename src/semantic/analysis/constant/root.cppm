module carven:semantic.analysis.constant.root;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.constant.freeze;
import :semantic.analysis.expr.aggregate;
import :semantic.analysis.expr.async;
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
import :semantic.analysis.stage.session;
import :semantic.evaluation.admission;
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
class StaticRootSite final {
public:
    struct Value final {
        SemanticExpression expression;
        bool takeable;
    };

    using Selection = Value;
    static constexpr auto mode = ExpressionMode::StaticRoot;

    struct OperandState final {
        bool completes;
    };

    static auto operand_state() noexcept -> OperandState { return {.completes = true}; }

    auto module_id() const noexcept -> ProgramModuleID { return source_module_id; }

    auto construction_requests() noexcept -> ConstructionRequests& {
        return scope.construction_requests();
    }

    auto permits_pointer_narrowing() const noexcept -> bool { return true; }

    auto infer_type(Value& value, Span) const noexcept -> AnalysisResult<ConstructionTypeRef> {
        return type(value);
    }

    auto resolve_construction_type(const ASTConstructionType& type) noexcept
        -> ExpressionTask<ConstructionTypeRef> {
        co_return (co_await scope.resolve_construction_type(type));
    }

    auto cpp_construct(const ASTConstructionExpr&, ConstructionTypeRef, Span) const noexcept
        -> ExpressionTask<Value> {
        co_return std::unexpected(ExpressionNotAdmitted {});
    }

    auto representation_access(StructID owner, Span span) noexcept -> AnalysisResult<void> {
        return scope.representation_access(owner, span);
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
        if (!concrete) {
            return false;
        }
        if (shape && shape->exact && shape->elements > maximum_constant_aggregate_elements) {
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
        return std::move(value.expression);
    }

    auto consume_take(OperandState&, Value value, Span span) noexcept
        -> AnalysisResult<SemanticExpression> {
        return consume_argument(std::move(value), AccessMode::Take, span);
    }

    auto consume_write(OperandState&, Value value, Span span) noexcept
        -> AnalysisResult<SemanticExpression> {
        return consume_argument(std::move(value), AccessMode::Write, span);
    }

    auto consume_argument(Value value, AccessMode access, Span span) noexcept
        -> AnalysisResult<SemanticExpression> {
        if (access == AccessMode::Write) {
            return std::unexpected(
                fail(span, DiagnosticCode::AccessWriteArgument, "Write requires a place expression")
            );
        }
        if (access == AccessMode::Take && !value.takeable) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::AccessTakeOperand,
                "this binding cannot be used as a Take source"
            ));
        }
        return std::move(value.expression);
    }

    auto consume_cold_owner(OperandState&, Value value, AccessMode access, Span span) noexcept
        -> ExpressionResult<SemanticExpression> {
        if (access == AccessMode::Write) {
            return std::unexpected(
                fail(span, DiagnosticCode::AsyncOwnership, "composition takes cold owners")
            );
        }
        return consume_argument(std::move(value), AccessMode::Take, span);
    }

    auto require_matching_type(
        ConstructionTypeRef source,
        ConstructionTypeRef target,
        Span span
    ) noexcept -> AnalysisResult<void> {
        if (!type_shapes_compatible(program, source, target)) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::TypeMismatch,
                "selection operations require the same success type"
            ));
        }
        return {};
    }

    auto consume_read_argument(OperandState& state, Value value, Span span) const noexcept
        -> AnalysisResult<SemanticExpression> {
        return consume_read(state, std::move(value), span);
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

    StaticRootSite(
        ProgramDraft& program,
        ProgramModuleID module_id,
        ASTView syntax,
        Scope& scope,
        Span root_span
    ) noexcept
        : shapes(program),
          program(program),
          source_module_id(module_id),
          ast(syntax),
          scope(scope),
          lifetime(root_lifetime(program, module_id, scope, root_span)),
          empty_failures(program.add_empty_failure_term()) {
        if (syntax.source_id() != program.syntax_tree(module_id).view().source_id()) {
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
        -> ExpressionTask<Value> {
        const auto& source = ast.expression(id);
        const auto aggregate = std::holds_alternative<ASTTupleExpr>(source.value)
            || std::holds_alternative<ASTArrayExpr>(source.value)
            || std::holds_alternative<ASTArrayRepeatExpr>(source.value)
            || std::holds_alternative<ASTConstructionExpr>(source.value);
        if (aggregate && aggregate_depth >= maximum_constant_aggregate_depth) {
            co_return std::unexpected(fail(
                source.span,
                DiagnosticCode::ConstLimit,
                "constant aggregate exceeds its nesting budget"
            ));
        }
        aggregate_depth += aggregate;
        const auto depth = AggregateDepth {.depth = aggregate_depth, .entered = aggregate};
        auto result = co_await interpret_expression(*this, id, expected);
        if (result) {
            scope.observe_expression(id, type(*result));
        }
        co_return result;
    }

    auto read_aggregate_element(
        ASTExprID id,
        std::optional<ConstructionTypeRef> expected,
        bool
    ) noexcept -> ExpressionTask<Value> {
        co_return (co_await read(id, expected));
    }

    auto resolve_array_extent(ASTExprID expression) noexcept -> AnalysisTask<std::uint64_t>;

    auto pending_checkpoint() const noexcept -> std::size_t { return pending_failures.size(); }

    auto consume_unexecuted_read(Value value, std::size_t first, Span span) noexcept
        -> ExpressionResult<SemanticExpression> {
        if (type(value) == ConstructionTypeRef(program.builtin_type(BuiltinType::Void))) {
            return std::unexpected(
                fail(span, DiagnosticCode::TypeValueRequired, "expression does not produce a value")
            );
        }
        auto pending = std::vector<ConstructionFailureRef>(
            pending_failures.begin() + first,
            pending_failures.end()
        );
        pending_failures.erase(pending_failures.begin() + first, pending_failures.end());
        if (!pending.empty()) {
            program.require_empty_failures(
                program.add_union_failure_term(std::move(pending)),
                value.expression.origin,
                EmptyFailureRequirementKind::OrdinaryConsumption
            );
        }
        return std::move(value.expression);
    }

    auto type(const Value& value) const noexcept -> ConstructionTypeRef {
        return value.expression.type.construction();
    }

    auto known(const Value& value) const noexcept -> std::optional<ConstantID> {
        return value.expression.constant;
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

    auto resolve_type(ASTTypeID type) noexcept -> ExpressionTask<ConstructionTypeRef> {
        co_return (co_await scope.resolve_type(type));
    }

    auto resolve_duration_type(Span span) noexcept -> ExpressionTask<TypeID> {
        co_return (co_await scope.resolve_duration_type(span));
    }

    auto constant(ConstantID value, Span span) noexcept -> Value {
        return make(program.constant(value).type, constant_operation(program, value), span);
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
                .left = OwnedSemanticExpression(std::move(left.expression)),
                .operation = conjunction ? ShortCircuitOperator::And : ShortCircuitOperator::Or,
                .right = OwnedSemanticExpression(std::move(right.expression))
            },
            span
        );
    }

    auto external_unary(UnaryOperator, const Value&, Span) const noexcept
        -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto external_binary(BinaryOperator, const Value&, const Value&, Span) const noexcept
        -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto external_cast(ConstructionTypeRef, const Value&, Span) const noexcept
        -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto extension(const ASTNameExpr& name, Span span, std::optional<ConstructionTypeRef>) noexcept
        -> ExpressionTask<Value> {
        if constexpr (requires { scope.resolve_static_name(spelling(name.name_span), span); }) {
            auto selected = co_await scope.resolve_static_name(spelling(name.name_span), span);
            if (!selected) {
                co_return std::unexpected(selected.error());
            }
            if (*selected) {
                co_return Value {.expression = std::move(**selected), .takeable = false};
            }
        }
        auto resolved = (co_await scope.resolve_name(
            program.source_slice_copy(source_module_id, name.name_span),
            name.name_span
        ));
        if (!resolved.has_value()) {
            co_return std::unexpected(resolved.error());
        }
        if (!resolved->has_value()) {
            co_return std::unexpected(ExpressionNotAdmitted {});
        }
        auto value = constant(**resolved, span);
        value.takeable = false;
        co_return value;
    }

    auto extension(
        const ASTPropagationExpr& source,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Value> {
        const auto first = pending_failures.size();
        auto operand = (co_await read(source.operand_id, expected));
        if (!operand) {
            co_return std::unexpected(operand.error());
        }
        auto pending = std::vector<ConstructionFailureRef>(
            pending_failures.begin() + first,
            pending_failures.end()
        );
        pending_failures.erase(pending_failures.begin() + first, pending_failures.end());
        program.require_non_empty_failures(
            program.add_union_failure_term(std::move(pending)),
            program
                .append_source_origin(program.module_source(source_module_id), source.operator_span)
        );
        const auto result_type = type(*operand);
        const auto takeable = operand->takeable;
        auto result = make(
            result_type,
            SemPropagate {.operand = OwnedSemanticExpression(std::move(operand->expression))},
            span
        );
        result.takeable = takeable;
        co_return result;
    }

    auto extension(
        const ASTTypeApplicationExpr&,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> ExpressionTask<Value> {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::TypeGenericArguments,
            "type arguments require a generic function or a nominal type qualifier"
        ));
    }

    template<typename Form>
    auto extension(const Form&, Span, std::optional<ConstructionTypeRef>) const noexcept
        -> ExpressionTask<Value> {
        static_assert(
            std::same_as<Form, ASTCppNameExpr>
            || std::same_as<Form, ASTAccessExpr>
            || std::same_as<Form, ASTIfForm>
            || std::same_as<Form, ASTLambdaExpr>
            || std::same_as<Form, ASTMatchForm>
            || std::same_as<Form, ASTTryForm>
        );
        co_return std::unexpected(ExpressionNotAdmitted {});
    }

    auto extension(
        const ASTConstructionExpr& source,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Value> {
        co_return (co_await construct_structure_expression(*this, source, span, expected));
    }

    auto extension(
        const ASTTupleExpr& source,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Value> {
        co_return (co_await construct_tuple_expression(*this, source, span, expected));
    }

    auto extension(
        const ASTTupleElementExpr& source,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> ExpressionTask<Value> {
        co_return (co_await construct_tuple_element_expression(*this, source, span));
    }

    auto finish_tuple_element(
        ConstructionTypeRef type,
        std::uint32_t index,
        Value receiver,
        Span span
    ) noexcept -> Value {
        const auto takeable = receiver.takeable;
        auto value = make(
            type,
            SemTupleElement {OwnedSemanticExpression(std::move(receiver.expression)), index},
            span
        );
        value.takeable = takeable;
        return value;
    }

    auto extension(
        const ASTArrayExpr& source,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Value> {
        co_return (co_await construct_array_expression(*this, source, span, expected));
    }

    auto extension(
        const ASTArrayRepeatExpr& source,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Value> {
        co_return (co_await construct_array_expression(*this, source, span, expected));
    }

    auto extension(
        const ASTIndexExpr& source,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> ExpressionTask<Value> {
        co_return (co_await construct_index_expression(*this, source, span));
    }

    auto extension(
        const ASTInterpolationExpr& source,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> ExpressionTask<Value> {
        co_return (co_await construct_interpolation(*this, source, span));
    }

    auto observe_field(ProgramOriginID definition, Span name, ConstructionTypeRef type) noexcept
        -> void {
        scope.observe_field(definition, name, type);
    }

    auto spelling(Span span) const noexcept -> std::string {
        return program.source_slice_copy(source_module_id, span);
    }

    auto resolve_type_qualifier(ASTExprID id) noexcept
        -> ExpressionTask<std::optional<ConstructionTypeRef>> {
        co_return (co_await scope.resolve_type_qualifier(id));
    }

    auto resolve_enum_case(TypeID type, std::string_view name, Span span) noexcept
        -> ExpressionTask<ResolvedEnumCase> {
        auto selected = (co_await scope.resolve_enum_case(type, name, span));
        if (!selected) {
            co_return std::unexpected(selected.error());
        }
        auto prepared =
            (co_await scope.construction_requests().ensure_type(type, source_module_id, span));
        if (!prepared) {
            co_return std::unexpected(prepared.error());
        }
        co_return selected;
    }

    auto invalid_type_qualifier(Span) const noexcept -> ExpressionResult<Value> {
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
        const auto takeable = value.takeable;
        const auto converted = convert_intrinsic_argument(*this, value, expected, span);
        value.takeable &= takeable;
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
        return sequence_extent(program, value.expression);
    }

    auto dereference(const ASTPrefixExpr&, Span) const noexcept -> ExpressionTask<Value> {
        co_return std::unexpected(ExpressionNotAdmitted {});
    }

    auto external_index(const Value&, const Value&, Span) const noexcept
        -> ExpressionResult<Value> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto external_member(const ASTMemberExpr&, const Value&, Span) const noexcept
        -> ExpressionResult<Selection> {
        return std::unexpected(ExpressionNotAdmitted {});
    }

    auto finish_index(
        ConstructionTypeRef type,
        bool owns_elements,
        IndexBoundsPolicy bounds,
        Value receiver,
        Value index,
        Span span
    ) noexcept -> Value {
        const auto takeable = receiver.takeable && owns_elements;
        auto value = make(
            type,
            SemIndex {
                .source = OwnedSemanticExpression(std::move(receiver.expression)),
                .index = OwnedSemanticExpression(std::move(index.expression)),
                .bounds = bounds,
                .source_owns_elements = owns_elements
            },
            span
        );
        value.takeable = takeable;
        return value;
    }

    auto finish_field(
        ConstructionTypeRef type,
        FieldProjection field,
        Value receiver,
        Span span
    ) noexcept -> Value {
        const auto takeable = receiver.takeable;
        auto value = make(
            type,
            SemField {
                .source = OwnedSemanticExpression(std::move(receiver.expression)),
                .field = field
            },
            span
        );
        value.takeable = takeable;
        return value;
    }

    auto associated_call(
        const ASTCallExpr& source,
        const ASTMemberExpr& member,
        StructID owner,
        Span span
    ) noexcept -> ExpressionTask<Value> {
        const auto function = scope.resolve_class_operation(
            owner,
            spelling(member.name_span),
            false,
            member.name_span
        );
        if (!function) {
            co_return std::unexpected(function.error());
        }
        co_return co_await call_function(*function, source, std::nullopt, span);
    }

    auto member_call(
        const ASTCallExpr& source,
        const ASTMemberExpr& member,
        Value receiver,
        Span span
    ) noexcept -> ExpressionTask<Value> {
        const auto receiver_type = type(receiver);
        if (const auto* concrete = std::get_if<TypeID>(&receiver_type)) {
            const auto canonical = program.type_copy(*concrete);
            if (const auto* record = std::get_if<StructTypeValue>(&canonical.value)) {
                const auto declaration =
                    program.construction_struct_declaration_copy(record->structure);
                const auto name = spelling(member.name_span);
                const auto field =
                    std::ranges::find_if(declaration.fields, [&](const auto& value) noexcept {
                        return program.spelling(value.name) == name;
                    });
                if (declaration.kind == RecordKind::Class && field == declaration.fields.end()) {
                    const auto function = scope.resolve_class_operation(
                        record->structure,
                        name,
                        true,
                        member.name_span
                    );
                    if (!function) {
                        co_return std::unexpected(function.error());
                    }
                    co_return co_await call_function(*function, source, std::move(receiver), span);
                }
            }
        }
        co_return std::unexpected(ExpressionNotAdmitted {});
    }

    auto generic_call(
        const ASTCallExpr& source,
        GenericDeclarationID definition,
        const ASTTypeApplicationExpr* application,
        Span span
    ) noexcept -> ExpressionTask<Value> {
        auto completed = co_await scope.construction_requests()
                             .ensure_generic_function(definition, source_module_id, span);
        if (!completed) {
            co_return std::unexpected(completed.error());
        }
        const auto function =
            std::get<GenericFunctionDefinition>(program.generic_declaration_copy(definition));
        if (!function.is_const) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::ConstAdmission,
                "compile-time execution can only call an explicitly declared const fn"
            ));
        }
        if (source.arguments.size() != function.signature.parameters.size()) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::TypeCallArity,
                "generic call argument count differs from its declaration"
            ));
        }
        auto inferred =
            std::vector<std::optional<GenericArgument>>(function.contract.parameters.size());
        auto explicit_environment = GenericEnvironment();
        auto explicit_types = CanonicalGenericEnvironment();
        if (application) {
            auto resolved = co_await scope.resolve_generic_arguments(
                application->arguments,
                function.contract.parameter_kinds
            );
            if (!resolved) {
                co_return std::unexpected(resolved.error());
            }
            explicit_environment = std::move(*resolved);
            for (auto i = 0uz; i < inferred.size(); ++i) {
                inferred[i] = explicit_environment[i];
            }
            if (const auto canonical =
                    program.normalize_generic_environment(explicit_environment)) {
                explicit_types = *canonical;
            }
        }
        auto effects = program.make_failure_instantiation(definition, explicit_environment);
        auto inputs = std::vector<Value>();
        for (auto i = 0uz; i < source.arguments.size(); ++i) {
            const auto operand = call_argument_operand(ast, source.arguments[i]);
            const auto& parameter = function.signature.parameters[i];
            if (operand.access != parameter.access) {
                co_return std::unexpected(fail(
                    span,
                    DiagnosticCode::AccessCallMismatch,
                    "constant call argument access does not match the generic parameter"
                ));
            }
            auto expected = std::optional<ConstructionTypeRef>();
            if (application) {
                if (explicit_types.size() == inferred.size()) {
                    auto resolved = program.substitute_function_type(
                        parameter.type,
                        definition,
                        explicit_types,
                        program.append_source_origin(program.module_source(source_module_id), span),
                        &effects
                    );
                    if (!resolved) {
                        co_return std::unexpected(resolved.error());
                    }
                    expected = *resolved;
                } else {
                    expected = program.substitute_generic_type(
                        parameter.type,
                        definition,
                        explicit_environment,
                        &effects
                    );
                }
            } else if (std::holds_alternative<TypeID>(parameter.type)) {
                expected = parameter.type;
            }
            auto value = co_await read(operand.expression, expected);
            if (!value) {
                co_return std::unexpected(value.error());
            }
            if (const auto* formal = std::get_if<GenericTypeID>(&parameter.type);
                formal && !application) {
                if (!program.infer_generic_argument(
                        *formal,
                        type(*value),
                        definition,
                        inferred,
                        program.append_source_origin(program.module_source(source_module_id), span)
                    )) {
                    co_return std::unexpected(fail(
                        span,
                        DiagnosticCode::TypeGenericArguments,
                        "call arguments do not determine a consistent generic type environment"
                    ));
                }
            }
            inputs.push_back(std::move(*value));
        }
        auto environment = GenericEnvironment();
        for (const auto& argument : inferred) {
            if (!argument) {
                co_return std::unexpected(fail(
                    span,
                    DiagnosticCode::TypeGenericArguments,
                    "generic parameter cannot be inferred; supply explicit arguments"
                ));
            }
            environment.push_back(*argument);
        }
        auto closed = co_await scope.construction_requests()
                          .close_generic_environment(environment, source_module_id, span);
        if (!closed) {
            co_return std::unexpected(closed.error());
        }
        auto types = std::move(*closed);
        auto target = program.instantiate_generic_function(
            definition,
            types,
            program.append_source_origin(program.module_source(source_module_id), span)
        );
        if (!target) {
            co_return std::unexpected(target.error());
        }
        const auto contract = program.construction_callable_contract_copy(*target);
        auto arguments = std::vector<SemCallArgument>();
        for (auto i = 0uz; i < inputs.size(); ++i) {
            auto converted = convert_argument(inputs[i], contract.parameters[i].type, span);
            if (!converted) {
                co_return std::unexpected(converted.error());
            }
            auto argument =
                consume_argument(std::move(inputs[i]), contract.parameters[i].access, span);
            if (!argument) {
                co_return std::unexpected(argument.error());
            }
            arguments.push_back(
                {.access = contract.parameters[i].access, .expression = std::move(*argument)}
            );
        }
        co_return co_await finish_call(*target, std::move(arguments), span, source.callee);
    }

    auto call(const ASTCallExpr& source, Span span) noexcept -> ExpressionTask<Value> {
        auto callee = source.callee;
        while (const auto* group = std::get_if<ASTGroupExpr>(&ast.expression(callee).value)) {
            callee = group->expression;
        }
        const auto* application =
            std::get_if<ASTTypeApplicationExpr>(&ast.expression(callee).value);
        if (application) {
            callee = application->operand_id;
        }
        const auto* name = std::get_if<ASTNameExpr>(&ast.expression(callee).value);
        if (!name) {
            co_return std::unexpected(ExpressionNotAdmitted {});
        }
        auto selected =
            co_await scope.resolve_static_callable(spelling(name->name_span), name->name_span);
        if (!selected) {
            co_return std::unexpected(selected.error());
        }
        if (!*selected) {
            co_return std::unexpected(ExpressionNotAdmitted {});
        }
        if (const auto* generic = std::get_if<GenericDeclarationID>(&**selected)) {
            co_return co_await generic_call(source, *generic, application, span);
        }
        if (application) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::TypeGenericArguments,
                "type arguments require a generic function or a nominal type qualifier"
            ));
        }
        if (const auto* intrinsic = std::get_if<AsyncIntrinsic>(&**selected)) {
            co_return construct_async_intrinsic(*this, source, *intrinsic, span);
        }
        if (const auto* policy = std::get_if<AsyncCompositionPolicy>(&**selected)) {
            co_return co_await construct_async_composition(*this, source, *policy, span);
        }
        if (const auto* contract = std::get_if<TimeoutContract>(&**selected)) {
            co_return co_await construct_async_timeout(*this, source, *contract, span);
        }
        co_return co_await call_function(
            std::get<FunctionID>(**selected),
            source,
            std::nullopt,
            span
        );
    }

    auto call_function(
        FunctionID function,
        const ASTCallExpr& source,
        std::optional<Value> receiver,
        Span span
    ) noexcept -> ExpressionTask<Value> {
        auto completed = co_await scope.construction_requests()
                             .ensure_function_signature(function, source_module_id, span);
        if (!completed) {
            co_return std::unexpected(completed.error());
        }
        const auto declaration = program.function_declaration_copy(function);
        if (!declaration.is_const) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::ConstAdmission,
                "compile-time execution can only call an explicitly declared const fn"
            ));
        }
        const auto contract = program.construction_callable_contract_copy(declaration.callable);
        const auto offset = receiver.has_value() ? 1uz : 0uz;
        if (contract.parameters.size() != source.arguments.size() + offset) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::TypeCallArity,
                "function call has the wrong number of arguments"
            ));
        }
        auto arguments = std::vector<SemCallArgument>();
        if (receiver) {
            const auto& parameter = contract.parameters.front();
            auto checked = convert_argument(*receiver, parameter.type, span);
            if (!checked) {
                co_return std::unexpected(checked.error());
            }
            auto argument = consume_argument(std::move(*receiver), parameter.access, span);
            if (!argument) {
                co_return std::unexpected(argument.error());
            }
            arguments.push_back({.access = parameter.access, .expression = std::move(*argument)});
        }
        for (auto index = 0uz; index < source.arguments.size(); ++index) {
            const auto& source_argument = source.arguments[index];
            const auto& parameter = contract.parameters[index + offset];
            const auto selected = call_argument_operand(ast, source_argument);
            if (selected.access != parameter.access) {
                co_return std::unexpected(fail(
                    span,
                    DiagnosticCode::AccessCallMismatch,
                    "constant call argument access does not match the parameter"
                ));
            }
            auto argument = (co_await read(selected.expression, parameter.type));
            if (!argument) {
                co_return std::unexpected(argument.error());
            }
            auto checked =
                convert_argument(*argument, parameter.type, ast.expression(source_argument).span);
            if (!checked) {
                co_return std::unexpected(checked.error());
            }
            auto value = consume_argument(
                std::move(*argument),
                selected.access,
                ast.expression(source_argument).span
            );
            if (!value) {
                co_return std::unexpected(value.error());
            }
            arguments.push_back({.access = selected.access, .expression = std::move(*value)});
        }
        co_return co_await finish_call(
            declaration.callable,
            std::move(arguments),
            span,
            source.callee
        );
    }

    auto finish_call(
        CallableID target,
        std::vector<SemCallArgument> arguments,
        Span span,
        ASTExprID source_callee
    ) noexcept -> ExpressionTask<Value> {
        const auto contract = program.construction_callable_contract_copy(target);
        auto dependencies = std::vector<ConstructionTypeRef> {contract.result};
        for (const auto& parameter : contract.parameters) {
            dependencies.push_back(parameter.type);
        }
        // Inferred failure types are prepared with their declarations and bodies.
        // Closed instance contracts can also name explicit failure arguments.
        if (const auto* closed = std::get_if<FailureSetID>(&contract.failures)) {
            for (const auto type : program.failure_set_copy(*closed).members) {
                dependencies.push_back(type);
            }
        }
        for (const auto type : dependencies) {
            auto completed =
                co_await scope.construction_requests().ensure_type(type, source_module_id, span);
            if (!completed) {
                co_return std::unexpected(completed.error());
            }
        }
        const auto callee_type =
            program.intern_type({.value = FunctionTypeValue {.callable = target}});
        scope.observe_expression(source_callee, callee_type);
        auto callee = make(callee_type, SemCallable {.callable = target}, span);
        if (contract.execution == CallableExecutionKind::Async) {
            co_return make(
                make_operation_type(program, contract.result, contract.failures),
                SemColdCall {
                    .callee = OwnedSemanticExpression(std::move(callee.expression)),
                    .target = target,
                    .arguments = std::move(arguments),
                },
                span
            );
        }
        pending_failures.push_back(contract.failures);
        co_return make(
            contract.result,
            SemCall {
                .callee = OwnedSemanticExpression(std::move(callee.expression)),
                .target = target,
                .arguments = std::move(arguments),
                .callee_failures = BodyFailures(contract.failures)
            },
            span
        );
    }

    auto await_expression(const ASTPrefixExpr& source, Span span) noexcept
        -> ExpressionTask<Value> {
        auto operand = co_await read(source.operand_id, std::nullopt);
        if (!operand) {
            co_return std::unexpected(operand.error());
        }
        const auto operand_type = type(*operand);
        const auto operation = operation_shape(program, operand_type);
        if (!operation) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::AsyncAdmission,
                "await requires a cold operation in compile-time execution"
            ));
        }
        auto value = consume_argument(std::move(*operand), AccessMode::Take, span);
        if (!value) {
            co_return std::unexpected(value.error());
        }
        pending_failures.push_back(operation->failures);
        co_return make(
            operation->success,
            SemAwait {
                .operand = OwnedSemanticExpression(std::move(*value)),
                .operand_kind = AsyncAwaitOperandKind::ColdOperation,
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
                          || std::same_as<Form, ASTTypeApplicationExpr>
                          || std::same_as<Form, ASTContextualCaseExpr>
                          || std::same_as<Form, ASTPrefixExpr>
                          || std::same_as<Form, ASTRangeExpr>
                          || std::same_as<Form, ASTBinaryExpr>
                          || std::same_as<Form, ASTCastExpr>
                          || std::same_as<Form, ASTInterpolationExpr>
                          || std::same_as<Form, ASTConstructionExpr>
                          || std::same_as<Form, ASTTupleExpr>
                          || std::same_as<Form, ASTTupleElementExpr>
                          || std::same_as<Form, ASTArrayExpr>
                          || std::same_as<Form, ASTArrayRepeatExpr>
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

    auto complete_root(const Value& value) noexcept -> void {
        if (!pending_failures.empty()) {
            program.require_empty_failures(
                program.add_union_failure_term(std::move(pending_failures)),
                value.expression.origin,
                EmptyFailureRequirementKind::OrdinaryConsumption
            );
        }
    }

    auto evaluate(Value& value) noexcept -> ExpressionTask<ExecutionValue> {
        complete_root(value);
        // Type construction still needs local constants before body staging.
        // Keep that pre-read at the scope boundary until types carry static inputs.
        if constexpr (requires { scope.resolve_static_references(value.expression); }) {
            auto closed = co_await scope.resolve_static_references(value.expression);
            if (!closed) {
                co_return std::unexpected(closed.error());
            }
            if (!*closed) {
                co_return std::unexpected(ExpressionNotAdmitted {});
            }
        }
        co_return co_await scope.construction_requests().stage().evaluate(value.expression);
    }

private:
    static auto root_lifetime(
        ProgramDraft& program,
        ProgramModuleID module_id,
        Scope& scope,
        Span span
    ) noexcept -> LifetimeRegionID {
        if constexpr (requires { scope.static_expression_lifetime(); }) {
            return scope.static_expression_lifetime();
        } else {
            auto lifetimes = MutableBodyTable<LifetimeRegion, LifetimeRegionID>(
                program.create_evaluation_root_identity()
            );
            return lifetimes.add({
                .parent = std::nullopt,
                .kind = LifetimeRegionKind::FullExpression,
                .origin = program.append_source_origin(program.module_source(module_id), span),
            });
        }
    }

    template<typename Operation>
    auto make(ConstructionTypeRef type, Operation operation, Span span) noexcept -> Value {
        auto known = std::optional<ConstantID>();
        if constexpr (std::same_as<Operation, SemConstant>) {
            known = operation.constant;
        }
        auto expression = SemanticExpression {
            .type = BodyType(type),
            .lifetime = lifetime,
            .origin = program.append_source_origin(program.module_source(source_module_id), span),
            .constant = known,
            .failures = empty_failures,
            .exits_test = false,
            .operation_reachable = true,
            .category = SemanticValueCategory::Value,
            .value = std::move(operation),

        };
        return {.expression = std::move(expression), .takeable = true};
    }

public:
    auto read_static_argument(
        ASTExprID expression,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Value> {
        co_return co_await read_argument(expression, expected);
    }

    auto read_argument(ASTExprID expression, std::optional<ConstructionTypeRef> expected) noexcept
        -> ExpressionTask<Value> {
        co_return (co_await read_value_argument(*this, expression, expected));
    }

private:
    ExecutionTypeShapes shapes;
    ProgramDraft& program;
    ProgramModuleID source_module_id;
    ASTView ast;
    Scope& scope;
    LifetimeRegionID lifetime;
    BodyFailures empty_failures;
    std::vector<ConstructionFailureRef> pending_failures;
    std::size_t aggregate_work = 0uz;

    struct AggregateDepth final {
        std::size_t& depth;
        bool entered;

        ~AggregateDepth() noexcept { depth -= entered; }
    };

    std::size_t aggregate_depth = 0uz;
};

template<typename Scope>
auto build_static_expression(
    ProgramDraft& draft,
    ProgramModuleID module_id,
    ASTView syntax,
    Scope& scope,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<SemanticExpression> {
    auto site = StaticRootSite(draft, module_id, syntax, scope, syntax.expression(expression).span);
    auto result = co_await site.read(expression, expected);
    if (!result) {
        co_return std::unexpected(result.error());
    }
    if (expected) {
        auto checked =
            site.convert_argument(*result, *expected, syntax.expression(expression).span);
        if (!checked) {
            co_return std::unexpected(checked.error());
        }
    }
    site.complete_root(*result);
    co_return std::move(result->expression);
}

template<typename Scope>
auto evaluate_constant_expression(
    ProgramDraft& draft,
    ProgramModuleID module_id,
    ASTView syntax,
    Scope& scope,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected = std::nullopt
) noexcept -> ExpressionTask<ConstantID> {
    auto site = StaticRootSite(draft, module_id, syntax, scope, syntax.expression(expression).span);
    auto result = (co_await site.read(expression, expected));
    if (!result.has_value()) {
        co_return std::unexpected(result.error());
    }
    if (expected) {
        auto checked =
            site.convert_argument(*result, *expected, syntax.expression(expression).span);
        if (!checked) {
            co_return std::unexpected(checked.error());
        }
    }
    auto evaluated = (co_await site.evaluate(*result));
    if (!evaluated) {
        co_return std::unexpected(evaluated.error());
    }
    if (const auto constant = freeze_constant_value(draft, std::move(*evaluated))) {
        if (expected && !type_shapes_compatible(draft, draft.constant(*constant).type, *expected)) {
            co_return std::unexpected(site.fail(
                syntax.expression(expression).span,
                DiagnosticCode::TypeMismatch,
                "expression has an incompatible type"
            ));
        }
        co_return *constant;
    }
    co_return std::unexpected(ExpressionNotAdmitted {});
}

template<typename Scope>
auto evaluate_array_extent(
    ProgramDraft& draft,
    ProgramModuleID module_id,
    ASTView syntax,
    Scope& scope,
    ASTExprID expression
) noexcept -> AnalysisTask<std::uint64_t> {
    auto value = co_await evaluate_constant_expression(draft, module_id, syntax, scope, expression);
    if (!value) {
        if (const auto* diagnostic = std::get_if<AnalysisFailure>(&value.error())) {
            co_return std::unexpected(*diagnostic);
        }
    }
    const auto* integer =
        value ? std::get_if<IntegerConstant>(&draft.constant(*value).value) : nullptr;
    const auto fail = [&](Span span, DiagnosticCode code, std::string message) noexcept {
        return draft.diagnostics().error(DiagnosticBuilder(code, std::move(message))
                                             .primary(locate(syntax.source_id(), span))
                                             .build());
    };
    const auto span = syntax.expression(expression).span;
    if (integer == nullptr) {
        co_return std::unexpected(
            fail(span, DiagnosticCode::ConstArrayExtent, "array extent must be a constant integer")
        );
    }
    if (integer->negative()) {
        co_return std::unexpected(
            fail(span, DiagnosticCode::ConstNegativeArrayExtent, "array extent cannot be negative")
        );
    }
    co_return integer->magnitude();
}

template<typename Scope>
auto StaticRootSite<Scope>::resolve_array_extent(ASTExprID expression) noexcept
    -> AnalysisTask<std::uint64_t> {
    co_return (co_await evaluate_array_extent(program, source_module_id, ast, scope, expression));
}
