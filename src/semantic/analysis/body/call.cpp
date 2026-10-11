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
import :semantic.analysis.expr.async;
import :semantic.analysis.expr.operand;
import :semantic.analysis.expr.scope;
import :semantic.analysis.names;
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
    if (auto contract = draft().callable_contract(type)) {
        return std::move(*contract);
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
            && (is_cpp_type(built.type()) || is_cpp_type(type))) {
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
    if (access_mode == AccessMode::Take && pointer_shape(draft(), type) && built.type() != type) {
        return std::unexpected(
            fail(span, DiagnosticCode::TypeMismatch, "Take requires the same complete ptr type")
        );
    }
    // Declaration references acquire callable storage during conversion.
    if (access_mode == AccessMode::Take && !built.is_function_reference()) {
        auto taken = consume_value(built, span, AccessMode::Take);
        if (!taken) {
            return std::unexpected(taken.error());
        }
        *built.storage = std::move(*taken);
    }
    if (access_mode == AccessMode::Read
        && (built.type() == type || (!is_cpp_type(built.type()) && !is_cpp_type(type)))) {
        if (auto* place = std::get_if<PlaceExpression>(&*built.storage);
            place && try_require_invariant_type(built.type(), type, span)) {
            return BuiltCallArgument {
                .argument = {AccessMode::Read, std::move(place->expression)},
                .pending_failures = std::move(pending_failures),
                .completes = built.completes,
            };
        }
    }
    auto coerced = coerce_to(built, type, span);
    if (!coerced.has_value()) {
        return std::unexpected(coerced.error());
    }
    if (access_mode == AccessMode::Read) {
        if (auto* place = std::get_if<PlaceExpression>(&*built.storage)) {
            return BuiltCallArgument {
                .argument = {AccessMode::Read, std::move(place->expression)},
                .pending_failures = std::move(pending_failures),
                .completes = built.completes,
            };
        }
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
    co_return require_body_expression(
        (co_await site.interpret_enum_case(enumeration_type, case_name, case_span, {}, span, false))
    );
}

auto BodyElaborator::call_expression(
    const ASTCallExpr& source,
    Span span,
    std::optional<SelectedExpression> prepared_callee,
    std::optional<BuiltExpression> receiver
) noexcept -> AnalysisTask<BuiltExpression> {
    auto generic_callee = source.callee;
    while (const auto* group = std::get_if<ASTGroupExpr>(&ast.expression(generic_callee).value)) {
        generic_callee = group->expression;
    }
    const auto* application =
        std::get_if<ASTTypeApplicationExpr>(&ast.expression(generic_callee).value);
    if (application) {
        generic_callee = application->operand_id;
    }
    if (const auto* name = std::get_if<ASTNameExpr>(&ast.expression(generic_callee).value)) {
        const auto text = spelling(name->name_span);
        if (!prepared_callee
            && !receiver
            && find_local(text) == nullptr
            && !catalog().lookup(source_module_id, text).empty()) {
            auto selected = co_await find_global(text, name->name_span);
            if (!selected) {
                co_return std::unexpected(selected.error());
            }
            if (const auto* generic = std::get_if<CatalogGenericForm>(&(*selected)->form)) {
                const auto declaration = draft().generic_declaration_copy(generic->definition);
                if (std::holds_alternative<GenericFunctionDefinition>(declaration)) {
                    co_return co_await generic_call_expression(
                        source,
                        generic->definition,
                        application ? std::span<const ASTGenericArgument>(application->arguments)
                                    : std::span<const ASTGenericArgument>(),
                        span
                    );
                }
            }
        }
    }
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
                co_return require_body_expression((co_await site.interpret_enum_case(
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
    if (const auto* composition = std::get_if<CompositionSelection>(&*selected_callee)) {
        auto site = BodyExprSite(*this);
        co_return require_body_expression(
            co_await construct_async_composition(site, source, composition->policy, span)
        );
    }
    if (const auto* timeout = std::get_if<TimeoutSelection>(&*selected_callee)) {
        auto site = BodyExprSite(*this);
        co_return require_body_expression(
            co_await construct_async_timeout(site, source, timeout->contract, span)
        );
    }
    if (const auto* intrinsic = std::get_if<AsyncIntrinsicSelection>(&*selected_callee)) {
        if (receiver) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::AsyncAdmission,
                "async intrinsic requires a resolved direct call"
            ));
        }
        auto site = BodyExprSite(*this);
        co_return require_body_expression(
            construct_async_intrinsic(site, source, intrinsic->kind, span)
        );
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
            const auto argument_id = source.arguments.front();
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
            const auto pointer = make_pointer_type(
                draft(),
                place->expression.type.construction(),
                selected.access == AccessMode::Write ? PointerAccess::Write : PointerAccess::Read,
                origin(ast.expression(argument_id).span)
            );
            if (!pointer) {
                co_return std::unexpected(pointer.error());
            }
            auto result = make_built(
                *pointer,
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
        auto pending = BodyPendingFailures();
        auto completes = true;
        auto report_condition_completes = true;
        const auto conditional_report = builtin->function == BuiltinFunction::Assert
            || builtin->function == BuiltinFunction::Check
            || builtin->function == BuiltinFunction::Require;
        for (const auto& argument : source.arguments) {
            const auto condition = arguments.empty() && conditional_report;
            if (condition) {
                builtin->condition_source = draft().intern_spelling(
                    draft().source_slice_copy(source_module_id, ast.expression(argument).span)
                );
                auto condition_id = argument;
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
            const auto argument_span = ast.expression(argument).span;
            argument_spans.push_back(argument_span);
            auto built = (co_await build_call_argument(
                argument,
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
            source.arguments[index],
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
    if (contract->execution == CallableExecutionKind::Async) {
        if (!target || receiver) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::AsyncAdmission,
                "cold async construction requires a directly resolved async fn"
            ));
        }
        if (static_stage()) {
            const auto function = draft().function_for_callable(*target);
            if (!function || !draft().function_declaration_copy(*function).is_const) {
                co_return std::unexpected(fail(
                    span,
                    DiagnosticCode::ConstAdmission,
                    "compile-time construction requires an explicitly declared const async fn"
                ));
            }
        }
        const auto operation = make_operation_type(draft(), contract->result, failures);
        auto result = make_built(
            operation,
            SemColdCall {
                .callee = UniqueIndirect(std::move(*callee_operand)),
                .target = *target,
                .arguments = std::move(arguments)
            },
            span,
            std::move(pending_failures)
        );
        result.completes = completes;
        co_return result;
    }
    append_pending_failures(pending_failures, BodyPendingFailures {failures});
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
    const auto function = lookup_class_operation(
        draft(),
        catalog(),
        source_module_id,
        owner,
        lexical_class,
        name,
        receiver,
        span
    );
    if (!function) {
        co_return std::unexpected(function.error());
    }
    auto ready = co_await batch->ensure_function_signature(*function, source_module_id, span);
    if (!ready) {
        co_return std::unexpected(ready.error());
    }
    const auto declaration = draft().function_declaration_copy(*function);
    auto callee = BuiltExpression {
        .storage = UniqueIndirect {BodyExpressionStorage {
            active_builder().callable_expression(declaration.callable, origin(span))
        }},
        .pending_failures = {},
        .takeable = false,
        .completes = true
    };
    observe_source(
        span,
        catalog().declaration_location(draft(), catalog().function_symbol(*function)),
        callee.type()
    );
    co_return callee;
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

auto BodyElaborator::generic_call_expression(
    const ASTCallExpr& source,
    GenericDeclarationID definition,
    std::span<const ASTGenericArgument> supplied,
    Span span
) noexcept -> AnalysisTask<BuiltExpression> {
    auto checked = co_await batch->ensure_generic_function(definition, source_module_id, span);
    if (!checked) {
        co_return std::unexpected(checked.error());
    }
    const auto function =
        std::get<GenericFunctionDefinition>(draft().generic_declaration_copy(definition));
    if (source.arguments.size() != function.signature.parameters.size()) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::TypeCallArity,
            "generic call argument count differs from its declaration"
        ));
    }
    const auto count = function.contract.parameters.size();
    auto inferred = std::vector<std::optional<GenericArgument>>(count);
    auto explicit_environment = GenericEnvironment();
    auto explicit_canonical = CanonicalGenericEnvironment();
    if (!supplied.empty()) {
        auto site = BodyExprSite(*this);
        auto resolved =
            co_await site.resolve_generic_arguments(supplied, function.contract.parameter_kinds);
        if (!resolved) {
            co_return std::unexpected(resolved.error());
        }
        explicit_environment = std::move(*resolved);
        for (auto i = 0uz; i < count; ++i) {
            inferred[i] = explicit_environment[i];
        }
        if (const auto canonical = draft().normalize_generic_environment(explicit_environment)) {
            explicit_canonical = *canonical;
        }
    }
    auto explicit_effects = draft().make_failure_instantiation(definition, explicit_environment);
    auto built = std::vector<BuiltExpression>();
    auto spans = std::vector<Span>();
    for (auto i = 0uz; i < source.arguments.size(); ++i) {
        const auto id = source.arguments[i];
        const auto operand = call_argument_operand(ast, id);
        const auto& parameter = function.signature.parameters[i];
        if (operand.access != parameter.access) {
            co_return std::unexpected(fail(
                ast.expression(id).span,
                DiagnosticCode::AccessCallMismatch,
                "argument access marker differs from the generic parameter"
            ));
        }
        auto expected = std::optional<ConstructionTypeRef>();
        if (!supplied.empty()) {
            if (explicit_canonical.size() == count) {
                auto resolved = draft().substitute_function_type(
                    parameter.type,
                    definition,
                    explicit_canonical,
                    origin(span),
                    &explicit_effects
                );
                if (!resolved) {
                    co_return std::unexpected(resolved.error());
                }
                expected = *resolved;
            } else {
                expected = draft().substitute_generic_type(
                    parameter.type,
                    definition,
                    explicit_environment,
                    &explicit_effects
                );
            }
        } else if (std::holds_alternative<TypeID>(parameter.type)) {
            expected = parameter.type;
        }
        auto value = co_await expression(operand.expression, expected);
        if (!value) {
            co_return std::unexpected(value.error());
        }
        if (const auto* formal = std::get_if<GenericTypeID>(&parameter.type);
            formal && supplied.empty()) {
            if (!draft().infer_generic_argument(
                    *formal,
                    value->type(),
                    definition,
                    inferred,
                    origin(ast.expression(id).span)
                )) {
                co_return std::unexpected(fail(
                    ast.expression(id).span,
                    DiagnosticCode::TypeGenericArguments,
                    "call arguments do not determine a consistent generic type environment"
                ));
            }
        } else if (supplied.empty()
                   && !std::holds_alternative<GenericTypeID>(parameter.type)
                   && !compatible(value->type(), parameter.type)) {
            co_return std::unexpected(fail(
                ast.expression(id).span,
                DiagnosticCode::TypeMismatch,
                "generic call argument differs from its parameter type"
            ));
        }
        built.push_back(std::move(*value));
        spans.push_back(ast.expression(id).span);
    }
    auto arguments = GenericEnvironment();
    for (const auto& argument : inferred) {
        if (!argument) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::TypeGenericArguments,
                "generic parameter cannot be inferred from this call; supply explicit arguments"
            ));
        }
        arguments.push_back(*argument);
    }
    const auto canonical = draft().normalize_generic_environment(arguments);
    const auto all_concrete = canonical.has_value();
    const auto concrete = canonical.value_or(CanonicalGenericEnvironment());
    auto formed = draft().check_generic_type_formations(definition, arguments, origin(span));
    if (!formed) {
        co_return std::unexpected(formed.error());
    }
    auto effects = draft().make_failure_instantiation(definition, arguments);
    auto contract = function.signature;
    for (auto& parameter : contract.parameters) {
        parameter.type =
            draft().substitute_generic_type(parameter.type, definition, arguments, &effects);
    }
    contract.result =
        draft().substitute_generic_type(contract.result, definition, arguments, &effects);
    contract.failures = effects.substitute(contract.failures);
    auto resolved_target = std::optional<CallableID>();
    if (all_concrete && !generic_context) {
        auto target = draft().instantiate_generic_function(definition, concrete, origin(span));
        if (!target) {
            co_return std::unexpected(target.error());
        }
        resolved_target = *target;
        contract = draft().construction_callable_contract_copy(*target);
    }
    auto operands = std::vector<SemCallArgument>();
    auto pending = BodyPendingFailures();
    auto completes = true;
    for (auto i = 0uz; i < built.size(); ++i) {
        auto bound = bind_call_argument(
            std::move(built[i]),
            contract.parameters[i].access,
            contract.parameters[i].type,
            spans[i],
            std::nullopt
        );
        if (!bound) {
            co_return std::unexpected(bound.error());
        }
        append_pending_failures(pending, bound->pending_failures);
        completes &= bound->completes;
        operands.push_back(std::move(bound->argument));
    }
    if (!resolved_target) {
        auto result_type = contract.result;
        auto failures = contract.failures;
        if (contract.execution == CallableExecutionKind::Async) {
            result_type = make_operation_type(draft(), contract.result, failures);
        } else {
            append_pending_failures(pending, BodyPendingFailures {failures});
        }
        auto result = make_built(
            result_type,
            SemGenericCall {
                definition,
                std::move(arguments),
                std::move(operands),
                BodyFailures(
                    contract.execution == CallableExecutionKind::Async
                        ? ConstructionFailureRef(draft().intern_failure_set({}))
                        : failures
                )
            },
            span,
            std::move(pending)
        );
        result.completes = completes;
        co_return result;
    }
    const auto target = resolved_target;
    auto callee = active_builder().callable_expression(*target, origin(span));
    if (contract.execution == CallableExecutionKind::Async) {
        const auto operation = make_operation_type(draft(), contract.result, contract.failures);
        auto result = make_built(
            operation,
            SemColdCall {OwnedSemanticExpression(std::move(callee)), *target, std::move(operands)},
            span,
            std::move(pending)
        );
        result.completes = completes;
        co_return result;
    }
    append_pending_failures(pending, BodyPendingFailures {contract.failures});
    auto result = make_built(
        contract.result,
        SemCall {
            OwnedSemanticExpression(std::move(callee)),
            *target,
            std::move(operands),
            BodyFailures(contract.failures)
        },
        span,
        std::move(pending)
    );
    result.completes = completes;
    co_return result;
}
