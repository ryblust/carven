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

// Projections are rebuilt from stable names; reading a field has no execution effects.
auto display_statements(
    ModuleLowering& context,
    TypeID type,
    TargetLocalID writer,
    const std::function<TargetExpr()>& value,
    std::size_t depth
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
    const auto child = [&](TypeID child_type,
                           const std::function<TargetExpr()>& projection) noexcept {
        const auto& child = context.semantic().types().type(child_type).value;
        if (depth + 1 < 8
            && (std::holds_alternative<StructTypeValue>(child)
                || std::holds_alternative<EnumTypeValue>(child)
                || std::holds_alternative<ArrayTypeValue>(child)
                || std::holds_alternative<SliceTypeValue>(child)
                || std::holds_alternative<RangeTypeValue>(child))) {
            body.push_back(generated_statement(
                TargetExprStmt {
                    .expression = call_expression(
                        context.display_emitter(child_type, depth + 1),
                        target_expressions(name_expression(writer), projection())
                    )
                }
            ));
        } else {
            body.append_range(
                display_statements(context, child_type, writer, projection, depth + 1)
                | std::views::as_rvalue
            );
        }
    };
    const auto& canonical = context.semantic().types().type(type).value;
    if (depth == 8) {
        body.push_back(generated_statement(TargetDiscardStmt {.expression = value()}));
        text("...");
    } else if (const auto* structure = std::get_if<StructTypeValue>(&canonical)) {
        const auto& declaration = context.semantic().declarations().structure(structure->structure);
        text(std::string(context.semantic().provenance().spelling(declaration.name)) + " {");
        for (auto index = 0uz; index < declaration.fields.size(); ++index) {
            const auto& field = declaration.fields[index];
            text(
                "\n" + std::string((depth + 1) * 4, ' ')
                + std::string(context.semantic().provenance().spelling(field.name)) + ": "
            );
            child(field.type, [&]() noexcept {
                return member_expression(
                    value(),
                    context.name_allocator().source(
                        context.semantic().provenance().spelling(field.name),
                        context.semantic().provenance().spelling(declaration.name)
                    )
                );
            });
            text(",");
        }
        if (declaration.fields.empty()) {
            body.push_back(generated_statement(TargetDiscardStmt {.expression = value()}));
        }
        text(declaration.fields.empty() ? "}" : "\n" + std::string(depth * 4, ' ') + "}");
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
                        text("\n" + std::string((depth + 1) * 4, ' '));
                        child(item.payload_types[field], [&]() noexcept {
                            return member_expression(
                                dereference_expression(projection()),
                                TargetNameAllocator::enum_payload_field(field)
                            );
                        });
                        text(",");
                    }
                    text("\n" + std::string(depth * 4, ' ') + ")");
                }
            }
            branches.push_back({.condition = std::move(condition), .body = std::move(body)});
            body = std::move(saved);
        }
        body.push_back(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    } else if (const auto* array = std::get_if<ArrayTypeValue>(&canonical)) {
        emit(
            "sequence",
            target_expressions(
                value(),
                context.display_emitter(array->element, depth + 1),
                integer_expression(depth)
            )
        );
    } else if (const auto* slice = std::get_if<SliceTypeValue>(&canonical)) {
        emit(
            "sequence",
            target_expressions(
                value(),
                context.display_emitter(slice->element, depth + 1),
                integer_expression(depth)
            )
        );
    } else if (const auto* range = std::get_if<RangeTypeValue>(&canonical)) {
        child(range->element, [&]() noexcept {
            return member_expression(value(), TargetNameAllocator::fixed("first"));
        });
        emit(
            "text",
            target_expressions(
                TargetExpr {
                    .value = TargetConditionalExpr {
                        .condition = target_child(
                            member_expression(value(), TargetNameAllocator::fixed("inclusive"))
                        ),
                        .true_value = target_child(
                            string_expression("..=", TargetStringLiteralKind::StringView)
                        ),
                        .false_value = target_child(
                            string_expression("..", TargetStringLiteralKind::StringView)
                        )
                    }
                }
            )
        );
        child(range->element, [&]() noexcept {
            return member_expression(value(), TargetNameAllocator::fixed("last"));
        });
    } else if (std::holds_alternative<BuiltinTypeValue>(canonical)
               || std::holds_alternative<PointerTypeValue>(canonical)) {
        emit("scalar", target_expressions(value()));
    } else {
        body.push_back(generated_statement(TargetDiscardStmt {.expression = value()}));
        text("<opaque>");
    }
    return body;
}

} // namespace

auto ModuleLowering::display_emitter(TypeID type, std::size_t depth) noexcept -> TargetExpr {
    const auto key = std::pair {type, depth};
    auto found = display_types.find(key);
    if (found == display_types.end()) {
        const auto name = name_allocator().fresh(TargetTemporaryNameKind::Display);
        const auto prefix =
            names().module_names(active_module()).qualified_namespace_name.components();
        auto components = std::vector<TargetIdentifier>(prefix.begin(), prefix.end());
        components.push_back(name);
        const auto helper_type = named_type(TargetName::globally_qualified(std::move(components)));
        auto locals = make_callable_name_allocator();
        const auto scope = TargetScopeID {.ordinal = 0};
        const auto writer = target().add_local(locals.local_symbol("writer", 0, scope));
        const auto value = target().add_local(locals.local_symbol("value", 1, scope));
        auto body = display_statements(
            *this,
            type,
            writer,
            [&]() noexcept { return name_expression(value); },
            depth
        );
        auto members = std::vector<TargetRecordMember>();
        members.push_back(
            TargetMemberFunctionDecl {
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
                .form = TargetMemberFunctionDefinition {.body = std::move(body)},
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
        found = display_types.emplace(key, helper_type).first;
    }
    return {.value = TargetConstructionExpr {.type = found->second, .initializer = {}}};
}

auto ModuleLowering::take_display_helpers() noexcept -> std::vector<TargetItem> {
    return std::exchange(display_helpers, {});
}

auto realize_display(ModuleLowering& context, TypeID type, TargetExpr value) noexcept
    -> TargetExpr {
    return call_expression(
        intrinsic_expression(TargetSymbol::RuntimeStructuralDisplay),
        target_expressions(std::move(value), context.display_emitter(type))
    );
}
