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
import :semantic.analysis.body.expression_site;
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.interpret;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::callable_contract(BuiltExpression& callee, Span span) noexcept
    -> AnalysisResult<ConstructionCallableContract> {
    const auto& built = (callee);
    if (const auto* concrete = std::get_if<TypeID>(&built.type())) {
        const auto canonical = draft().type_copy(*concrete);
        auto callable = std::optional<CallableID>();
        std::visit(
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
                            || std::same_as<Value, CppTypeValue>,
                        "unhandled non-owning callable type"
                    );
                },
            },
            canonical.value
        );
        if (callable.has_value()) {
            return draft().construction_callable_contract_copy(*callable);
        }
    } else {
        const auto construction =
            draft().construction_type_copy(std::get<TypeTermID>(built.type()));
        if (const auto* view =
                std::get_if<ConstructionCallableViewTypeValue>(&construction.value)) {
            return ConstructionCallableContract {
                .parameters = view->parameters,
                .result = view->result,
                .failures = view->failures,
                .policy = FailureContractPolicy::Declared,
            };
        }
    }
    return std::unexpected(
        fail(span, DiagnosticCode::TypeNotCallable, "expression is not callable")
    );
}

auto BodyElaborator::build_call_argument(
    ASTExprID source_id,
    const ConstructionCallableParameter& parameter
) noexcept -> AnalysisResult<BuiltCallArgument> {
    const auto& source = ast.expression(source_id);
    auto operand_id = source_id;
    auto explicit_access = std::optional<ASTAccessMode>();
    if (const auto* access = std::get_if<ASTAccessExpr>(&source.value)) {
        explicit_access = access->mode;
        operand_id = access->operand_id;
    }
    const auto required_ast = [&]() noexcept {
        switch (parameter.access) {
            case AccessMode::Read:  return ASTAccessMode::Read;
            case AccessMode::Write: return ASTAccessMode::Write;
            case AccessMode::Take:  return ASTAccessMode::Take;
        }
        std::unreachable();
    }();
    if (parameter.access != AccessMode::Read
        && (!explicit_access.has_value() || *explicit_access != required_ast)) {
        return std::unexpected(fail(
            source.span,
            DiagnosticCode::AccessCallMismatch,
            parameter.access == AccessMode::Write
                ? "Write parameter requires an explicit Write argument"
                : "Take parameter requires an explicit Take argument"
        ));
    }
    if (parameter.access == AccessMode::Read
        && explicit_access.has_value()
        && *explicit_access != ASTAccessMode::Read) {
        return std::unexpected(fail(
            source.span,
            DiagnosticCode::AccessCallMismatch,
            "argument access marker differs from the parameter"
        ));
    }
    auto built = expression(operand_id, parameter.type);
    if (!built.has_value()) {
        return std::unexpected(built.error());
    }
    auto pending_failures = take_pending_failures(*built);
    if (parameter.access == AccessMode::Write) {
        auto compatible_storage =
            require_writable_storage_type(built->type(), parameter.type, source.span);
        if (!compatible_storage.has_value()) {
            return std::unexpected(compatible_storage.error());
        }
        auto place = consume_place(*built, source.span);
        if (!place.has_value()) {
            return std::unexpected(place.error());
        }
        if (place->expression.type.construction() != parameter.type) {
            *place = active_builder().cpp_place(
                std::move(*place),
                parameter.type,
                CppConvertOperation {.explicit_cast = false},
                {},
                origin(source.span)
            );
        }
        return BuiltCallArgument {
            .argument = SemCallArgument {AccessMode::Write, std::move(place->expression)},
            .pending_failures = std::move(pending_failures),
            .completes = built->completes,
        };
    }
    auto coerced = coerce_to(*built, parameter.type, source.span);
    if (!coerced.has_value()) {
        return std::unexpected(coerced.error());
    }
    auto value = consume_value(*built, source.span, parameter.access);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    return BuiltCallArgument {
        .argument = {parameter.access, std::move(*value)},
        .pending_failures = std::move(pending_failures),
        .completes = built->completes,
    };
}

auto BodyElaborator::enum_case_reference(
    TypeID enumeration_type,
    std::string_view case_name,
    Span span,
    Span case_span
) noexcept -> AnalysisResult<BuiltExpression> {
    auto site = BodyExpressionSite(*this);
    return interpret_enum_case(site, enumeration_type, case_name, case_span, {}, span, false);
}

auto BodyElaborator::call_expression(
    const ASTCallExpr& source,
    Span span,
    std::optional<SelectedExpression> prepared_callee
) noexcept -> AnalysisResult<BuiltExpression> {
    const auto& callee_source = ast.expression(source.callee);
    if (const auto* name = std::get_if<ASTNameExpr>(&callee_source.value)) {
        const auto text = spelling(name->name_span);
        if (find_local(text) == nullptr && !catalog().lookup(source_module_id, text).empty()) {
            auto selected = find_global(text, name->name_span);
            if (!selected.has_value()) {
                return std::unexpected(selected.error());
            }
            if (const auto* enum_case = std::get_if<CatalogEnumCaseForm>(&(*selected)->form)) {
                const auto type = draft().intern_type(
                    CanonicalType {
                        .value = EnumTypeValue {.enumeration = enum_case->owner},
                    }
                );
                auto site = BodyExpressionSite(*this);
                return interpret_enum_case(
                    site,
                    type,
                    text,
                    name->name_span,
                    source.arguments,
                    span,
                    true
                );
            }
        }
    }
    auto selected_callee = [&]() noexcept -> AnalysisResult<SelectedExpression> {
        if (prepared_callee.has_value()) {
            return std::move(*prepared_callee);
        }
        return select_expression(source.callee);
    }();
    if (!selected_callee.has_value()) {
        return std::unexpected(selected_callee.error());
    }
    if (std::holds_alternative<CppSelection>(*selected_callee)
        || is_cpp_type(std::get<BuiltExpression>(*selected_callee).type())) {
        return cpp_call(std::move(*selected_callee), source, span);
    }
    auto callee =
        std::optional<BuiltExpression>(std::move(std::get<BuiltExpression>(*selected_callee)));
    auto pending_failures = take_pending_failures(*callee);
    auto contract = callable_contract(*callee, ast.expression(source.callee).span);
    if (!contract.has_value()) {
        return std::unexpected(contract.error());
    }
    if (source.arguments.size() != contract->parameters.size()) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeCallArity,
            std::format(
                "call expects {} arguments but received {}",
                contract->parameters.size(),
                source.arguments.size()
            )
        ));
    }
    auto callee_operand = std::optional<SemanticExpression>();
    if (callee->is_function_reference()) {
        callee_operand = take_built(*callee, ast.expression(source.callee).span);
    } else {
        auto value = consume_value(*callee, ast.expression(source.callee).span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        callee_operand = std::move(*value);
    }
    auto completes = callee->completes;
    auto arguments = std::vector<SemCallArgument>();
    arguments.reserve(source.arguments.size());
    for (auto index = 0uz; index < source.arguments.size(); ++index) {
        auto argument =
            build_call_argument(source.arguments[index].expression, contract->parameters[index]);
        if (!argument.has_value()) {
            return std::unexpected(argument.error());
        }
        append_pending_failures(pending_failures, argument->pending_failures);
        completes &= argument->completes;
        arguments.push_back(std::move(argument->argument));
    }
    const auto failures = contract->failures;
    append_pending_failures(pending_failures, BodyPendingFailureTerms {failures});
    auto result = active_builder().make_expression(
        contract->result,
        active_builder().lifetime(),
        origin(span),
        SemCall {
            .callee = UniqueIndirect(std::move(*callee_operand)),
            .arguments = std::move(arguments),
            .callee_failures = BodyFailures(failures)
        }
    );
    return BuiltExpression {
        .storage = std::move(result),

        .pending_failures = std::move(pending_failures),
        .completes = completes,
    };
}
