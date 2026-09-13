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
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.names;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::select_name(const ASTNameExpr& name, Span span) noexcept
    -> AnalysisResult<SelectedExpression> {
    const auto text = spelling(name.name_span);
    if (const auto* local = use_local(text)) {
        if (const auto* constant = std::get_if<ConstantID>(&local->storage)) {
            auto value = active_builder().make_expression(
                local->type,
                active_builder().lifetime(),
                origin(span),
                SemConstant {.constant = *constant}
            );
            return BuiltExpression {
                .storage = std::move(value),

                .pending_failures = {},
                .takeable = false,
            };
        }
        const auto& runtime = std::get<BoundStorage>(local->storage);
        if (local->role == BodyLocalRole::RangeRead) {
            auto value = active_builder().binding_expression(runtime.binding).expression;
            value.category = SemanticValueCategory::Value;
            value.lifetime = active_builder().lifetime();
            value.origin = origin(span);
            return BuiltExpression {
                .storage = std::move(value),

                .pending_failures = {},
                .takeable = false
            };
        }
        return BuiltExpression {
            .storage = active_builder().binding_expression(runtime.binding),

            .pending_failures = {},
            .takeable = local->takeable,
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
            return std::unexpected(name_reference.error());
        }
        if (name_reference->has_value() && (**name_reference).lookup == CppNameLookup::Global) {
            return CppSelection {.target = std::move(**name_reference), .span = span};
        }
        const auto builtin = text == "print" ? std::optional(BuiltinFunction::Print)
            : text == "println"              ? std::optional(BuiltinFunction::Println)
            : text == "eprint"               ? std::optional(BuiltinFunction::Eprint)
            : text == "eprintln"             ? std::optional(BuiltinFunction::Eprintln)
            : text == "check"                ? std::optional(BuiltinFunction::Check)
            : text == "require"              ? std::optional(BuiltinFunction::Require)
            : text == "fail"                 ? std::optional(BuiltinFunction::Fail)
                                             : std::nullopt;
        if (builtin) {
            return BuiltinSelection {*builtin, span, std::nullopt};
        }
        if (name_reference->has_value()) {
            return CppSelection {.target = std::move(**name_reference), .span = span};
        }
    }
    auto selected = find_global(text, name.name_span);
    if (!selected.has_value()) {
        return std::unexpected(selected.error());
    }
    return std::visit(
        Overloaded {
            [&](const CatalogFunctionForm& function) noexcept -> AnalysisResult<BuiltExpression> {
                auto completed =
                    batch->ensure_function_signature(function.function, source_module_id, span);
                if (!completed.has_value()) {
                    return std::unexpected(completed.error());
                }
                auto value = active_builder().callable_expression(function.callable, origin(span));
                return BuiltExpression {
                    .storage = std::move(value),
                    .pending_failures = {},
                };
            },
            [&](const CatalogConstantForm& constant) noexcept -> AnalysisResult<BuiltExpression> {
                const auto declaration =
                    draft().module_constant_declaration_copy(constant.constant);
                auto value = active_builder().make_expression(
                    draft().constant_copy(declaration.value).type,
                    active_builder().lifetime(),
                    origin(span),
                    SemConstant {.constant = declaration.value}
                );
                return BuiltExpression {
                    .storage = std::move(value),

                    .pending_failures = {},
                    .takeable = false,
                };
            },
            [&](const CatalogEnumCaseForm& enum_case) noexcept -> AnalysisResult<BuiltExpression> {
                const auto type = draft().intern_type(
                    CanonicalType {
                        .value = EnumTypeValue {.enumeration = enum_case.owner},
                    }
                );
                return enum_case_reference(type, text, span, span);
            },
            [&]<typename Form>(const Form&) noexcept -> AnalysisResult<BuiltExpression> {
                static_assert(
                    std::same_as<Form, CatalogStructForm> || std::same_as<Form, CatalogEnumForm>,
                    "unhandled non-value catalog symbol"
                );
                return std::unexpected(fail(
                    name.name_span,
                    DiagnosticCode::TypeValueRequired,
                    std::format("'{}' does not name a runtime value", text)
                ));
            },
        },
        (*selected)->form
    );
}

auto BodyElaborator::validate_builtin(
    BuiltinSelection selection,
    std::span<const ConstructionCallableParameter> parameters,
    std::span<const Span> argument_spans
) noexcept -> AnalysisResult<void> {
    const auto testing = selection.function == BuiltinFunction::Check
        || selection.function == BuiltinFunction::Require
        || selection.function == BuiltinFunction::Fail;
    const auto conditional = selection.function != BuiltinFunction::Fail;
    const auto newline = selection.function == BuiltinFunction::Println
        || selection.function == BuiltinFunction::Eprintln;
    if (testing
        && (parameters.size() < (conditional ? 1uz : 0uz)
            || parameters.size() > (conditional ? 2uz : 1uz))) {
        return std::unexpected(fail(
            selection.span,
            DiagnosticCode::TestArgumentCount,
            "test operation has the wrong number of arguments"
        ));
    }
    if (!testing && !newline && parameters.empty()) {
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
        if (testing) {
            const auto condition = conditional && index == 0;
            if (parameter.access != AccessMode::Read
                || type == nullptr
                || (condition
                        ? type->kind != BuiltinType::Bool
                        : type->kind != BuiltinType::Str && type->kind != BuiltinType::String)) {
                return std::unexpected(fail(
                    parameter_span,
                    condition ? DiagnosticCode::TestConditionType : DiagnosticCode::TestMessageType,
                    condition ? "test condition must have type bool" : "test message must be text"
                ));
            }
            continue;
        }
        if (parameter.access != AccessMode::Read
            || type == nullptr
            || !(
                builtin_is_numeric(type->kind)
                || type->kind == BuiltinType::Bool
                || type->kind == BuiltinType::Char
                || type->kind == BuiltinType::Str
                || type->kind == BuiltinType::String
            )) {
            return std::unexpected(fail(
                parameter_span,
                DiagnosticCode::TypeMismatch,
                "printing requires a Read builtin scalar or text argument"
            ));
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
    const auto result_type = draft().intern_builtin_type(BuiltinType::Void);
    const auto kind = selection.function;
    if (kind == BuiltinFunction::Check
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
                == ConstructionTypeRef(draft().intern_builtin_type(BuiltinType::String))) {
                auto inputs = std::vector<SemCallArgument>();
                inputs.push_back({AccessMode::Read, std::move(argument)});
                argument = builder.make_expression(
                    draft().intern_builtin_type(BuiltinType::Str),
                    builder.lifetime(),
                    site,
                    SemTextIntrinsic {TextIntrinsic::AsStr, std::move(inputs)}
                );
            }
            message = UniqueIndirect(std::move(argument));
        }
        return builder.make_expression(
            result_type,
            builder.lifetime(),
            site,
            SemTestReport {
                .kind = kind == BuiltinFunction::Check ? TestReportKind::Check
                    : kind == BuiltinFunction::Require ? TestReportKind::Require
                                                       : TestReportKind::Fail,
                .condition = std::move(condition),
                .message = std::move(message),
                .condition_source = selection.condition_source
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
    return builder.make_expression(
        result_type,
        builder.lifetime(),
        site,
        SemPrint {print_kind, std::move(operands)}
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
    const auto result_type = draft().intern_builtin_type(BuiltinType::Void);
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
        .pending_failures = {}
    };
}
