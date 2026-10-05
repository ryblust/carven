module carven:semantic.analysis.body.call.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.expr_site;
import :semantic.analysis.body.resolve;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.interpret;
import :semantic.analysis.expr.operand;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.types.display;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.evaluation.operation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.unique_indirect;
import :support.visit;
import std;

auto BodyElaborator::callable_contract(ConstructionTypeRef type, Span span) noexcept
    -> AnalysisResult<ConstructionCallableContract> {
    if (auto view = construct_callable_view_contract(draft(), type)) {
        return *view;
    }
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        const auto canonical = draft().type_copy(*concrete);
        auto callable = std::optional<CallableID>();
        canonical.value.visit(
            Overloaded {
                [&](const FunctionTypeValue& value) noexcept { callable = value.callable; },
                [&](const ClosureTypeValue& value) noexcept { callable = value.callable; },
                []<typename Value>(const Value&) static noexcept {
                    static_assert(
                        std::same_as<Value, BuiltinTypeValue>
                            || std::same_as<Value, StructTypeValue>
                            || std::same_as<Value, EnumTypeValue>
                            || std::same_as<Value, ArrayTypeValue>
                            || std::same_as<Value, CallableViewTypeValue>
                            || std::same_as<Value, CppTypeValue>
                            || std::same_as<Value, PointerTypeValue>
                            || std::same_as<Value, SliceTypeValue>
                            || std::same_as<Value, RangeTypeValue>,
                        "unhandled non-owning callable type"
                    );
                },
            }
        );
        if (callable.has_value()) {
            return draft().construction_callable_contract_copy(*callable);
        }
    }
    return std::unexpected(
        fail(span, DiagnosticCode::TypeNotCallable, "expression is not callable")
    );
}

auto BodyElaborator::build_call_argument(
    ASTExprID source_id,
    AccessMode access_mode,
    std::optional<ConstructionTypeRef> expected,
    std::optional<DiagnosticCode> mismatch_code,
    ParameterStage stage
) noexcept -> AnalysisTask<BuiltCallArgument> {
    const auto& source = ast.expression(source_id);
    const auto selected = call_argument_operand(ast, source_id);
    if (selected.access != access_mode) {
        co_return std::unexpected(fail(
            source.span,
            DiagnosticCode::AccessCallMismatch,
            access_mode == AccessMode::Write ? "Write parameter requires an explicit Write argument"
                : access_mode == AccessMode::Take
                ? "Take parameter requires an explicit Take argument"
                : "argument access marker differs from the parameter"
        ));
    }
    auto built = stage == ParameterStage::Static
        ? co_await static_expression(selected.expression, expected)
        : co_await expression(selected.expression, expected);
    if (!built.has_value()) {
        co_return std::unexpected(built.error());
    }
    co_return bind_call_argument(
        std::move(*built),
        access_mode,
        expected,
        source.span,
        mismatch_code
    );
}

auto BodyElaborator::bind_call_argument(
    BuiltExpression built,
    AccessMode access_mode,
    std::optional<ConstructionTypeRef> expected,
    Span span,
    std::optional<DiagnosticCode> mismatch_code
) noexcept -> AnalysisResult<BuiltCallArgument> {
    const auto type = expected ? *expected : built.type();
    if (mismatch_code && !is_cpp_type(built.type()) && !compatible(built.type(), type)) {
        return std::unexpected(fail(
            span,
            *mismatch_code,
            std::format(
                "argument of type '{}' does not match parameter type '{}'",
                type_display_name(draft(), built.type()),
                type_display_name(draft(), type)
            )
        ));
    }
    auto pending_failures = take_pending_failures(built);
    if (access_mode == AccessMode::Write) {
        auto compatible_storage = require_invariant_type(built.type(), type, span);
        if (!compatible_storage.has_value()) {
            return std::unexpected(compatible_storage.error());
        }
        auto place = consume_place(built, span);
        if (!place.has_value()) {
            return std::unexpected(place.error());
        }
        if (place->expression.type.construction() != type
            && (is_cpp_type(place->expression.type.construction()) || is_cpp_type(type))) {
            *place = active_builder().cpp_place(
                std::move(*place),
                type,
                CppConvertOperation {.explicit_cast = false},
                {},
                origin(span)
            );
        }
        return BuiltCallArgument {
            .argument = SemCallArgument {AccessMode::Write, std::move(place->expression)},
            .pending_failures = std::move(pending_failures),
            .completes = built.completes,
        };
    }
    if (access_mode == AccessMode::Take && pointer_shape(draft(), type)) {
        if (auto checked = require_invariant_type(built.type(), type, span); !checked) {
            return std::unexpected(checked.error());
        }
    }
    // Declaration references acquire callable storage during conversion.
    if (access_mode == AccessMode::Take && !built.is_function_reference()) {
        auto taken = consume_value(built, span, AccessMode::Take);
        if (!taken) {
            return std::unexpected(taken.error());
        }
        *built.storage = std::move(*taken);
    }
    auto coerced = coerce_to(built, type, span);
    if (!coerced.has_value()) {
        return std::unexpected(coerced.error());
    }
    auto value = consume_value(built, span, AccessMode::Read);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    return BuiltCallArgument {
        .argument = {access_mode, std::move(*value)},
        .pending_failures = std::move(pending_failures),
        .completes = built.completes,
    };
}

auto BodyElaborator::enum_case_reference(
    TypeID enumeration_type,
    std::string_view case_name,
    Span span,
    Span case_span
) noexcept -> AnalysisTask<BuiltExpression> {
    auto site = BodyExprSite(*this);
    co_return require_body_expression((
        co_await interpret_enum_case(site, enumeration_type, case_name, case_span, {}, span, false)
    ));
}

auto BodyElaborator::call_expression(
    const ASTCallExpr& source,
    Span span,
    std::optional<SelectedExpression> prepared_callee,
    std::optional<BuiltExpression> receiver
) noexcept -> AnalysisTask<BuiltExpression> {
    const auto& callee_source = ast.expression(source.callee);
    if (const auto* name = std::get_if<ASTNameExpr>(&callee_source.value)) {
        const auto text = spelling(name->name_span);
        if (find_local(text) == nullptr && !catalog().lookup(source_module_id, text).empty()) {
            auto selected = (co_await find_global(text, name->name_span));
            if (!selected.has_value()) {
                co_return std::unexpected(selected.error());
            }
            if (const auto* enum_case = std::get_if<CatalogEnumCaseForm>(&(*selected)->form)) {
                const auto type = draft().intern_type(
                    CanonicalType {
                        .value = EnumTypeValue {.enumeration = enum_case->owner},
                    }
                );
                auto site = BodyExprSite(*this);
                co_return require_body_expression((co_await interpret_enum_case(
                    site,
                    type,
                    text,
                    name->name_span,
                    source.arguments,
                    span,
                    true
                )));
            }
        }
    }
    auto selected_callee = co_await [&]() noexcept -> AnalysisTask<SelectedExpression> {
        if (prepared_callee.has_value()) {
            co_return std::move(*prepared_callee);
        }
        co_return (co_await select_expression(source.callee));
    }();
    if (!selected_callee.has_value()) {
        co_return std::unexpected(selected_callee.error());
    }
    if (auto* builtin = std::get_if<BuiltinSelection>(&*selected_callee)) {
        builtin->span = span;
        if (builtin->function == BuiltinFunction::Addressof) {
            if (source.arguments.size() != 1uz) {
                co_return std::unexpected(fail(
                    span,
                    DiagnosticCode::TypeCallArity,
                    "addressof requires exactly one argument"
                ));
            }
            const auto argument_id = source.arguments.front().expression;
            const auto selected = call_argument_operand(ast, argument_id);
            if (selected.access == AccessMode::Take) {
                co_return std::unexpected(fail(
                    ast.expression(argument_id).span,
                    DiagnosticCode::AccessCallMismatch,
                    "addressof cannot take its argument"
                ));
            }
            auto argument = (co_await expression(selected.expression));
            if (!argument) {
                co_return std::unexpected(argument.error());
            }
            auto pending = take_pending_failures(*argument);
            auto place = consume_place(*argument, ast.expression(argument_id).span);
            if (!place) {
                co_return std::unexpected(place.error());
            }
            if (selected.access == AccessMode::Write && place->access != AccessMode::Write) {
                co_return std::unexpected(fail(
                    ast.expression(argument_id).span,
                    DiagnosticCode::AccessImmutable,
                    "addressof Write requires writable storage"
                ));
            }
            const auto target = place->expression.type.construction();
            const auto access =
                selected.access == AccessMode::Write ? PointerAccess::Write : PointerAccess::Read;
            const auto* concrete = std::get_if<TypeID>(&target);
            const auto pointer = concrete
                ? ConstructionTypeRef {draft().intern_type(
                      {.value = PointerTypeValue {.target = *concrete, .access = access}}
                  )}
                : ConstructionTypeRef {draft().append_construction_type(
                      {.value = ConstructionPointerTypeValue {.target = target, .access = access}}
                  )};
            auto result = make_built(
                pointer,
                SemAddressOf {.source = UniqueIndirect(std::move(place->expression))},
                span,
                std::move(pending)
            );
            result.completes = argument->completes;
            co_return result;
        }
        auto argument_spans = std::vector<Span>();
        auto arguments = std::vector<SemCallArgument>();
        auto parameters = std::vector<ConstructionCallableParameter>();
        auto pending = BodyPendingFailureTerms();
        auto completes = true;
        auto report_condition_completes = true;
        const auto conditional_report = builtin->function == BuiltinFunction::Assert
            || builtin->function == BuiltinFunction::Check
            || builtin->function == BuiltinFunction::Require;
        for (const auto& argument : source.arguments) {
            const auto condition = arguments.empty() && conditional_report;
            if (condition) {
                builtin->condition_source = draft().intern_spelling(
                    draft().source_slice_copy(
                        source_module_id,
                        ast.expression(argument.expression).span
                    )
                );
                auto condition_id = argument.expression;
                while (const auto* group =
                           std::get_if<ASTGroupExpr>(&ast.expression(condition_id).value)) {
                    condition_id = group->expression;
                }
                if (const auto* binary =
                        std::get_if<ASTBinaryExpr>(&ast.expression(condition_id).value)) {
                    const auto capture = [&](ASTExprID id) noexcept {
                        return draft().intern_spelling(
                            draft().source_slice_copy(source_module_id, ast.expression(id).span)
                        );
                    };
                    builtin->operand_sources =
                        std::array {capture(binary->left), capture(binary->right)};
                }
            }
            const auto argument_span = ast.expression(argument.expression).span;
            argument_spans.push_back(argument_span);
            auto built = (co_await build_call_argument(
                argument.expression,
                AccessMode::Read,
                condition
                    ? std::optional<ConstructionTypeRef>(draft().builtin_type(BuiltinType::Bool))
                    : std::nullopt,
                condition ? std::optional(
                                builtin->function == BuiltinFunction::Assert
                                    ? DiagnosticCode::TypeConditionBool
                                    : DiagnosticCode::TestConditionType
                            )
                          : std::nullopt
            ));
            if (!built) {
                co_return std::unexpected(built.error());
            }
            parameters.push_back(
                {.stage = ParameterStage::Runtime,
                 .access = AccessMode::Read,
                 .type = built->argument.expression.type.construction()}
            );
            append_pending_failures(pending, built->pending_failures);
            if (condition) {
                report_condition_completes = built->completes;
            }
            if (!conditional_report || condition) {
                completes &= built->completes;
            }
            arguments.push_back(std::move(built->argument));
        }
        auto valid = validate_builtin(*builtin, parameters, argument_spans);
        if (!valid) {
            co_return std::unexpected(valid.error());
        }
        auto operation = builtin_operation(active_builder(), *builtin, std::move(arguments));
        if (std::holds_alternative<SemReport>(operation.value)) {
            operation.operation_reachable = report_condition_completes;
        }
        co_return BuiltExpression {
            .storage = UniqueIndirect {BodyExpressionStorage {std::move(operation)}},
            .pending_failures = std::move(pending),
            .takeable = true,
            .completes = completes && builtin->function != BuiltinFunction::Fail
        };
    }
    if (std::holds_alternative<CppSelection>(*selected_callee)
        || is_cpp_type(std::get<BuiltExpression>(*selected_callee).type())) {
        co_return (co_await cpp_call(std::move(*selected_callee), source, span));
    }
    auto callee =
        std::optional<BuiltExpression>(std::move(std::get<BuiltExpression>(*selected_callee)));
    const auto target = active_builder().known_callable(callee->expression());
    auto pending_failures = take_pending_failures(*callee);
    auto contract = callable_contract(callee->type(), ast.expression(source.callee).span);
    if (!contract.has_value()) {
        co_return std::unexpected(contract.error());
    }
    const auto offset = receiver.has_value() ? 1uz : 0uz;
    if (source.arguments.size() + offset != contract->parameters.size()) {
        const auto expected = contract->parameters.size() - offset;
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::TypeCallArity,
            std::format(
                "call expects {} argument{} but received {}",
                expected,
                expected == 1uz ? "" : "s",
                source.arguments.size()
            )
        );
        diagnostic.primary(locate(ast.source_id(), span));
        if (const auto function = target ? draft().function_for_callable(*target) : std::nullopt) {
            const auto* symbol = catalog().symbol(catalog().function_symbol(*function));
            diagnostic.related(
                locate(
                    draft().syntax_tree(symbol->module_id).view().source_id(),
                    symbol->declaration_span
                ),
                std::format("'{}' is declared here", symbol->name)
            );
        }
        co_return std::unexpected(draft().diagnostics().error(diagnostic.build()));
    }
    auto callee_operand = std::optional<SemanticExpression>();
    if (callee->is_function_reference()) {
        callee_operand = take_built(*callee, ast.expression(source.callee).span);
    } else {
        auto value = consume_value(*callee, ast.expression(source.callee).span, AccessMode::Read);
        if (!value.has_value()) {
            co_return std::unexpected(value.error());
        }
        callee_operand = std::move(*value);
    }
    auto completes = callee->completes;
    auto arguments = std::vector<SemCallArgument>();
    arguments.reserve(contract->parameters.size());
    if (receiver) {
        auto bound = bind_call_argument(
            std::move(*receiver),
            contract->parameters.front().access,
            contract->parameters.front().type,
            ast.expression(source.callee).span,
            std::nullopt
        );
        if (!bound) {
            co_return std::unexpected(bound.error());
        }
        append_pending_failures(pending_failures, bound->pending_failures);
        completes &= bound->completes;
        arguments.push_back(std::move(bound->argument));
    }
    for (auto index = 0uz; index < source.arguments.size(); ++index) {
        auto argument = (co_await build_call_argument(
            source.arguments[index].expression,
            contract->parameters[index + offset].access,
            contract->parameters[index + offset].type,
            std::nullopt,
            contract->parameters[index + offset].stage
        ));
        if (!argument.has_value()) {
            co_return std::unexpected(argument.error());
        }
        append_pending_failures(pending_failures, argument->pending_failures);
        completes &= argument->completes;
        arguments.push_back(std::move(argument->argument));
    }
    if (std::ranges::any_of(
            contract->parameters,
            [](const auto& parameter) static noexcept {
                return parameter.stage == ParameterStage::Static;
            }
        )
        && (!target || !draft().function_for_callable(*target))) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::ConstAdmission,
            "const-parameter call requires a directly identified Carven function"
        ));
    }
    const auto failures =
        target ? draft().construction_callable_contract_copy(*target).failures : contract->failures;
    append_pending_failures(pending_failures, BodyPendingFailureTerms {failures});
    auto result = active_builder().make_expression(
        contract->result,
        active_builder().lifetime(),
        origin(span),
        SemCall {
            .callee = UniqueIndirect(std::move(*callee_operand)),
            .target = target,
            .arguments = std::move(arguments),
            .callee_failures = BodyFailures(failures)
        }
    );
    co_return BuiltExpression {
        .storage = UniqueIndirect {BodyExpressionStorage {std::move(result)}},

        .pending_failures = std::move(pending_failures),
        .takeable = true,
        .completes = completes,
    };
}

auto BodyElaborator::class_operation(
    StructID owner,
    std::string_view name,
    bool receiver,
    Span span
) noexcept -> AnalysisTask<BuiltExpression> {
    const auto* symbol = catalog().symbol(catalog().struct_symbol(owner));
    const auto& form = std::get<CatalogStructForm>(symbol->form);
    for (const auto function : form.operations) {
        const auto* selected = catalog().symbol(catalog().function_symbol(function));
        if (selected->name != name) {
            continue;
        }
        const auto operation = *selected->class_operation;
        if (operation.visibility == MemberVisibility::Private && lexical_class != owner) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::AccessClassPrivate,
                "private operation is accessible only inside its defining class"
            ));
        }
        if (operation.receiver != receiver) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::TypeMethodCall,
                operation.receiver ? "instance operation requires a receiver"
                                   : "associated operation requires Type::name"
            ));
        }
        auto ready = (co_await batch->ensure_function_signature(function, source_module_id, span));
        if (!ready) {
            co_return std::unexpected(ready.error());
        }
        const auto declaration = draft().function_declaration_copy(function);
        auto callee = BuiltExpression {
            .storage = UniqueIndirect {BodyExpressionStorage {
                active_builder().callable_expression(declaration.callable, origin(span))
            }},
            .pending_failures = {},
            .takeable = false,
            .completes = true
        };
        co_return callee;
    }
    co_return std::unexpected(fail(
        span,
        DiagnosticCode::TypeMemberUnresolved,
        std::format("class has no operation named '{}'", name)
    ));
}

auto BodyElaborator::class_call(
    const ASTCallExpr& source,
    const ASTMemberExpr& member,
    StructID owner,
    std::optional<BuiltExpression> receiver,
    Span span
) noexcept -> AnalysisTask<BuiltExpression> {
    auto callee = (co_await class_operation(
        owner,
        spelling(member.name_span),
        receiver.has_value(),
        member.name_span
    ));
    if (!callee) {
        co_return std::unexpected(callee.error());
    }
    co_return (co_await call_expression(
        source,
        span,
        SelectedExpression(std::move(*callee)),
        std::move(receiver)
    ));
}
