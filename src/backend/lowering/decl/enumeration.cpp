module carven:backend.lowering.decl.enumeration.impl;

import :backend.lowering.program;
import :backend.lowering.decl;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir.access;
import :semantic.hir.constant;
import :semantic.hir.decl;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

auto lower_enumeration_declaration(TargetModuleLowerer& context, EnumID enumeration_id) noexcept
    -> TargetItemValue {
    const auto& enumeration = context.source().enumeration(enumeration_id);
    if (enumeration.profile == HIREnumProfile::Numeric) {
        if (!enumeration.underlying_type.has_value()) {
            invariant_violation("numeric enumeration has no underlying type");
        }
        auto cases = std::vector<TargetEnumCase> {};
        cases.reserve(enumeration.cases.size());
        for (const auto enum_case_id : enumeration.cases) {
            const auto& enum_case = context.source().enum_case(enum_case_id);
            if (!enum_case.constant.has_value()) {
                invariant_violation("numeric enum case has no normalized constant fact");
            }
            const auto* constant = std::get_if<HIRNumericEnumConstant>(
                &context.source().constant(*enum_case.constant).value
            );
            if (constant == nullptr) {
                invariant_violation("numeric enum case has an invalid constant fact");
            }
            const auto value = context.target().append_expression({
                .value = TargetLiteralExpr {
                    .value = lower_literal(
                        context,
                        HIRIntegerLiteralValue {
                            .negative = constant->value.negative(),
                            .magnitude = constant->value.magnitude(),
                        },
                        *enumeration.underlying_type
                    ),
                },
            });
            cases.push_back({
                .name = symbol_identifier(context, enum_case.symbol),
                .value = value,
            });
        }
        return TargetDecl {TargetEnumDecl {
            .name = symbol_identifier(context, enumeration.symbol),
            .underlying_type = lower_type(context, *enumeration.underlying_type),
            .cases = std::move(cases),
        }};
    }
    struct LoweredCase final {
        TargetIdentifier name;
        std::vector<TargetTypeID> payload_types;
    };
    auto members = std::vector<LoweredCase>();
    members.reserve(enumeration.cases.size());
    for (const auto member_id : enumeration.cases) {
        const auto& member = context.source().enum_case(member_id);
        auto payload_types = std::vector<TargetTypeID>();
        payload_types.reserve(member.payload_types.size());
        for (const auto type : member.payload_types) {
            payload_types.push_back(lower_type(context, type));
        }
        members.push_back({
            .name = symbol_identifier(context, member.symbol),
            .payload_types = std::move(payload_types),
        });
    }
    const auto name = symbol_identifier(context, enumeration.symbol);
    const auto& representation_names = context.payload_enum(enumeration_id);
    const auto enum_type = lower_type(context, *context.source().symbol(enumeration.symbol).type);
    const auto equality =
        context.source().nominal_capabilities(HIRNominalDeclRef {enumeration_id}).equality;
    const auto bool_type = intrinsic_type(context, TargetSymbol::Bool);
    const auto storage_name = representation_names.storage_member;
    const auto storage_value = name_expression(context, TargetName {storage_name});

    auto public_members = std::vector<TargetClassMember> {};
    for (auto case_index = 0uz; case_index < members.size(); ++case_index) {
        const auto& sum_case = members[case_index];
        if (sum_case.payload_types.empty()) {
            public_members.push_back(
                TargetMemberVariable {
                    .type = enum_type,
                    .name = sum_case.name,
                    .static_specifier = true,
                    .const_specifier = true,
                }
            );
        } else {
            auto parameters = std::vector<TargetParameter> {};
            for (auto index = 0uz; index < sum_case.payload_types.size(); ++index) {
                parameters.push_back({
                    .name = TargetNameAllocator::enum_payload_field(index),
                    .type = parameter_type(
                        context,
                        HIRAccessMode::Read,
                        context.source()
                            .enum_case(enumeration.cases[case_index])
                            .payload_types[index],
                        sum_case.payload_types[index]
                    ),
                });
            }
            public_members.push_back(
                TargetMemberFunctionDecl {
                    .name = sum_case.name,
                    .parameters = std::move(parameters),
                    .result = enum_type,
                    .body = {},
                    .static_specifier = true,
                    .constexpr_specifier = false,
                    .friend_specifier = false,
                    .declaration_only = true,
                    .defaulted = false,
                    .result_reference = false,
                    .const_qualified = false,
                }
            );
        }
    }
    if (equality) {
        public_members.push_back(
            TargetMemberFunctionDecl {
                .name = TargetOperatorName::Equality,
                .parameters =
                    {
                        {.name = std::nullopt, .type = reference_type(context, enum_type, true)},
                        {.name = std::nullopt, .type = reference_type(context, enum_type, true)},
                    },
                .result = bool_type,
                .body = {},
                .static_specifier = false,
                .constexpr_specifier = true,
                .friend_specifier = true,
                .declaration_only = false,
                .defaulted = true,
                .result_reference = false,
                .const_qualified = false,
            }
        );
    }

    auto private_members = std::vector<TargetClassMember> {};
    {
        auto alternative_type_ids = std::vector<TargetTypeID> {};
        for (auto case_index = 0uz; case_index < members.size(); ++case_index) {
            const auto record_name = representation_names.cases[case_index].record_type;
            auto record_members = std::vector<TargetRecordMember> {};
            for (auto payload_index = 0uz; payload_index < members[case_index].payload_types.size();
                 ++payload_index) {
                record_members.push_back(
                    TargetStructField {
                        .name = TargetNameAllocator::enum_payload_field(payload_index),
                        .type = members[case_index].payload_types[payload_index],
                    }
                );
            }
            if (equality) {
                const auto record_type = named_type(context, TargetName {record_name});
                record_members.push_back(
                    TargetMemberFunctionDecl {
                        .name = TargetOperatorName::Equality,
                        .parameters =
                            {
                                {.name = std::nullopt,
                                 .type = reference_type(context, record_type, true)},
                                {.name = std::nullopt,
                                 .type = reference_type(context, record_type, true)},
                            },
                        .result = bool_type,
                        .body = {},
                        .static_specifier = false,
                        .constexpr_specifier = true,
                        .friend_specifier = true,
                        .declaration_only = false,
                        .defaulted = true,
                        .result_reference = false,
                        .const_qualified = false,
                    }
                );
            }
            private_members.push_back(
                TargetNestedRecord {
                    .name = record_name,
                    .members = std::move(record_members),
                }
            );
            alternative_type_ids.push_back(named_type(context, TargetName {record_name}));
        }
        const auto variant_type = context.target().intern_type({
            .value =
                TargetIntrinsicType {
                    .symbol = TargetSymbol::StdVariant,
                    .type_argument_ids = std::move(alternative_type_ids),
                },
            .const_qualified = false,
        });
        private_members.push_back(
            TargetTypeAlias {
                .name = representation_names.storage_type,
                .type = variant_type,
            }
        );
        const auto storage_type =
            named_type(context, TargetName {representation_names.storage_type});
        private_members.push_back(
            TargetMemberVariable {
                .type = storage_type,
                .name = storage_name,
                .static_specifier = false,
                .const_specifier = false,
            }
        );
        private_members.push_back(
            TargetConstructorDecl {
                .name = name,
                .template_type_parameters = {representation_names.storage_parameter},
                .parameters =
                    {{.name = storage_name,
                      .type = reference_type(
                          context,
                          named_type(context, TargetName {representation_names.storage_parameter}),
                          true
                      )}},
                .initializers = {{.name = storage_name, .value = storage_value}},
                .constexpr_specifier = true,
                .explicit_specifier = true,
            }
        );
    }

    auto helper_members = std::vector<TargetClassMember> {};
    for (auto case_index = 0uz; case_index < members.size(); ++case_index) {
        const auto& sum_case = members[case_index];
        const auto& case_names = representation_names.cases[case_index];
        const auto record_name = case_names.record_type;
        const auto condition = context.target().append_expression({
            .value = TargetCallExpr {
                .callee = name_expression(context, TargetSymbol::StdHoldsAlternative),
                .template_argument_type_ids = {named_type(context, TargetName {record_name})},
                .arguments = {name_expression(context, TargetName {storage_name})},
            },
        });
        const auto returned =
            context.target().append_lowering_statement(TargetReturnStmt {.expression = condition});
        helper_members.push_back(
            TargetMemberFunctionDecl {
                .name = case_names.holds_function,
                .parameters = {},
                .result = bool_type,
                .body = {returned},
                .static_specifier = false,
                .constexpr_specifier = true,
                .friend_specifier = false,
                .declaration_only = false,
                .defaulted = false,
                .result_reference = false,
                .const_qualified = true,
            }
        );
        if (!sum_case.payload_types.empty()) {
            const auto get = context.target().append_expression({
                .value = TargetCallExpr {
                    .callee = name_expression(context, TargetSymbol::StdGet),
                    .template_argument_type_ids = {named_type(context, TargetName {record_name})},
                    .arguments = {name_expression(context, TargetName {storage_name})},
                },
            });
            const auto payload_return =
                context.target().append_lowering_statement(TargetReturnStmt {.expression = get});
            helper_members.push_back(
                TargetMemberFunctionDecl {
                    .name = case_names.payload_function,
                    .parameters = {},
                    .result = named_type(context, TargetName {record_name}, true),
                    .body = {payload_return},
                    .static_specifier = false,
                    .constexpr_specifier = true,
                    .friend_specifier = false,
                    .declaration_only = false,
                    .defaulted = false,
                    .result_reference = true,
                    .const_qualified = true,
                }
            );
        }
    }

    auto items = std::vector<TargetItemID> {};
    items.push_back(context.target().append_lowering_item(
        TargetDecl {TargetClassDecl {
            .name = name,
            .final_specifier = true,
            .sections = {
                {.access = TargetClassAccess::Public, .members = std::move(public_members)},
                {.access = TargetClassAccess::Private, .members = std::move(private_members)},
                {.access = TargetClassAccess::Public, .members = std::move(helper_members)},
            },
        }}
    ));
    for (auto case_index = 0uz; case_index < members.size(); ++case_index) {
        const auto& sum_case = members[case_index];
        if (sum_case.payload_types.empty()) {
            const auto initializer = [&]() noexcept -> TargetExprID {
                const auto record = named_type(
                    context,
                    TargetName::from_components(
                        {name, representation_names.cases[case_index].record_type}
                    )
                );
                return context.target().append_expression({
                    .value = TargetConstructionExpr {
                        .type = record,
                        .initializer = std::monostate {},
                    },
                });
            }();
            items.push_back(context.target().append_lowering_item(
                TargetDecl {TargetVariableDecl {
                    .type = enum_type,
                    .name = TargetName::from_components({name, sum_case.name}),
                    .initializer = initializer,
                    .inline_specifier = true,
                    .constexpr_specifier = true,
                }}
            ));
            continue;
        }
        auto parameters = std::vector<TargetParameter> {};
        auto arguments = std::vector<TargetExprID> {};
        for (auto payload_index = 0uz; payload_index < sum_case.payload_types.size();
             ++payload_index) {
            const auto argument_name = TargetNameAllocator::enum_payload_field(payload_index);
            parameters.push_back({
                .name = argument_name,
                .type = parameter_type(
                    context,
                    HIRAccessMode::Read,
                    context.source()
                        .enum_case(enumeration.cases[case_index])
                        .payload_types[payload_index],
                    sum_case.payload_types[payload_index]
                ),
            });
            arguments.push_back(name_expression(context, TargetName {argument_name}));
        }
        const auto record = named_type(
            context,
            TargetName::from_components({name, representation_names.cases[case_index].record_type})
        );
        const auto storage = context.target().append_expression({
            .value = TargetConstructionExpr {
                .type = record,
                .initializer = std::move(arguments),
            },
        });
        const auto result = context.target().append_expression({
            .value = TargetConstructionExpr {
                .type = enum_type,
                .initializer = std::vector<TargetExprID> {storage},
            },
        });
        const auto returned =
            context.target().append_lowering_statement(TargetReturnStmt {.expression = result});
        items.push_back(context.target().append_lowering_item(
            TargetDecl {TargetFunctionDecl {
                .name = TargetName::from_components({name, sum_case.name}),
                .parameters = std::move(parameters),
                .result = enum_type,
                .body = {returned},
                .declaration_only = false,
                .inline_specifier = true,
            }}
        ));
    }
    return TargetItemGroup {
        .items = std::move(items),
        .separation = TargetVerticalSeparation::BlankLine,
    };
}
