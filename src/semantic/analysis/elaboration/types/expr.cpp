module carven:semantic.analysis.elaboration.types.expr.impl;

import :diagnostics.builder;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.region;
import :frontend.literal;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expr;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.types;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.type;
import :support.visit;
import std;

auto build_expected_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTExprID id,
    HIRTypeID expected,
    ExpectedExpressionUsage usage
) noexcept -> HIRExprID {
    return expression_expected_diagnosing(
        module_analysis,
        scopes,
        control,
        id,
        expected,
        DiagnosticCode::TypeMismatch,
        "expression has an incompatible type",
        usage
    );
}

auto expression_expected_diagnosing(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTExprID id,
    HIRTypeID expected,
    DiagnosticCode mismatch_code,
    std::string_view mismatch_message,
    ExpectedExpressionUsage usage
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto& value = ast.expression(id);
    const auto lambda_expression = std::holds_alternative<ASTLambdaExpr>(value.value);
    if (lambda_expression) {
        const auto result = build_expression(module_analysis, scopes, control, id, expected);
        if (!compatible(module_analysis, expected, expression_type(module_analysis, result))) {
            module_analysis.emit(value.span, std::string(mismatch_message), mismatch_code);
            return result;
        }
        defer_callable_compatibility(
            module_analysis,
            result,
            expected,
            builder.expression(result).origin,
            mismatch_code,
            mismatch_message
        );
        const auto* closure = std::get_if<HIRClosureTypeValue>(
            &builder.type(expression_type(module_analysis, result)).value
        );
        if (closure == nullptr
            || !std::holds_alternative<HIRFunctionRefTypeValue>(builder.type(expected).value)) {
            return result;
        }
        const auto forbidden = closure->capturing && usage != ExpectedExpressionUsage::CallArgument;
        if (forbidden) {
            module_analysis.emit(
                value.span,
                "capturing Lambda temporary cannot initialize an escaping function view",
                DiagnosticCode::TypeCallableViewEscape
            );
        }
        return append_expression(
            module_analysis,
            {
                .origin = module_analysis.origin(value.span),
                .type = expected,
                .constant = std::nullopt,
                .value = HIRCallableViewExpr {
                    .source = result,
                },
            }
        );
    }
    if (const auto* region = std::get_if<CppRegion>(&value.value)) {
        return append_expression(
            module_analysis,
            {
                .origin = module_analysis.origin(value.span),
                .type = expected,
                .constant = std::nullopt,
                .value = HIRCppExpr {
                    .bytes = builder.intern_string(module_analysis.spelling(region->body_span)),
                },
            }
        );
    }
    if (const auto* array = std::get_if<ASTArrayExpr>(&value.value)) {
        if (const auto* expected_array =
                std::get_if<HIRArrayTypeValue>(&builder.type(expected).value)) {
            const auto element_type = expected_array->element_type_id;
            const auto extent = expected_array->extent;
            auto elements = std::vector<HIRExprID>();
            elements.reserve(array->element_ids.size());
            for (const auto element : array->element_ids) {
                elements.push_back(expression_expected_diagnosing(
                    module_analysis,
                    scopes,
                    control,
                    element,
                    element_type,
                    mismatch_code,
                    mismatch_message,
                    ExpectedExpressionUsage::Construction
                ));
            }
            if (elements.size() != extent) {
                module_analysis.emit(
                    value.span,
                    std::format(
                        "array expression has extent {}, but the expected type has extent {}",
                        elements.size(),
                        extent
                    ),
                    mismatch_code
                );
            }
            return append_expression(
                module_analysis,
                {
                    .origin = module_analysis.origin(value.span),
                    .type = expected,
                    .constant = std::nullopt,
                    .value = HIRArrayExpr {.element_ids = std::move(elements)},
                }
            );
        }
    }
    if (const auto* contextual = std::get_if<ASTContextualCaseExpr>(&value.value)) {
        const auto member_resolution = resolve_enum_case(
            module_analysis,
            expected,
            module_analysis.spelling(contextual->name_span),
            contextual->name_span
        );
        if (!member_resolution.has_value() && member_resolution.error() == LookupError::Diagnosed) {
            return module_analysis.recover_expression(value.span);
        }
        if (!member_resolution.has_value()) {
            module_analysis.emit(
                contextual->name_span,
                "expected enum has no case with this name",
                DiagnosticCode::TypeMemberUnresolved
            );
            return module_analysis.recover_expression(value.span);
        }
        const auto member = member_resolution.value();
        const auto payload_types = member.payload_types;
        if (!payload_types.empty()) {
            module_analysis.emit(
                value.span,
                "payload enum case must be called with its payload",
                DiagnosticCode::TypeEnumCaseArity
            );
        }
        return append_expression(
            module_analysis,
            {
                .origin = module_analysis.origin(value.span),
                .type = expected,
                .constant = member.constant.has_value()
                    ? std::optional<HIRConstant> {builder.constant(*member.constant).value}
                    : std::nullopt,
                .value = HIRCaseConstructionExpr {
                    .enum_case = member.id,
                    .payload = {},
                },
            }
        );
    }
    if (const auto* call = std::get_if<ASTCallExpr>(&value.value)) {
        const auto& callee = ast.expression(call->callee);
        if (const auto* contextual = std::get_if<ASTContextualCaseExpr>(&callee.value)) {
            const auto recover_call = [&]() noexcept -> HIRExprID {
                for (const auto& argument : call->arguments) {
                    static_cast<void>(
                        build_expression(module_analysis, scopes, control, argument.expression)
                    );
                }
                return module_analysis.recover_expression(value.span);
            };
            const auto member_resolution = resolve_enum_case(
                module_analysis,
                expected,
                module_analysis.spelling(contextual->name_span),
                contextual->name_span
            );
            if (!member_resolution.has_value()
                && member_resolution.error() == LookupError::Diagnosed) {
                return recover_call();
            }
            if (!member_resolution.has_value()) {
                module_analysis.emit(
                    contextual->name_span,
                    "expected enum has no case with this name",
                    DiagnosticCode::TypeMemberUnresolved
                );
                return recover_call();
            }
            const auto member = member_resolution.value();
            const auto payload_types = member.payload_types;
            auto payload = std::vector<HIRExprID>();
            payload.reserve(call->arguments.size());
            if (call->arguments.size() != payload_types.size()) {
                module_analysis.emit(
                    value.span,
                    "enum case payload arity does not match",
                    DiagnosticCode::TypeEnumCaseArity
                );
            }
            for (auto index = 0uz; index < call->arguments.size(); ++index) {
                payload.push_back(
                    index < payload_types.size() ? expression_expected_diagnosing(
                                                       module_analysis,
                                                       scopes,
                                                       control,
                                                       call->arguments[index].expression,
                                                       payload_types[index],
                                                       DiagnosticCode::TypeCallArgument,
                                                       "enum case payload has an incompatible type",
                                                       ExpectedExpressionUsage::Construction
                                                   )
                                                 : build_expression(
                                                       module_analysis,
                                                       scopes,
                                                       control,
                                                       call->arguments[index].expression
                                                   )
                );
            }
            auto constant = std::optional<HIRConstant>();
            auto payload_constants = std::vector<HIRConstantID>();
            if (payload.size() == payload_types.size()
                && std::ranges::all_of(payload, [&](HIRExprID argument) noexcept -> bool {
                       return builder.expression(argument).constant.has_value();
                   })) {
                for (const auto argument : payload) {
                    payload_constants.push_back(*builder.expression(argument).constant);
                }
                constant = HIRPayloadEnumConstant {
                    .enum_case = member.id,
                    .payload = std::move(payload_constants),
                };
            }
            return append_expression(
                module_analysis,
                {
                    .origin = module_analysis.origin(value.span),
                    .type = expected,
                    .constant = std::move(constant),
                    .value = HIRCaseConstructionExpr {
                        .enum_case = member.id,
                        .payload = std::move(payload),
                    },
                }
            );
        }
    }
    const auto* literal = std::get_if<ASTLiteral>(&value.value);
    if (literal != nullptr
        && std::holds_alternative<HIRBuiltinTypeValue>(builder.type(expected).value)) {
        const auto inferred = literal_type(module_analysis, *literal);
        const auto numeric = as_numeric_literal(*literal);
        const auto contextual_numeric = numeric.has_value()
            && numeric_suffix(*numeric) == NumericSuffix::None
            && is_numeric(module_analysis, expected)
            && is_numeric(module_analysis, inferred)
            && (is_integer(module_analysis, expected) == is_integer(module_analysis, inferred));
        if (!compatible(module_analysis, expected, inferred) && !contextual_numeric) {
            const auto result = build_expression(module_analysis, scopes, control, id);
            module_analysis.emit(value.span, std::string(mismatch_message), mismatch_code);
            return result;
        }
        const auto fact = literal_fact(module_analysis, *literal, expected);
        return append_expression(
            module_analysis,
            {
                .origin = module_analysis.origin(value.span),
                .type = expected,
                .constant = fact.constant,
                .value = HIRLiteralExpr {
                    .value = fact.value,
                },
            }
        );
    }
    const auto result = build_expression(module_analysis, scopes, control, id, expected);
    const auto inferred = expression_type(module_analysis, result);
    if (!compatible(module_analysis, expected, inferred)) {
        module_analysis.emit(value.span, std::string(mismatch_message), mismatch_code);
        return result;
    }
    defer_callable_compatibility(
        module_analysis,
        result,
        expected,
        builder.expression(result).origin,
        mismatch_code,
        mismatch_message
    );
    const auto& result_type = builder.type(inferred).value;
    const auto* closure = std::get_if<HIRClosureTypeValue>(&result_type);
    const auto callable =
        closure != nullptr || std::holds_alternative<HIRFunctionTypeValue>(result_type);
    if (callable && std::holds_alternative<HIRFunctionRefTypeValue>(builder.type(expected).value)) {
        const auto* name = std::get_if<HIRNameExpr>(&builder.expression(result).value);
        const auto allowed_lvalue = name != nullptr
            && (usage == ExpectedExpressionUsage::Binding
                || usage == ExpectedExpressionUsage::CallArgument);
        if (closure != nullptr && closure->capturing && !allowed_lvalue) {
            module_analysis.emit(
                value.span,
                "capturing local closure cannot initialize an escaping function view",
                DiagnosticCode::TypeCallableViewEscape
            );
        }
        return append_expression(
            module_analysis,
            {
                .origin = module_analysis.origin(value.span),
                .type = expected,
                .constant = std::nullopt,
                .value = HIRCallableViewExpr {
                    .source = result,
                },
            }
        );
    }
    if (std::holds_alternative<HIRForeignTypeValue>(result_type)) {
        builder.expression(result).type = expected;
    }
    return result;
}

auto build_construction_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTConstructionExpr& construction,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    auto& builder = module_analysis.builder();
    const auto result_type = construction_type(module_analysis, scopes, control, construction.type);
    const auto signature_resolution =
        resolve_structure_contract(module_analysis, result_type, construction.type.span);
    const auto result_is_error =
        std::holds_alternative<HIRErrorTypeValue>(builder.type(result_type).value);
    if (!signature_resolution.has_value()) {
        if (signature_resolution.error() == LookupError::Missing && !result_is_error) {
            module_analysis.emit(
                construction.type.span,
                "construction syntax requires a structure type",
                DiagnosticCode::TypeConstructNotStruct
            );
        }
        std::visit(
            Overloaded {
                [](const std::monostate&) static noexcept {},
                [&](const ASTPositionalInitializerList& positional) noexcept {
                    for (const auto value : positional.values) {
                        static_cast<void>(
                            build_expression(module_analysis, scopes, control, value)
                        );
                    }
                },
                [&](const ASTFieldInitializerList& fields) noexcept {
                    for (const auto& field : fields.fields) {
                        static_cast<void>(
                            build_expression(module_analysis, scopes, control, field.value)
                        );
                    }
                },
            },
            construction.initializer.value
        );
        return module_analysis.recover_expression(construction.type.span);
    }
    const auto signature = signature_resolution.value();

    auto values = std::vector<HIRFieldInitializer>();
    auto valid_mapping = true;
    std::visit(
        Overloaded {
            [&](const std::monostate&) noexcept {
                if (!signature.fields.empty()) {
                    module_analysis.emit(
                        construction.type.span,
                        "structure construction must initialize every field",
                        DiagnosticCode::TypeConstructArity
                    );
                    valid_mapping = false;
                }
            },
            [&](const ASTPositionalInitializerList& positional) noexcept {
                values.reserve(std::min(positional.values.size(), signature.fields.size()));
                for (auto index = 0uz; index < positional.values.size(); ++index) {
                    const auto value = index < signature.fields.size()
                        ? build_expected_expression(
                              module_analysis,
                              scopes,
                              control,
                              positional.values[index],
                              signature.fields[index].type,
                              ExpectedExpressionUsage::Construction
                          )
                        : build_expression(
                              module_analysis,
                              scopes,
                              control,
                              positional.values[index]
                          );
                    if (index < signature.fields.size()) {
                        values.push_back({
                            .value = value,
                            .declaration_index = static_cast<std::uint32_t>(index),
                        });
                    }
                }
                if (positional.values.size() != signature.fields.size()) {
                    module_analysis.emit(
                        positional.span,
                        "positional construction must initialize every structure field",
                        DiagnosticCode::TypeConstructArity
                    );
                    valid_mapping = false;
                }
            },
            [&](const ASTFieldInitializerList& fields) noexcept {
                auto initialized = std::flat_map<std::string, Span, std::less<>>();
                auto declared_initialized = std::vector<bool>(signature.fields.size(), false);
                values.reserve(std::min(fields.fields.size(), signature.fields.size()));
                for (const auto& field : fields.fields) {
                    const auto name = module_analysis.spelling(field.name_span);
                    const auto found = initialized.find(name);
                    if (found != initialized.end()) {
                        auto diagnostic = DiagnosticBuilder(
                            DiagnosticCode::TypeConstructDuplicateField,
                            "a construction field is initialized more than once"
                        );
                        diagnostic.primary(
                            locate(module_analysis.source_id(), field.name_span),
                            "duplicate initializer"
                        );
                        diagnostic.related(
                            locate(module_analysis.source_id(), found->second),
                            "first initializer"
                        );
                        module_analysis.emit(diagnostic.build());
                        static_cast<void>(
                            build_expression(module_analysis, scopes, control, field.value)
                        );
                        valid_mapping = false;
                        continue;
                    }
                    initialized.emplace(std::string(name), field.name_span);
                    const auto declared = std::ranges::find_if(
                        signature.fields,
                        [&](const HIRStructField& candidate) noexcept -> bool {
                            return builder.provenance().spelling(candidate.name) == name;
                        }
                    );
                    if (declared == signature.fields.end()) {
                        module_analysis.emit(
                            field.name_span,
                            std::format("structure has no field named '{}'", name),
                            DiagnosticCode::TypeConstructUnknownField
                        );
                        static_cast<void>(
                            build_expression(module_analysis, scopes, control, field.value)
                        );
                        valid_mapping = false;
                        continue;
                    }
                    const auto declaration_index =
                        static_cast<std::size_t>(std::distance(signature.fields.begin(), declared));
                    declared_initialized[declaration_index] = true;
                    values.push_back({
                        .value = expression_expected_diagnosing(
                            module_analysis,
                            scopes,
                            control,
                            field.value,
                            declared->type,
                            DiagnosticCode::TypeConstructField,
                            "construction field has an incompatible type",
                            ExpectedExpressionUsage::Construction
                        ),
                        .declaration_index = static_cast<std::uint32_t>(declaration_index),
                    });
                }
                if (fields.fields.size() != signature.fields.size()
                    || !std::ranges::all_of(declared_initialized, std::identity {})) {
                    module_analysis.emit(
                        fields.span,
                        "named construction must initialize every structure field",
                        DiagnosticCode::TypeConstructArity
                    );
                    valid_mapping = false;
                }
            },
        },
        construction.initializer.value
    );
    if (!valid_mapping) {
        return module_analysis.recover_expression(construction.type.span);
    }
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = result_type,
            .constant = std::nullopt,
            .value = HIRConstructionExpr {
                .fields = std::move(values),
            },
        }
    );
}
