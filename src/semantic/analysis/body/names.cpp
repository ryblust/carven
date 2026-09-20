module carven:semantic.analysis.body.names.impl;

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
import :semantic.analysis.body.resolve;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.names;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.evaluation.operation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::select_name(const ASTNameExpr& name, Span span) noexcept
    -> AnalysisTask<SelectedExpression> {
    const auto text = spelling(name.name_span);
    if (const auto* local = use_local(text)) {
        if (const auto* constant = std::get_if<ConstantID>(&local->storage)) {
            auto value = active_builder().make_expression(
                local->type,
                active_builder().lifetime(),
                origin(span),
                SemConstant {.constant = *constant}
            );
            co_return BuiltExpression {
                .storage = std::move(value),

                .pending_failures = {},
                .takeable = false,
                .completes = true,
            };
        }
        const auto& runtime = std::get<BoundStorage>(local->storage);
        if (runtime.binding.owner() != active_builder().identity()) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::ConstAdmission,
                "constant block cannot access a value from an enclosing execution frame"
            ));
        }
        if (local->role == BodyLocalRole::RangeRead) {
            auto value = active_builder().binding_expression(runtime.binding).expression;
            value.category = SemanticValueCategory::Value;
            value.lifetime = active_builder().lifetime();
            value.origin = origin(span);
            co_return BuiltExpression {
                .storage = std::move(value),

                .pending_failures = {},
                .takeable = false,
                .completes = true,
            };
        }
        co_return BuiltExpression {
            .storage = active_builder().binding_expression(runtime.binding),

            .pending_failures = {},
            .takeable = local->takeable,
            .completes = true,
        };
    }
    if (catalog().lookup(source_module_id, text).empty()) {
        auto name_reference = lookup_cpp_name(
            draft(),
            catalog(),
            import_usage(),
            source_module_id,
            CppNameLookup::ModuleScope,
            std::span(&name.name_span, 1uz)
        );
        if (!name_reference.has_value()) {
            co_return std::unexpected(name_reference.error());
        }
        if (name_reference->has_value() && (**name_reference).lookup == CppNameLookup::Global) {
            co_return CppSelection {.target = std::move(**name_reference), .span = span};
        }
        const auto builtin = text == "print" ? std::optional(BuiltinFunction::Print)
            : text == "println"              ? std::optional(BuiltinFunction::Println)
            : text == "eprint"               ? std::optional(BuiltinFunction::Eprint)
            : text == "eprintln"             ? std::optional(BuiltinFunction::Eprintln)
            : text == "assert"               ? std::optional(BuiltinFunction::Assert)
            : text == "check"                ? std::optional(BuiltinFunction::Check)
            : text == "require"              ? std::optional(BuiltinFunction::Require)
            : text == "fail"                 ? std::optional(BuiltinFunction::Fail)
                                             : std::nullopt;
        if (builtin) {
            co_return BuiltinSelection {
                .function = *builtin,
                .span = span,
                .condition_source = std::nullopt,
                .operand_sources = std::nullopt
            };
        }
        if (name_reference->has_value()) {
            co_return CppSelection {.target = std::move(**name_reference), .span = span};
        }
    }
    auto selected = (co_await find_global(text, name.name_span));
    if (!selected.has_value()) {
        co_return std::unexpected(selected.error());
    }
    co_return (co_await (*selected)->form.visit(
        Overloaded {
            [&](const CatalogFunctionForm& function) noexcept -> AnalysisTask<BuiltExpression> {
                auto completed =
                    (co_await batch
                         ->ensure_function_signature(function.function, source_module_id, span));
                if (!completed.has_value()) {
                    co_return std::unexpected(completed.error());
                }
                auto value = active_builder().callable_expression(function.callable, origin(span));
                co_return BuiltExpression {
                    .storage = std::move(value),
                    .pending_failures = {},
                    .takeable = true,
                    .completes = true,
                };
            },
            [&](const CatalogConstantForm& constant) noexcept -> AnalysisTask<BuiltExpression> {
                const auto declaration =
                    draft().module_constant_declaration_copy(constant.constant);
                auto value = active_builder().make_expression(
                    draft().constant(declaration.value).type,
                    active_builder().lifetime(),
                    origin(span),
                    SemConstant {.constant = declaration.value}
                );
                co_return BuiltExpression {
                    .storage = std::move(value),

                    .pending_failures = {},
                    .takeable = false,
                    .completes = true,
                };
            },
            [&](const CatalogEnumCaseForm& enum_case) noexcept -> AnalysisTask<BuiltExpression> {
                const auto type = draft().intern_type(
                    CanonicalType {
                        .value = EnumTypeValue {.enumeration = enum_case.owner},
                    }
                );
                co_return (co_await enum_case_reference(type, text, span, span));
            },
            [&]<typename Form>(const Form&) noexcept -> AnalysisTask<BuiltExpression> {
                static_assert(
                    std::same_as<Form, CatalogStructForm> || std::same_as<Form, CatalogEnumForm>,
                    "unhandled non-value catalog symbol"
                );
                co_return std::unexpected(fail(
                    name.name_span,
                    DiagnosticCode::TypeValueRequired,
                    std::format("'{}' does not name a runtime value", text)
                ));
            },
        }
    ));
}

auto BodyElaborator::validate_builtin(
    BuiltinSelection selection,
    std::span<const ConstructionCallableParameter> parameters,
    std::span<const Span> argument_spans
) noexcept -> AnalysisResult<void> {
    const auto assertion = selection.function == BuiltinFunction::Assert;
    const auto reporting = selection.function == BuiltinFunction::Assert
        || selection.function == BuiltinFunction::Check
        || selection.function == BuiltinFunction::Require
        || selection.function == BuiltinFunction::Fail;
    const auto conditional = selection.function != BuiltinFunction::Fail;
    const auto newline = selection.function == BuiltinFunction::Println
        || selection.function == BuiltinFunction::Eprintln;
    if (reporting
        && (parameters.size() < (conditional ? 1uz : 0uz)
            || parameters.size() > (conditional ? 2uz : 1uz))) {
        return std::unexpected(fail(
            selection.span,
            assertion ? DiagnosticCode::TypeCallArity : DiagnosticCode::TestArgumentCount,
            "report operation has the wrong number of arguments"
        ));
    }
    if (!reporting && !newline && parameters.empty()) {
        return std::unexpected(fail(
            selection.span,
            DiagnosticCode::TypeCallArity,
            "printing expects at least one argument"
        ));
    }
    for (const auto& [index, parameter] : std::views::enumerate(parameters)) {
        const auto parameter_span = argument_spans.empty() ? selection.span : argument_spans[index];
        const auto* concrete = std::get_if<TypeID>(&parameter.type);
        const auto canonical =
            concrete ? std::optional(draft().type_copy(*concrete)) : std::nullopt;
        const auto* type = canonical ? std::get_if<BuiltinTypeValue>(&canonical->value) : nullptr;
        if (reporting) {
            const auto condition = conditional && index == 0;
            if (parameter.access != AccessMode::Read
                || type == nullptr
                || (condition
                        ? type->kind != BuiltinType::Bool
                        : type->kind != BuiltinType::Str && type->kind != BuiltinType::String)) {
                return std::unexpected(fail(
                    parameter_span,
                    condition ? (assertion ? DiagnosticCode::TypeConditionBool
                                           : DiagnosticCode::TestConditionType)
                              : (assertion ? DiagnosticCode::TypeMismatch
                                           : DiagnosticCode::TestMessageType),
                    condition ? "report condition must have type bool"
                              : "report message must be text"
                ));
            }
            continue;
        }
        if (parameter.access != AccessMode::Read
            || (type != nullptr
                && (type->kind == BuiltinType::Void
                    || type->kind == BuiltinType::EntryArgs
                    || type->kind == BuiltinType::StrCharsView))) {
            return std::unexpected(
                fail(parameter_span, DiagnosticCode::TypeMismatch, "printing requires a Read value")
            );
        }
    }
    return {};
}

auto BodyElaborator::builtin_operation(
    BodyBuilder& builder,
    BuiltinSelection selection,
    std::vector<SemCallArgument> operands
) noexcept -> SemanticExpression {
    const auto site = origin(selection.span);
    const auto result_type = draft().builtin_type(BuiltinType::Void);
    const auto kind = selection.function;
    if (kind == BuiltinFunction::Assert
        || kind == BuiltinFunction::Check
        || kind == BuiltinFunction::Require
        || kind == BuiltinFunction::Fail) {
        const auto conditional = kind != BuiltinFunction::Fail;
        auto condition = std::optional<OwnedSemanticExpression>();
        auto message = std::optional<OwnedSemanticExpression>();
        if (conditional) {
            condition = UniqueIndirect(std::move(operands.front().expression));
        }
        if (operands.size() > (conditional ? 1uz : 0uz)) {
            auto argument = std::move(operands.back().expression);
            if (argument.type.construction()
                == ConstructionTypeRef(draft().builtin_type(BuiltinType::String))) {
                auto inputs = std::vector<SemCallArgument>();
                inputs.push_back({AccessMode::Read, std::move(argument)});
                argument = builder.make_expression(
                    draft().builtin_type(BuiltinType::Str),
                    builder.lifetime(),
                    site,
                    SemTextIntrinsic {TextIntrinsic::AsStr, std::move(inputs)}
                );
            }
            message = UniqueIndirect(std::move(argument));
        }
        const auto operand_sources = condition
                && (std::holds_alternative<SemBinary>((*condition)->value)
                    || std::holds_alternative<SemShortCircuit>((*condition)->value))
            ? selection.operand_sources
            : std::nullopt;
        return builder.make_expression(
            result_type,
            builder.lifetime(),
            site,
            SemReport {
                .kind = kind == BuiltinFunction::Assert ? ReportKind::Assert
                    : kind == BuiltinFunction::Check    ? ReportKind::Check
                    : kind == BuiltinFunction::Require  ? ReportKind::Require
                                                        : ReportKind::Fail,
                .condition = std::move(condition),
                .message = std::move(message),
                .condition_source = selection.condition_source,
                .operand_sources = operand_sources
            }
        );
    }
    const auto print_kind = [&]() noexcept {
        switch (kind) {
            case BuiltinFunction::Print:    return PrintKind::Print;
            case BuiltinFunction::Println:  return PrintKind::Println;
            case BuiltinFunction::Eprint:   return PrintKind::Eprint;
            case BuiltinFunction::Eprintln: return PrintKind::Eprintln;
            default:                        std::unreachable();
        }
    }();
    for (auto& operand : operands) {
        operand.expression.constant = builder.known_constant(operand.expression);
    }
    return builder.make_expression(
        result_type,
        builder.lifetime(),
        site,
        SemPrint {
            .kind = print_kind,
            .operands = std::move(operands),
        }
    );
}

auto BodyElaborator::builtin_callable(
    BuiltinSelection selection,
    std::span<const ConstructionCallableParameter> parameters,
    std::span<const Span> argument_spans
) noexcept -> AnalysisResult<BuiltExpression> {
    auto valid = validate_builtin(selection, parameters, argument_spans);
    if (!valid) {
        return std::unexpected(valid.error());
    }
    const auto site = origin(selection.span);
    auto reservation = draft().reserve_body(BodyKind::Closure);
    const auto body_id = reservation.id();
    auto builder = BodyBuilder(std::move(reservation), draft());
    const auto lexical =
        builder.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, site);
    const auto full =
        builder.add_lifetime_region(lexical, LifetimeRegionKind::FullExpression, site);
    builder.set_lifetime(full);
    auto operands = std::vector<SemCallArgument>();
    for (const auto& [index, parameter] : std::views::enumerate(parameters)) {
        const auto binding = builder.add_parameter(
            draft().intern_spelling(std::format("value{}", index)),
            parameter.type,
            lexical,
            parameter.access,
            site
        );
        auto input = builder.binding_expression(binding.binding).expression;
        operands.push_back({parameter.access, std::move(input)});
    }
    const auto result_type = draft().builtin_type(BuiltinType::Void);
    const auto failures = draft().add_empty_failure_term();
    auto operation = builtin_operation(builder, selection, std::move(operands));
    const auto exits_test = operation.exits_test;
    auto statements = std::vector<SemanticStatement>();
    statements.push_back(
        {.origin = site, .lifetime = full, .value = SemExpressionStatement {std::move(operation)}}
    );
    if (selection.function != BuiltinFunction::Fail) {
        statements.push_back({.origin = site, .lifetime = full, .value = SemReturn {std::nullopt}});
    }
    auto body = std::move(builder).finish(
        SemanticRegion {
            .lifetime = lexical,
            .origin = site,
            .statements = std::move(statements),
            .result = std::nullopt,
            .failures = BodyFailures(failures),
            .exits_test = exits_test
        }
    );
    const auto callable = draft().append_body_callable(
        ConstructionCallableContract {
            .parameters = std::vector(parameters.begin(), parameters.end()),
            .result = result_type,
            .failures = failures,
            .policy = FailureContractPolicy::Declared
        }
    );
    draft().complete_callable(callable, ClosureBodyImplementation {.body = body_id});
    draft().add_body_draft(std::move(body));
    const auto type = draft().intern_type({.value = ClosureTypeValue {.callable = callable}});
    return BuiltExpression {
        .storage = active_builder().make_expression(
            type,
            active_builder().lifetime(),
            site,
            SemClosure {.callable = callable, .captures = {}}
        ),
        .pending_failures = {},
        .takeable = true,
        .completes = true,
    };
}
