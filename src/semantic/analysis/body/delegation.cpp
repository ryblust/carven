module carven:semantic.analysis.body.delegation.impl;

import :diagnostics.code;
import :frontend.ast.expr;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.constant.fold;
import :semantic.analysis.expr.operand;
import :semantic.analysis.names;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :support.invariant;
import :support.unique_indirect;
import :support.visit;
import std;

auto BodyElaborator::is_cpp_type(ConstructionTypeRef type) const noexcept -> bool {
    type = unwrap_type_alias(draft(), type);
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        return std::holds_alternative<CppTypeValue>(draft().type_copy(*concrete).value);
    }
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto shape = draft().generic_type_copy(*generic);
        return std::holds_alternative<GenericCppNamedType>(shape)
            || std::holds_alternative<GenericCppQueryType>(shape);
    }
    return false;
}

auto BodyElaborator::select_cpp_name(const ASTCppNameExpr& name, Span span) noexcept
    -> AnalysisResult<SelectedExpression> {
    auto reference = lookup_cpp_name(
        draft(),
        catalog(),
        import_usage(),
        source_module_id,
        {.global_root = name.global_root, .components = name.components},
        observe_sources ? &source_occurrences : nullptr
    );
    if (!reference.has_value()) {
        return std::unexpected(reference.error());
    }
    return CppSelection {.target = std::move(**reference), .span = span};
}

auto BodyElaborator::cpp_query_result_type(GenericCppQueryType query) noexcept
    -> ConstructionTypeRef {
    auto symbolic = false;
    visit_cpp_query_types(query, [&](ConstructionTypeRef type) noexcept {
        symbolic |= !std::holds_alternative<TypeID>(unwrap_type_alias(draft(), type));
    });
    if (symbolic) {
        return draft().intern_generic_type(std::move(query));
    }
    const auto concrete = map_cpp_query_types(query, [&](ConstructionTypeRef type) noexcept {
        return std::get<TypeID>(unwrap_type_alias(draft(), type));
    });
    return draft().intern_type({.value = CppTypeValue {.form = concrete}});
}

auto BodyElaborator::cpp_result_type(
    const CppOperation& operation,
    std::span<const BasicCppTypeOperand<ConstructionTypeRef>> operands
) noexcept -> ConstructionTypeRef {
    return cpp_query_result_type(cpp_query_type(operation, operands));
}

auto BodyElaborator::materialize_selection(
    SelectedExpression&& selected,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<BuiltExpression> {
    if (auto* built = std::get_if<BuiltExpression>(&selected)) {
        return std::move(*built);
    }
    if (const auto* composition = std::get_if<CompositionSelection>(&selected)) {
        return std::unexpected(fail(
            composition->span,
            DiagnosticCode::AsyncAdmission,
            "composition declarations require direct calls; callable values are not admitted"
        ));
    }
    if (const auto* timeout = std::get_if<TimeoutSelection>(&selected)) {
        return std::unexpected(
            fail(timeout->span, DiagnosticCode::AsyncAdmission, "timeout requires a direct call")
        );
    }
    if (const auto* intrinsic = std::get_if<AsyncIntrinsicSelection>(&selected)) {
        return std::unexpected(fail(
            intrinsic->span,
            DiagnosticCode::AsyncAdmission,
            "async intrinsics require a resolved direct call; first-class values are not admitted"
        ));
    }
    if (const auto* builtin = std::get_if<BuiltinSelection>(&selected)) {
        const auto view = expected ? draft().callable_shape(*expected) : std::nullopt;
        if (view && !view->owning_type) {
            return builtin_callable(*builtin, view->parameters);
        }
        return std::unexpected(fail(
            builtin->span,
            DiagnosticCode::TypeNotCallable,
            "builtin value requires an expected callable signature"
        ));
    }
    auto& selection = std::get<CppSelection>(selected);
    if (auto* name = std::get_if<CppNameReference>(&selection.target)) {
        return cpp_expression(CppNameOperation {.name = std::move(*name)}, {}, selection.span);
    }
    if (auto* member = std::get_if<CppStaticMemberCallee<ConstructionTypeRef>>(&selection.target)) {
        return cpp_expression(
            CppStaticMemberOperation {.type = member->type, .member = std::move(member->member)},
            {},
            selection.span
        );
    }
    auto& member = std::get<CppMemberSelection>(selection.target);
    return cpp_projection(
        std::move(member.receiver),
        CppMemberOperation {.name = std::move(member.member)},
        {},
        selection.span
    );
}

auto BodyElaborator::cpp_projection(
    BuiltExpression receiver,
    CppOperation operation,
    std::vector<SemCallArgument> operands,
    Span span
) noexcept -> AnalysisResult<BuiltExpression> {
    auto inputs = std::vector<BasicCppTypeOperand<ConstructionTypeRef>>();
    auto* place = std::get_if<PlaceExpression>(&*receiver.storage);
    inputs.push_back(
        {.type = receiver.type(), .access = place != nullptr ? place->access : AccessMode::Read}
    );
    for (const auto& operand : operands) {
        inputs.push_back(
            {.type = operand.expression.type.construction(), .access = operand.access}
        );
    }
    const auto type = cpp_result_type(operation, inputs);
    if (place != nullptr) {
        return BuiltExpression {
            .storage = UniqueIndirect {BodyExpressionStorage {active_builder().cpp_place(
                std::move(*place),
                type,
                std::move(operation),
                std::move(operands),
                origin(span)
            )}},

            .pending_failures = std::move(receiver.pending_failures),
            .takeable = false,
            .completes = receiver.completes
        };
    }
    auto pending = take_pending_failures(receiver);
    auto value = consume_value(receiver, span, AccessMode::Read);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    operands.insert(
        operands.begin(),
        {.access = AccessMode::Read, .expression = std::move(*value)}
    );
    auto result = cpp_expression(std::move(operation), std::move(operands), span, type);
    if (result.has_value()) {
        result->pending_failures = std::move(pending);
        result->completes = receiver.completes;
    }
    return result;
}

auto BodyElaborator::cpp_expression(
    CppOperation operation,
    std::vector<SemCallArgument> operands,
    Span span,
    std::optional<ConstructionTypeRef> type
) noexcept -> AnalysisResult<BuiltExpression> {
    if (type.has_value() && std::holds_alternative<CppConvertOperation>(operation)) {
        // A typed native producer constructs the operation directly. Its C++
        // return type must satisfy the source completion contract.
        if (operation_shape(*type)
            && operands.size() == 1uz
            && std::holds_alternative<SemCppCall>(operands.front().expression.value)) {
            auto value = std::move(operands.front().expression);
            value.type = BodyType(*type);
            return BuiltExpression {
                .storage = UniqueIndirect {BodyExpressionStorage {std::move(value)}},
                .pending_failures = {},
                .takeable = true,
                .completes = true,
            };
        }
        const auto shape = draft().callable_shape(*type);
        const auto borrowed = shape && !shape->owning_type;
        if (borrowed) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::TypeCallableViewEscape,
                "an undeclared C++ contract cannot establish a Carven callable borrow"
            ));
        }
    }
    const auto location = origin(span);
    auto result_type = type;
    if (!result_type.has_value()) {
        auto inputs = std::vector<BasicCppTypeOperand<ConstructionTypeRef>>();
        for (const auto& operand : operands) {
            inputs.push_back(
                {.type = operand.expression.type.construction(), .access = operand.access}
            );
        }
        if (const auto* construction = std::get_if<CppConstructOperation>(&operation)) {
            auto arguments = std::vector<BasicCppConstructArgument<ConstructionTypeRef>>();
            for (const auto& operand : operands) {
                auto constant = std::optional<ConstantID>();
                if (const auto* concrete =
                        std::get_if<TypeID>(&operand.expression.type.construction())) {
                    const auto shape = draft().type_copy(*concrete);
                    const auto* builtin = std::get_if<BuiltinTypeValue>(&shape.value);
                    if (builtin
                        && (builtin_is_numeric(builtin->kind)
                            || builtin->kind == BuiltinType::Bool
                            || builtin->kind == BuiltinType::Char)
                        && operand.access == AccessMode::Read
                        && !operand.expression.selects_storage()) {
                        constant = operand.expression.constant;
                    }
                }
                arguments.push_back(
                    {.operand =
                         {.type = operand.expression.type.construction(), .access = operand.access},
                     .constant = constant}
                );
            }
            result_type = cpp_query_result_type(
                {.expression = BasicCppConstructQuery<ConstructionTypeRef> {
                     .target = construction->target,
                     .arguments = std::move(arguments)
                 }}
            );
        } else {
            result_type = cpp_result_type(operation, inputs);
        }
    }
    auto value = active_builder().make_expression(
        *result_type,
        active_builder().lifetime(),
        location,
        SemCpp {.operation = std::move(operation), .operands = std::move(operands)}
    );
    return BuiltExpression {
        .storage = UniqueIndirect {BodyExpressionStorage {std::move(value)}},

        .pending_failures = {},
        .takeable = true,
        .completes = true,
    };
}

auto BodyElaborator::build_cpp_argument(ASTExprID source) noexcept
    -> AnalysisTask<BuiltCppArgument> {
    const auto selected = call_argument_operand(ast, source);
    auto built = (co_await expression(selected.expression, true));
    if (!built) {
        co_return std::unexpected(built.error());
    }
    const auto span = ast.expression(selected.expression).span;
    auto value = [&]() noexcept -> AnalysisResult<SemanticExpression> {
        if (selected.access == AccessMode::Write) {
            auto place = consume_place(*built, span);
            if (!place) {
                return std::unexpected(place.error());
            }
            return std::move(place->expression);
        }
        return consume_value(*built, span, selected.access);
    }();
    if (!value) {
        co_return std::unexpected(value.error());
    }
    co_return BuiltCppArgument {
        .argument = {.access = selected.access, .expression = std::move(*value)},
        .completes = built->completes,
    };
}

auto BodyElaborator::cpp_construct(
    const ASTConstructionExpr& source,
    ConstructionTypeRef target,
    Span span
) noexcept -> AnalysisTask<BuiltExpression> {
    auto operands = std::vector<SemCallArgument>();
    auto completes = true;
    if (const auto* values = std::get_if<ASTPositionalInitializerList>(&source.initializer.value)) {
        for (const auto value : values->values) {
            auto built = (co_await build_cpp_argument(value));
            if (!built) {
                co_return std::unexpected(built.error());
            }
            if (built->argument.access == AccessMode::Read) {
                auto resolved = co_await resolve_static_references(built->argument.expression);
                if (!resolved) {
                    co_return std::unexpected(resolved.error());
                }
                // C++ deduction needs scalar witnesses after type-time static
                // reads. Preserve operand effects in the original tree.
                auto expressions = std::vector<SemanticExpression*>();
                visit_semantic_nodes(
                    built->argument.expression,
                    [&](SemanticExpression& value) noexcept { expressions.push_back(&value); }
                );
                for (auto* expression : expressions | std::views::reverse) {
                    if (auto folded = fold_constant_expression(
                            draft(),
                            *expression,
                            ConstantFoldFailure::Diagnose
                        );
                        !folded) {
                        co_return std::unexpected(folded.error());
                    }
                }
            }
            completes &= built->completes;
            operands.push_back(std::move(built->argument));
        }
    } else if (std::holds_alternative<ASTFieldInitializerList>(source.initializer.value)) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::TypeConstructNotStruct,
            "named initializers require a Carven structure"
        ));
    }
    auto result =
        cpp_expression(CppConstructOperation {.target = target}, std::move(operands), span);
    if (result) {
        result->completes = completes;
    }
    co_return result;
}

auto BodyElaborator::cpp_call(
    SelectedExpression selected,
    const ASTCallExpr& source,
    Span span
) noexcept -> AnalysisTask<BuiltExpression> {
    using Operand = SemCppOperand;
    auto completes = true;
    const auto receiver = [&](BuiltExpression& built,
                              Span receiver_span) noexcept -> AnalysisResult<Operand> {
        completes &= built.completes;
        auto consumed = consume_pending(built, receiver_span);
        if (!consumed.has_value()) {
            return std::unexpected(consumed.error());
        }
        if (auto* place = std::get_if<PlaceExpression>(&*built.storage)) {
            const auto access = place->access;
            return Operand {
                .access = access,
                .expression = UniqueIndirect(std::move(place->expression))
            };
        }
        auto value = consume_value(built, receiver_span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        return Operand {
            .access = AccessMode::Read,
            .expression = UniqueIndirect(std::move(*value))
        };
    };
    auto callee = [&]() noexcept -> AnalysisResult<CppCallee<Operand>> {
        if (auto* selection = std::get_if<CppSelection>(&selected)) {
            if (auto* name = std::get_if<CppNameReference>(&selection->target)) {
                return std::move(*name);
            }
            if (auto* member =
                    std::get_if<CppStaticMemberCallee<ConstructionTypeRef>>(&selection->target)) {
                return std::move(*member);
            }
            auto& member = std::get<CppMemberSelection>(selection->target);
            auto value = (receiver(member.receiver, selection->span));
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            return CppMemberCallee<Operand> {
                .receiver = std::move(*value),
                .member = std::move(member.member)
            };
        }
        auto& built = std::get<BuiltExpression>(selected);
        completes &= built.completes;
        auto value = consume_value(built, ast.expression(source.callee).span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        return Operand {
            .access = AccessMode::Read,
            .expression = UniqueIndirect(std::move(*value))
        };
    }();
    if (!callee.has_value()) {
        co_return std::unexpected(callee.error());
    }
    auto arguments = std::vector<SemCallArgument>();
    for (const auto& argument : source.arguments) {
        auto built = (co_await build_cpp_argument(argument));
        if (!built) {
            co_return std::unexpected(built.error());
        }
        completes &= built->completes;
        arguments.push_back(std::move(built->argument));
    }

    auto call = SemCppCall {.callee = std::move(*callee), .arguments = std::move(arguments)};
    const auto query =
        cpp_call_query(call, [](ConstructionTypeRef type) static noexcept { return type; });
    const auto type = cpp_query_result_type(query);
    auto value =
        active_builder()
            .make_expression(type, active_builder().lifetime(), origin(span), std::move(call));
    co_return BuiltExpression {
        .storage = UniqueIndirect {BodyExpressionStorage {std::move(value)}},

        .pending_failures = {},
        .takeable = true,
        .completes = completes
    };
}
