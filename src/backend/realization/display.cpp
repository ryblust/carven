module carven:backend.realization.display.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.realization.display;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.decl;
import :semantic.semir.type;
import :source.provenance;
import std;

namespace {

auto display_statements(
    ModuleLowering& context,
    TypeID type,
    TargetLocalID writer,
    TargetLocalID value,
    TargetLocalID depth
) noexcept -> std::vector<TargetStmt> {
    auto body = std::vector<TargetStmt>();
    const auto emit = [&](std::string_view method, std::vector<TargetExpr> arguments) noexcept {
        body.push_back(generated_statement(
            TargetExprStmt {
                .expression = call_member(name_expression(writer), method, std::move(arguments))
            }
        ));
    };
    const auto text = [&](std::string bytes) noexcept {
        emit(
            "text",
            target_expressions(
                string_expression(std::move(bytes), TargetStringLiteralKind::StringView)
            )
        );
    };
    const auto child_depth = [&]() noexcept {
        return binary_expression(
            name_expression(depth),
            TargetBinaryOperator::Add,
            integer_expression(1)
        );
    };
    const auto line = [&](bool closing = false) noexcept {
        emit("line", target_expressions(closing ? name_expression(depth) : child_depth()));
    };
    const auto child = [&](TypeID child_type, TargetExpr projection) noexcept {
        body.push_back(generated_statement(
            TargetExprStmt {
                .expression = call_expression(
                    context.display_emitter(child_type),
                    target_expressions(
                        name_expression(writer),
                        std::move(projection),
                        child_depth()
                    )
                )
            }
        ));
    };
    const auto& canonical = context.semantic().types().type(type).value;
    if (const auto* structure = std::get_if<StructTypeValue>(&canonical)) {
        const auto& declaration = context.semantic().declarations().structure(structure->structure);
        const auto name = context.semantic().provenance().spelling(declaration.name);
        if (declaration.kind == RecordKind::Class) {
            body.push_back(
                generated_statement(TargetDiscardStmt {.expression = name_expression(value)})
            );
            text(std::string(name));
        } else {
            text(std::string(name) + " {");
            for (auto index = 0uz; index < declaration.fields.size(); ++index) {
                const auto& field = declaration.fields[index];
                line();
                text(std::string(context.semantic().provenance().spelling(field.name)) + ": ");
                child(
                    field.type,
                    member_expression(
                        name_expression(value),
                        context.field_identifier(structure->structure, index)
                    )
                );
                text(",");
            }
            if (declaration.fields.empty()) {
                body.push_back(
                    generated_statement(TargetDiscardStmt {.expression = name_expression(value)})
                );
            }
            if (!declaration.fields.empty()) {
                line(true);
            }
            text("}");
        }
    } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&canonical)) {
        const auto& declaration =
            context.semantic().declarations().enumeration(enumeration->enumeration);
        auto branches = std::vector<TargetIfBranch>();
        for (auto index = 0uz; index < declaration.cases.size(); ++index) {
            const auto id = declaration.cases[index];
            const auto& item = context.semantic().declarations().enum_case(id);
            text(
                std::string(context.semantic().provenance().spelling(declaration.name))
                + "::" + std::string(context.semantic().provenance().spelling(item.name))
            );
            const auto needs_selection = index + 1uz < declaration.cases.size();
            auto condition = std::optional<TargetExpr>();
            if (std::holds_alternative<NumericEnumRepresentation>(declaration.representation)) {
                if (needs_selection) {
                    condition = binary_expression(
                        name_expression(value),
                        TargetBinaryOperator::Equal,
                        enum_case_expression(context, id, {})
                    );
                }
            } else {
                const auto projection = [&]() noexcept {
                    return context.enum_payload_projection(
                        enumeration->enumeration,
                        index,
                        name_expression(value)
                    );
                };
                if (needs_selection) {
                    condition = binary_expression(
                        projection(),
                        TargetBinaryOperator::NotEqual,
                        intrinsic_expression(TargetSymbol::StdNullptr)
                    );
                }
                if (!item.payload_types.empty()) {
                    text("(");
                    for (auto field = 0uz; field < item.payload_types.size(); ++field) {
                        line();
                        child(
                            item.payload_types[field],
                            member_expression(
                                dereference_expression(projection()),
                                enum_payload_field_identifier(field)
                            )
                        );
                        text(",");
                    }
                    line(true);
                    text(")");
                }
            }
            if (condition) {
                branches.push_back({
                    .condition = std::move(*condition),
                    .body = std::exchange(body, {}),
                });
            }
        }
        if (!branches.empty()) {
            auto selection = TargetIfStmt {
                .branches = std::move(branches),
                .else_body = std::move(body),
            };
            body.clear();
            body.push_back(generated_statement(std::move(selection)));
        } else if (std::holds_alternative<NumericEnumRepresentation>(declaration.representation)) {
            body.push_back(
                generated_statement(TargetDiscardStmt {.expression = name_expression(value)})
            );
        }
    }
    auto result = std::vector<TargetStmt>();
    auto limit_body = std::vector<TargetStmt>();
    limit_body.push_back(generated_statement(TargetReturnStmt {.expression = std::nullopt}));
    auto limit_branches = std::vector<TargetIfBranch>();
    limit_branches.push_back({
        .condition = call_member(
            name_expression(writer),
            "truncate_at_limit",
            target_expressions(name_expression(depth))
        ),
        .body = std::move(limit_body),
    });
    result.push_back(generated_statement(
        TargetIfStmt {
            .branches = std::move(limit_branches),
            .else_body = std::nullopt,
        }
    ));
    result.append_range(body | std::views::as_rvalue);
    return result;
}

} // namespace

auto ModuleLowering::display_emitter_type(TypeID type) noexcept -> TargetTypeID {
    if (const auto found = display_types.find(type); found != display_types.end()) {
        return found->second;
    }
    const auto& canonical = semantic().types().type(type).value;
    const auto aggregate = [&](TargetSymbol symbol, TypeID element) noexcept -> TargetTypeID {
        const auto emitter_type = target().intern_type({
            .value =
                TargetIntrinsicType {
                    .symbol = symbol,
                    .type_argument_ids = {display_emitter_type(element)},
                },
            .const_qualified = false,
        });
        display_types.emplace(type, emitter_type);
        return emitter_type;
    };
    if (const auto* array = std::get_if<ArrayTypeValue>(&canonical)) {
        return aggregate(TargetSymbol::RuntimeSequenceDisplay, array->element);
    }
    if (const auto* slice = std::get_if<SliceTypeValue>(&canonical)) {
        return aggregate(TargetSymbol::RuntimeSequenceDisplay, slice->element);
    }
    if (const auto* sequence = std::get_if<OwnedSequenceTypeValue>(&canonical)) {
        return aggregate(TargetSymbol::RuntimeSequenceDisplay, sequence->element);
    }
    if (const auto* range = std::get_if<RangeTypeValue>(&canonical)) {
        return aggregate(TargetSymbol::RuntimeRangeDisplay, range->element);
    }
    if (!std::holds_alternative<StructTypeValue>(canonical)
        && !std::holds_alternative<EnumTypeValue>(canonical)) {
        const auto emitter_type = intrinsic_type(TargetSymbol::RuntimeScalarDisplay);
        display_types.emplace(type, emitter_type);
        return emitter_type;
    }
    const auto name = names().display_identifier(type);
    const auto helper_type = named_type(names().module_support_name(active_module(), name));
    display_types.emplace(type, helper_type);
    auto locals = make_callable_name_allocator();
    const auto scope = TargetScopeID {.ordinal = 0};
    const auto writer = target().add_local(locals.local_symbol("writer", 0, scope));
    const auto value = target().add_local(locals.local_symbol("value", 1, scope));
    const auto depth = target().add_local(locals.local_symbol("depth", 2, scope));
    auto body = display_statements(*this, type, writer, value, depth);
    const auto parameters = [&](bool named) noexcept {
        auto result = std::vector<TargetParameter>();
        result.push_back({
            .local = named ? std::optional(writer) : std::nullopt,
            .type = reference_type(intrinsic_type(TargetSymbol::RuntimeDisplayWriter)),
            .default_value = std::nullopt,
        });
        result.push_back({
            .local = named ? std::optional(value) : std::nullopt,
            .type = reference_type(lower_type(type), true),
            .default_value = std::nullopt,
        });
        result.push_back({
            .local = named ? std::optional(depth) : std::nullopt,
            .type = intrinsic_type(TargetSymbol::StdSize),
            .default_value = std::nullopt,
        });
        return result;
    };
    auto members = std::vector<TargetRecordMember>();
    members.push_back(
        TargetMemberFunctionDecl {
            .name = TargetOperatorName::Call,
            .parameters = parameters(false),
            .result = intrinsic_type(TargetSymbol::Void),
            .form = TargetMemberFunctionDeclaration {},
            .maybe_unused = false,
            .static_specifier = false,
            .constexpr_specifier = false,
            .friend_specifier = false,
            .result_reference = false,
            .const_qualified = true,
        }
    );
    display_helpers.push_back(compiler_item(
        TargetDecl {TargetStructDecl {.name = name, .members = std::move(members)}},
        TargetCompilerReason::ArtifactScaffolding
    ));
    display_definitions.push_back(compiler_item(
        TargetDecl {TargetOutOfClassMemberDefinition {
            .owner = names().module_support_name(active_module(), name),
            .name = TargetOperatorName::Call,
            .parameters = parameters(true),
            .result = intrinsic_type(TargetSymbol::Void),
            .body = std::move(body),
            .const_qualified = true,
            .inline_specifier = true,
        }},
        TargetCompilerReason::ArtifactScaffolding
    ));
    return helper_type;
}

auto ModuleLowering::display_emitter(TypeID type) noexcept -> TargetExpr {
    return template_name_expression(
        intrinsic_expression(TargetSymbol::RuntimeStatelessValue),
        {display_emitter_type(type)}
    );
}

auto ModuleLowering::take_display_helpers() noexcept -> std::vector<TargetItem> {
    auto result = std::exchange(display_helpers, {});
    auto definitions = std::exchange(display_definitions, {});
    result.append_range(definitions | std::views::as_rvalue);
    return result;
}

auto realize_display(ModuleLowering& context, TypeID type, TargetExpr value) noexcept
    -> TargetExpr {
    return call_expression(
        intrinsic_expression(TargetSymbol::RuntimeStructuralDisplay),
        target_expressions(std::move(value), context.display_emitter(type))
    );
}
