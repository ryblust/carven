module carven:semantic.analysis.body.literal.impl;

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
import :semantic.analysis.constant.proof;
import :semantic.analysis.coverage;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace body_elaboration {

auto BodyElaborator::literal_expression(
    const ASTLiteral& literal,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<BuiltExpression> {
    auto normalized = normalize_literal(draft(), literal, expected);
    if (!normalized.has_value()) {
        const auto diagnostic = constant_evaluation_diagnostic(normalized.error());
        return std::unexpected(fail(
            span,
            diagnostic.has_value() ? diagnostic->code : DiagnosticCode::ConstLiteralRange,
            diagnostic.has_value() ? std::string(diagnostic->message) : "invalid literal value"
        ));
    }
    const auto constant = draft().intern_constant(normalized->constant);
    const auto value = active_builder().append_value(
        normalized->constant.type,
        active_builder().lifetime(),
        SemLiteral {.value = normalized->literal},
        origin(span)
    );
    return BuiltExpression {
        .type = normalized->constant.type,
        .storage = value,
        .constant = constant,
        .pending_failures = {},
    };
}

auto BodyElaborator::name_expression(const ASTNameExpr& name, Span span) noexcept
    -> AnalysisResult<BuiltExpression> {
    const auto text = spelling(name.name_span);
    if (const auto* local = use_local(text)) {
        if (const auto* constant = std::get_if<ConstantID>(&local->storage)) {
            const auto value = active_builder().append_value(
                local->type,
                active_builder().lifetime(),
                SemConstant {.constant = *constant},
                origin(span)
            );
            return BuiltExpression {
                .type = local->type,
                .storage = value,
                .constant = *constant,
                .pending_failures = {},
                .takeable = false,
            };
        }
        if (const auto* value = std::get_if<ExpressionHandle>(&local->storage)) {
            return BuiltExpression {
                .type = local->type,
                .storage = *value,
                .constant = std::nullopt,
                .pending_failures = {},
                .takeable = local->takeable,
            };
        }
        if (const auto* place = std::get_if<PlaceHandle>(&local->storage)) {
            return BuiltExpression {
                .type = local->type,
                .storage = *place,
                .constant = std::nullopt,
                .pending_failures = {},
                .takeable = local->takeable,
            };
        }
        const auto& runtime = std::get<BoundStorage>(local->storage);
        if (local->role == LocalRole::RangeRead) {
            const auto value = active_builder().append_value(
                local->type,
                active_builder().lifetime(),
                expression_construction::Read {runtime.root_place},
                origin(span)
            );
            return BuiltExpression {
                .type = local->type,
                .storage = value,
                .constant = std::nullopt,
                .pending_failures = {},
                .takeable = false
            };
        }
        return BuiltExpression {
            .type = local->type,
            .storage = runtime.root_place,
            .constant = std::nullopt,
            .pending_failures = {},
            .takeable = local->takeable,
        };
    }
    if (catalog().lookup(source_module_id, text).empty()) {
        auto admitted = false;
        for (const auto& binding : catalog().cpp_imports(source_module_id)) {
            admitted |= binding.opens_namespace;
            if (!binding.opens_namespace && binding.components.back() == text) {
                admitted = true;
                import_usage().record_cpp(source_module_id, binding.origin);
            }
        }
        if (admitted) {
            return cpp_expression(
                CppNameOperation {.module_id = semantic_module_id, .name = text},
                {},
                span
            );
        }
    }
    auto selected = find_global(text, name.name_span);
    if (!selected.has_value()) {
        return std::unexpected(selected.error());
    }
    return std::visit(
        Overloaded {
            [&](const CatalogFunctionForm& function) noexcept -> AnalysisResult<BuiltExpression> {
                return BuiltExpression {
                    .type = draft().intern_type(
                        CanonicalType {
                            .value = FunctionTypeValue {.callable = function.callable},
                        }
                    ),
                    .storage = DirectCallable {.callable = function.callable},
                    .constant = std::nullopt,
                    .pending_failures = {},
                };
            },
            [&](const CatalogConstantForm& constant) noexcept -> AnalysisResult<BuiltExpression> {
                const auto declaration =
                    draft().construction_module_constant_declaration_copy(constant.constant);
                const auto value = active_builder().append_value(
                    declaration.type,
                    active_builder().lifetime(),
                    SemConstant {.constant = declaration.value},
                    origin(span)
                );
                return BuiltExpression {
                    .type = declaration.type,
                    .storage = value,
                    .constant = declaration.value,
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

auto BodyElaborator::array_expression(
    const ASTArrayExpr& array,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<BuiltExpression> {
    auto expected_element = std::optional<ConstructionTypeRef>();
    if (expected.has_value()) {
        if (const auto* concrete = std::get_if<TypeID>(&*expected)) {
            const auto canonical = draft().type_copy(*concrete);
            if (const auto* type = std::get_if<ArrayTypeValue>(&canonical.value)) {
                if (type->extent != array.element_ids.size()) {
                    return std::unexpected(fail(
                        span,
                        DiagnosticCode::TypeMismatch,
                        "array literal length differs from its expected type"
                    ));
                }
                expected_element = type->element;
            }
        } else {
            const auto construction =
                draft().construction_type_copy(std::get<TypeTermID>(*expected));
            if (const auto* type = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
                if (type->extent != array.element_ids.size()) {
                    return std::unexpected(fail(
                        span,
                        DiagnosticCode::TypeMismatch,
                        "array literal length differs from its expected type"
                    ));
                }
                expected_element = type->element;
            }
        }
    }
    if (array.element_ids.empty() && !expected_element.has_value()) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeEmptyArray,
            "empty array literal requires an expected array type"
        ));
    }
    auto elements = std::vector<ExpressionHandle>();
    auto pending_failures = PendingFailureTerms();
    auto element_type = expected_element;
    auto completes = true;
    for (const auto element_id : array.element_ids) {
        auto element = expression(element_id, element_type);
        if (!element.has_value()) {
            return std::unexpected(element.error());
        }
        completes &= element->completes;
        append_pending(pending_failures, take_pending(*element));
        if (!element_type.has_value()) {
            auto inferred = infer_value_type(*element, ast.expression(element_id).span);
            if (!inferred.has_value()) {
                return std::unexpected(inferred.error());
            }
            element_type = *inferred;
        } else {
            auto coerced = coerce_to(*element, *element_type, ast.expression(element_id).span);
            if (!coerced.has_value()) {
                return std::unexpected(coerced.error());
            }
        }
        auto value = as_value(*element, ast.expression(element_id).span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        elements.push_back(*value);
    }
    if (!element_type.has_value()) {
        invariant_violation("empty expected array lost its element type");
    }
    const auto type = expected.value_or(
        draft().append_construction_type(
            ConstructionType {
                .value = ConstructionArrayTypeValue {
                    .element = *element_type,
                    .extent = array.element_ids.size(),
                },
            }
        )
    );
    const auto value = active_builder().append_value(
        type,
        active_builder().lifetime(),
        expression_construction::Array {.elements = std::move(elements)},
        origin(span)
    );
    return BuiltExpression {
        .type = type,
        .storage = value,
        .constant = std::nullopt,
        .pending_failures = std::move(pending_failures),
        .completes = completes,
    };
}

auto BodyElaborator::construction_expression(const ASTConstructionExpr& source, Span span) noexcept
    -> AnalysisResult<BuiltExpression> {
    auto resolved = resolve_construction_type(source.type);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }
    if (is_cpp_type(*resolved)) {
        auto operands = std::vector<SemCallArgument<ConstructionTypeRef, FailureTermID>>();
        if (const auto* values =
                std::get_if<ASTPositionalInitializerList>(&source.initializer.value)) {
            for (auto value_id : values->values) {
                auto access = AccessMode::Read;
                if (const auto* marker =
                        std::get_if<ASTAccessExpr>(&ast.expression(value_id).value)) {
                    access =
                        marker->mode == ASTAccessMode::Write ? AccessMode::Write : AccessMode::Take;
                    value_id = marker->operand_id;
                }
                auto value = expression(value_id);
                if (!value.has_value()) {
                    return std::unexpected(value.error());
                }
                if (access == AccessMode::Write) {
                    auto place = as_place(*value, ast.expression(value_id).span);
                    if (!place.has_value()) {
                        return std::unexpected(place.error());
                    }
                    operands.push_back(
                        {.access = access, .expression = active_builder().take_place(*place)}
                    );
                } else {
                    auto read = as_value(*value, ast.expression(value_id).span, access);
                    if (!read.has_value()) {
                        return std::unexpected(read.error());
                    }
                    operands.push_back(
                        {.access = access, .expression = active_builder().take_value(*read)}
                    );
                }
            }
        } else if (std::holds_alternative<ASTFieldInitializerList>(source.initializer.value)) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::TypeConstructNotStruct,
                "named initializers require a Carven structure"
            ));
        }
        return cpp_expression(CppConstructOperation {}, std::move(operands), span, *resolved);
    }
    const auto* concrete = std::get_if<TypeID>(&*resolved);
    if (concrete == nullptr) {
        return std::unexpected(fail(
            source.type.span,
            DiagnosticCode::TypeConstructNotStruct,
            "callable-view types cannot be aggregate-constructed"
        ));
    }
    const auto canonical = draft().type_copy(*concrete);
    const auto* structure = std::get_if<StructTypeValue>(&canonical.value);
    if (structure == nullptr) {
        return std::unexpected(fail(
            source.type.span,
            DiagnosticCode::TypeConstructNotStruct,
            "construction expression requires a structure type"
        ));
    }
    const auto declaration = draft().construction_struct_declaration_copy(structure->structure);
    auto fields = std::vector<expression_construction::StructField>();
    auto pending_failures = PendingFailureTerms();
    auto completes = true;
    const auto append_field = [&](std::size_t index,
                                  ASTExprID value_id) noexcept -> AnalysisResult<void> {
        if (index >= declaration.fields.size()) {
            return std::unexpected(fail(
                ast.expression(value_id).span,
                DiagnosticCode::TypeConstructArity,
                "too many structure initializer values"
            ));
        }
        auto value = expression(value_id, declaration.fields[index].type);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        completes &= value->completes;
        append_pending(pending_failures, take_pending(*value));
        if (!compatible(value->type, declaration.fields[index].type)) {
            return std::unexpected(fail(
                ast.expression(value_id).span,
                DiagnosticCode::TypeConstructField,
                "structure initializer has an incompatible field type"
            ));
        }
        auto operand = as_value(*value, ast.expression(value_id).span, AccessMode::Read);
        if (!operand.has_value()) {
            return std::unexpected(operand.error());
        }
        fields.push_back(
            expression_construction::StructField {
                .declaration_index = static_cast<std::uint32_t>(index),
                .value = *operand,
            }
        );
        return {};
    };
    auto result = std::visit(
        Overloaded {
            [&](const std::monostate&) noexcept -> AnalysisResult<void> {
                if (!declaration.fields.empty()) {
                    return std::unexpected(fail(
                        span,
                        DiagnosticCode::TypeConstructArity,
                        "structure initializer omits required fields"
                    ));
                }
                return {};
            },
            [&](const ASTPositionalInitializerList& values) noexcept -> AnalysisResult<void> {
                if (values.values.size() != declaration.fields.size()) {
                    return std::unexpected(fail(
                        values.span,
                        DiagnosticCode::TypeConstructArity,
                        "positional initializer count differs from structure fields"
                    ));
                }
                for (auto index = 0uz; index < values.values.size(); ++index) {
                    auto appended = append_field(index, values.values[index]);
                    if (!appended.has_value()) {
                        return std::unexpected(appended.error());
                    }
                }
                return {};
            },
            [&](const ASTFieldInitializerList& values) noexcept -> AnalysisResult<void> {
                auto initialized = std::vector<std::uint8_t>(declaration.fields.size(), 0u);
                for (const auto& source_field : values.fields) {
                    const auto name = spelling(source_field.name_span);
                    const auto found = std::ranges::find(
                        declaration.fields,
                        name,
                        [&](const ConstructionStructField& field) noexcept {
                            return draft().spelling_copy(field.name);
                        }
                    );
                    if (found == declaration.fields.end()) {
                        return std::unexpected(fail(
                            source_field.name_span,
                            DiagnosticCode::TypeConstructUnknownField,
                            std::format("structure has no field named '{}'", name)
                        ));
                    }
                    const auto index =
                        static_cast<std::size_t>(std::distance(declaration.fields.begin(), found));
                    if (initialized[index] != 0u) {
                        return std::unexpected(fail(
                            source_field.name_span,
                            DiagnosticCode::TypeConstructDuplicateField,
                            std::format("field '{}' is initialized more than once", name)
                        ));
                    }
                    initialized[index] = 1u;
                    auto appended = append_field(index, source_field.value);
                    if (!appended.has_value()) {
                        return std::unexpected(appended.error());
                    }
                }
                if (std::ranges::contains(initialized, 0u)) {
                    return std::unexpected(fail(
                        values.span,
                        DiagnosticCode::TypeConstructArity,
                        "named initializer omits one or more fields"
                    ));
                }
                return {};
            },
        },
        source.initializer.value
    );
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    const auto value = active_builder().append_value(
        *concrete,
        active_builder().lifetime(),
        expression_construction::Struct {
            .structure = structure->structure,
            .fields = std::move(fields)
        },
        origin(span)
    );
    return BuiltExpression {
        .type = *resolved,
        .storage = value,
        .constant = std::nullopt,
        .pending_failures = std::move(pending_failures),
        .completes = completes,
    };
}


} // namespace body_elaboration
