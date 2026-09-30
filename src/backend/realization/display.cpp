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
import :support.invariant;
import std;

namespace {

// Projections are rebuilt from stable names; reading a field has no execution effects.
auto display_statements(
    ModuleLowering& context,
    TypeID type,
    TargetLocalID writer,
    const std::function<TargetExpr()>& value
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
    const auto line = [&](bool outer = false) noexcept {
        emit("line", outer ? target_expressions(integer_expression(1)) : std::vector<TargetExpr>());
    };
    const auto child = [&](TypeID child_type,
                           const std::function<TargetExpr()>& projection) noexcept {
        body.push_back(generated_statement(
            TargetExprStmt {
                .expression = call_expression(
                    context.display_emitter(child_type),
                    target_expressions(name_expression(writer), projection())
                )
            }
        ));
    };
    const auto& canonical = context.semantic().types().type(type).value;
    if (const auto* structure = std::get_if<StructTypeValue>(&canonical)) {
        const auto& declaration = context.semantic().declarations().structure(structure->structure);
        const auto name = context.semantic().provenance().spelling(declaration.name);
        if (declaration.kind == RecordKind::Class) {
            body.push_back(generated_statement(TargetDiscardStmt {.expression = value()}));
            text(std::string(name));
        } else {
            text(std::string(name) + " {");
            for (auto index = 0uz; index < declaration.fields.size(); ++index) {
                const auto& field = declaration.fields[index];
                line();
                text(std::string(context.semantic().provenance().spelling(field.name)) + ": ");
                child(field.type, [&]() noexcept {
                    return member_expression(
                        value(),
                        context.field_identifier(structure->structure, index)
                    );
                });
                text(",");
            }
            if (declaration.fields.empty()) {
                body.push_back(generated_statement(TargetDiscardStmt {.expression = value()}));
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
            auto saved = std::move(body);
            body = std::vector<TargetStmt>();
            text(
                std::string(context.semantic().provenance().spelling(declaration.name))
                + "::" + std::string(context.semantic().provenance().spelling(item.name))
            );
            auto condition = bool_expression(false);
            if (std::holds_alternative<NumericEnumRepresentation>(declaration.representation)) {
                condition = binary_expression(
                    value(),
                    TargetBinaryOperator::Equal,
                    enum_case_expression(context, id, {})
                );
            } else {
                const auto projection = [&]() noexcept {
                    return call_member(
                        value(),
                        context.payload_enum(enumeration->enumeration)
                            .cases[index]
                            .projection_function.spelling(),
                        {}
                    );
                };
                condition = binary_expression(
                    projection(),
                    TargetBinaryOperator::NotEqual,
                    intrinsic_expression(TargetSymbol::StdNullptr)
                );
                if (!item.payload_types.empty()) {
                    text("(");
                    for (auto field = 0uz; field < item.payload_types.size(); ++field) {
                        line();
                        child(item.payload_types[field], [&]() noexcept {
                            return member_expression(
                                dereference_expression(projection()),
                                enum_payload_field_identifier(field)
                            );
                        });
                        text(",");
                    }
                    line(true);
                    text(")");
                }
            }
            branches.push_back({.condition = std::move(condition), .body = std::move(body)});
            body = std::move(saved);
        }
        body.push_back(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    }
    emit("leave", {});
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back({
        .condition = call_member(name_expression(writer), "enter", {}),
        .body = std::move(body),
    });
    auto result = std::vector<TargetStmt>();
    result.push_back(generated_statement(
        TargetIfStmt {
            .branches = std::move(branches),
            .else_body = std::nullopt,
        }
    ));
    return result;
}

} // namespace

auto ModuleLowering::display_emitter(TypeID type) noexcept -> TargetExpr {
    auto found = display_types.find(type);
    if (found == display_types.end()) {
        const auto& canonical = semantic().types().type(type).value;
        const auto construction = [](TargetTypeID type) static noexcept -> TargetExpr {
            return {.value = TargetConstructionExpr {.type = type, .initializer = {}}};
        };
        const auto aggregate = [&](TargetSymbol symbol, TypeID element) noexcept -> TargetExpr {
            const auto emitter = display_emitter(element);
            const auto* child = std::get_if<TargetConstructionExpr>(&emitter.value);
            if (child == nullptr) {
                invariant_violation("display emitter must be a construction");
            }
            return construction(
                target().intern_type({
                    .value =
                        TargetIntrinsicType {.symbol = symbol, .type_argument_ids = {child->type}},
                    .const_qualified = false,
                })
            );
        };
        if (const auto* array = std::get_if<ArrayTypeValue>(&canonical)) {
            return aggregate(TargetSymbol::RuntimeSequenceDisplay, array->element);
        }
        if (const auto* slice = std::get_if<SliceTypeValue>(&canonical)) {
            return aggregate(TargetSymbol::RuntimeSequenceDisplay, slice->element);
        }
        if (const auto* range = std::get_if<RangeTypeValue>(&canonical)) {
            return aggregate(TargetSymbol::RuntimeRangeDisplay, range->element);
        }
        if (!std::holds_alternative<StructTypeValue>(canonical)
            && !std::holds_alternative<EnumTypeValue>(canonical)) {
            return construction(intrinsic_type(TargetSymbol::RuntimeScalarDisplay));
        }
        const auto name = names().display_identifier(type);
        const auto helper_type = named_type(names().module_support_name(active_module(), name));
        display_types.emplace(type, helper_type);
        auto locals = make_callable_name_allocator();
        const auto scope = TargetScopeID {.ordinal = 0};
        const auto writer = target().add_local(locals.local_symbol("writer", 0, scope));
        const auto value = target().add_local(locals.local_symbol("value", 1, scope));
        auto body = display_statements(*this, type, writer, [&]() noexcept {
            return name_expression(value);
        });
        auto members = std::vector<TargetRecordMember>();
        members.push_back(
            TargetMemberFunctionDecl {
                .name = TargetOperatorName::Call,
                .parameters = target_parameters(
                    {.local = std::nullopt,
                     .type = reference_type(intrinsic_type(TargetSymbol::RuntimeDisplayWriter)),
                     .default_value = std::nullopt},
                    {.local = std::nullopt,
                     .type = reference_type(lower_type(type), true),
                     .default_value = std::nullopt}
                ),
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
                .parameters = target_parameters(
                    {.local = writer,
                     .type = reference_type(intrinsic_type(TargetSymbol::RuntimeDisplayWriter)),
                     .default_value = std::nullopt},
                    {.local = value,
                     .type = reference_type(lower_type(type), true),
                     .default_value = std::nullopt}
                ),
                .result = intrinsic_type(TargetSymbol::Void),
                .body = std::move(body),
                .const_qualified = true,
                .inline_specifier = true,
            }},
            TargetCompilerReason::ArtifactScaffolding
        ));
        found = display_types.find(type);
    }
    return {.value = TargetConstructionExpr {.type = found->second, .initializer = {}}};
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
