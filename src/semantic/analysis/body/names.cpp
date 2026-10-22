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
import :semantic.analysis.async;
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
import :semantic.semir.generic;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.unique_indirect;
import :support.visit;
import std;

namespace {

auto builtin_result_type(BuiltinFunction function) noexcept -> BuiltinType {
    switch (function) {
        case BuiltinFunction::Hash:  return BuiltinType::U64;
        case BuiltinFunction::Equal: return BuiltinType::Bool;
        default:                     return BuiltinType::Void;
    }
}

} // namespace

auto BodyElaborator::select_name(const ASTNameExpr& name, Span span) noexcept
    -> AnalysisTask<SelectedExpression> {
    const auto text = spelling(name.name_span);
    observe_source(name.name_span, std::nullopt, std::nullopt);
    if (const auto* local = use_local(text)) {
        if (runtime_local_outside_static_scope(text)) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::ConstAdmission,
                std::format("static expression cannot use the runtime value '{}'", text)
            ));
        }
        const auto binding = local->storage.binding;
        const auto foreign = binding.owner() != active_builder().identity();
        observe_binding(name.name_span, *local);
        if (foreign || local->static_source || local->role == BodyLocalRole::RangeRead) {
            auto value = co_await read_local(*local, span);
            if (!value) {
                co_return std::unexpected(value.error());
            }
            if (!*value) {
                co_return std::unexpected(fail(
                    span,
                    DiagnosticCode::ConstAdmission,
                    "separate body cannot access an unbound value from an enclosing frame"
                ));
            }
            co_return BuiltExpression {
                .storage = UniqueIndirect {BodyExpressionStorage {std::move(**value)}},
                .pending_failures = {},
                .takeable = false,
                .completes = true,
            };
        }
        co_return BuiltExpression {
            .storage = UniqueIndirect {BodyExpressionStorage {local_place(*local, span)}},
            .pending_failures = {},
            .takeable = local->takeable,
            .completes = true,
        };
    }
    auto name_reference = lookup_cpp_name(
        draft(),
        catalog(),
        import_usage(),
        source_module_id,
        {.global_root = std::nullopt, .components = {name.name_span}}
    );
    if (!name_reference.has_value()) {
        co_return std::unexpected(name_reference.error());
    }
    if (name_reference->has_value()) {
        co_return CppSelection {.target = std::move(**name_reference), .span = span};
    }
    auto selected = (co_await find_global(text, name.name_span));
    if (!selected.has_value()) {
        co_return std::unexpected(selected.error());
    }
    const auto* selected_symbol = catalog().symbol(*selected);
    if (const auto* provided = std::get_if<ProvidedExport>(&*selected)) {
        if (const auto callable = draft().provided_callable(*provided)) {
            co_return BuiltExpression {
                .storage = UniqueIndirect {BodyExpressionStorage {
                    active_builder().callable_expression(*callable, origin(span))
                }},
                .pending_failures = {},
                .takeable = false,
                .completes = true
            };
        }
        const auto builtin = [&]() noexcept -> std::optional<BuiltinFunction> {
            switch (*provided) {
                case ProvidedExport::Print:     return BuiltinFunction::Print;
                case ProvidedExport::Println:   return BuiltinFunction::Println;
                case ProvidedExport::Eprint:    return BuiltinFunction::Eprint;
                case ProvidedExport::Eprintln:  return BuiltinFunction::Eprintln;
                case ProvidedExport::Assert:    return BuiltinFunction::Assert;
                case ProvidedExport::Check:     return BuiltinFunction::Check;
                case ProvidedExport::Require:   return BuiltinFunction::Require;
                case ProvidedExport::Fail:      return BuiltinFunction::Fail;
                case ProvidedExport::Addressof: return BuiltinFunction::Addressof;
                default:                        return std::nullopt;
            }
        }();
        if (builtin) {
            co_return BuiltinSelection {
                .function = *builtin,
                .span = span,
                .condition_source = std::nullopt,
                .operand_sources = std::nullopt
            };
        }
        if (const auto async = provided_async_builtin(*provided)) {
            auto resolved = co_await resolve_async_builtin(
                construction_requests(),
                *async,
                source_module_id,
                span
            );
            if (!resolved) {
                co_return std::unexpected(resolved.error());
            }
            co_return resolved->visit(
                Overloaded {
                    [&](AsyncIntrinsic intrinsic) noexcept -> SelectedExpression {
                        return AsyncIntrinsicSelection {.kind = intrinsic, .span = span};
                    },
                    [&](AsyncCompositionPolicy policy) noexcept -> SelectedExpression {
                        return CompositionSelection {.policy = policy, .span = span};
                    },
                    [&](const TimeoutContract& contract) noexcept -> SelectedExpression {
                        return TimeoutSelection {.contract = contract, .span = span};
                    },
                }
            );
        }
    }
    if (!selected_symbol) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::TypeValueRequired,
            std::format("'{}' does not name a runtime value", text)
        ));
    }
    co_return (co_await selected_symbol->form.visit(
        Overloaded {
            [&](const CatalogFunctionForm& function) noexcept -> AnalysisTask<BuiltExpression> {
                auto completed =
                    (co_await batch
                         ->ensure_function_signature(function.function, source_module_id, span));
                if (!completed.has_value()) {
                    co_return std::unexpected(completed.error());
                }
                auto value = active_builder().callable_expression(function.callable, origin(span));
                observe_source(name.name_span, std::nullopt, value.type.construction());
                co_return BuiltExpression {
                    .storage = UniqueIndirect {BodyExpressionStorage {std::move(value)}},
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
                    constant_operation(draft(), declaration.value)
                );
                co_return BuiltExpression {
                    .storage = UniqueIndirect {BodyExpressionStorage {std::move(value)}},

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
                    std::same_as<Form, CatalogStructForm>
                        || std::same_as<Form, CatalogEnumForm>
                        || std::same_as<Form, CatalogGenericForm>,
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
    if (selection.function == BuiltinFunction::Hash
        || selection.function == BuiltinFunction::Equal) {
        const auto hashing = selection.function == BuiltinFunction::Hash;
        if (parameters.size() != (hashing ? 1uz : 2uz)) {
            return std::unexpected(fail(
                selection.span,
                DiagnosticCode::TypeCallArity,
                hashing ? "Hash::hash requires one argument" : "Equal::equal requires two arguments"
            ));
        }
        const auto capability = hashing ? BuiltinCapability::Hash : BuiltinCapability::Equal;
        for (const auto& parameter : parameters) {
            if (parameter.access != AccessMode::Read) {
                return std::unexpected(fail(
                    selection.span,
                    DiagnosticCode::AccessCallMismatch,
                    "capability operands require Read access"
                ));
            }
            if (!type_satisfies_capability(draft(), parameter.type, capability)) {
                return std::unexpected(fail(
                    selection.span,
                    DiagnosticCode::TypeGenericArguments,
                    hashing ? "operand does not satisfy Hash" : "operand does not satisfy Equal"
                ));
            }
        }
        const auto text = [&](ConstructionTypeRef type) noexcept {
            type = unwrap_type_alias(draft(), type);
            return type == ConstructionTypeRef(draft().builtin_type(BuiltinType::String))
                || type == ConstructionTypeRef(draft().builtin_type(BuiltinType::Str));
        };
        if (!hashing
            && !type_shapes_compatible(draft(), parameters[0].type, parameters[1].type)
            && !(text(parameters[0].type) && text(parameters[1].type))) {
            return std::unexpected(fail(
                selection.span,
                DiagnosticCode::TypeMismatch,
                "Equal::equal requires compatible operand types"
            ));
        }
        return {};
    }
    if (selection.function == BuiltinFunction::Addressof) {
        return std::unexpected(fail(
            selection.span,
            DiagnosticCode::TypeNotCallable,
            "addressof must be called on a place"
        ));
    }
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
                && (type->kind == BuiltinType::Void || type->kind == BuiltinType::StrCharsView))) {
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
    const auto result_type = draft().builtin_type(builtin_result_type(selection.function));
    const auto kind = selection.function;
    if (kind == BuiltinFunction::Hash) {
        return builder.make_expression(
            result_type,
            builder.lifetime(),
            site,
            SemHash {.operand = UniqueIndirect(std::move(operands.front().expression))}
        );
    }
    if (kind == BuiltinFunction::Equal) {
        return builder.make_expression(
            result_type,
            builder.lifetime(),
            site,
            SemBinary {
                .left = UniqueIndirect(std::move(operands[0].expression)),
                .operation = BinaryOperator::Equal,
                .right = UniqueIndirect(std::move(operands[1].expression))
            }
        );
    }
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
                inputs.push_back({.access = AccessMode::Read, .expression = std::move(argument)});
                argument = builder.make_expression(
                    draft().builtin_type(BuiltinType::Str),
                    builder.lifetime(),
                    site,
                    SemIntrinsic {.operation = TextIntrinsic::AsStr, .operands = std::move(inputs)}
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
    const auto site = origin(selection.span);
    auto signature_parameters = std::vector(parameters.begin(), parameters.end());
    for (auto& parameter : signature_parameters) {
        parameter.type = unwrap_type_alias(draft(), parameter.type);
    }
    auto valid = validate_builtin(selection, signature_parameters, argument_spans);
    if (!valid) {
        return std::unexpected(valid.error());
    }
    const auto result_type = draft().builtin_type(builtin_result_type(selection.function));
    const auto failures = draft().add_empty_failure_term();
    auto signature = ConstructionCallableContract {
        .execution = CallableExecutionKind::Synchronous,
        .parameters = std::move(signature_parameters),
        .result = result_type,
        .failures = failures,
        .policy = FailureContractPolicy::Declared,
    };
    auto definition = std::optional<GenericDeclarationID>();
    if (generic_context) {
        definition = draft().append_generic_closure(generic_context->definition, site, signature);
    }
    auto reservation = definition ? draft().reserve_generic_body(*definition)
                                  : draft().reserve_body(BodyKind::Closure);
    const auto body_id = reservation.id();
    auto builder = BodyBuilder(std::move(reservation), draft());
    const auto lexical =
        builder.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, site);
    const auto full =
        builder.add_lifetime_region(lexical, LifetimeRegionKind::FullExpression, site);
    builder.set_lifetime(full);
    auto operands = std::vector<SemCallArgument>();
    for (const auto& [index, parameter] : std::views::enumerate(signature.parameters)) {
        const auto binding = builder.add_parameter(
            draft().intern_spelling(std::format("value{}", index)),
            parameter.type,
            lexical,
            parameter.access,
            site
        );
        auto input = builder.binding_expression(binding.binding).expression;
        operands.push_back({.access = parameter.access, .expression = std::move(input)});
    }
    auto operation = builtin_operation(builder, selection, std::move(operands));
    auto statements = std::vector<SemanticStatement>();
    const auto returns_value = !is_void_type(draft(), result_type);
    if (returns_value) {
        statements.push_back(
            {.origin = site,
             .lifetime = full,
             .reachable = true,
             .value = SemReturn {.value = std::move(operation)}}
        );
    } else {
        statements.push_back(
            {.origin = site,
             .lifetime = full,
             .reachable = true,
             .value = SemExpressionStatement {.expression = std::move(operation)}}
        );
    }
    if (selection.function != BuiltinFunction::Fail && !returns_value) {
        statements.push_back({
            .origin = site,
            .lifetime = full,
            .reachable = true,
            .value = SemReturn {.value = std::nullopt},
        });
    }
    auto body = std::move(builder).finish(
        SemanticRegion {
            .lifetime = lexical,
            .origin = site,
            .statements = std::move(statements),
            .result = std::nullopt,
            .result_reachable = false,
            .failures = BodyFailures(failures),
            .exits_call = false,

        }
    );
    const auto callable = [&]() noexcept -> ConstructionCallableRef {
        if (definition) {
            draft().complete_generic_callable_result(*definition, result_type);
            draft().set_generic_callable_body(*definition, std::move(body));
            return draft().intern_generic_type(
                GenericClosureType {
                    .definition = *definition,
                    .arguments = draft().generic_parameter_environment(*definition),
                }
            );
        }
        const auto id = draft().append_body_callable(std::move(signature));
        draft().complete_callable(id, ClosureBodyImplementation {.body = body_id});
        draft().add_body_draft(std::move(body));
        return id;
    }();
    const auto type = callable.visit([&](const auto id) noexcept -> ConstructionTypeRef {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(id)>, CallableID>) {
            return draft().intern_type({.value = ClosureTypeValue {.callable = id}});
        } else {
            return id;
        }
    });
    return BuiltExpression {
        .storage = UniqueIndirect {BodyExpressionStorage {active_builder().make_expression(
            type,
            active_builder().lifetime(),
            site,
            SemClosure {.callable = callable, .captures = {}}
        )}},
        .pending_failures = {},
        .takeable = true,
        .completes = true,
    };
}
