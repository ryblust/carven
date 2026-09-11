module carven:backend.lowering.decl.nominal.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.lowering.decl.lowerer;
import :backend.lowering.decl;
import :backend.realization.constant;
import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.raw;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

auto lower_structure(ModuleLowering& context, StructID id) noexcept -> TargetDecl {
    const auto& declaration = context.semantic().declarations().structure(id);
    auto members = std::vector<TargetRecordMember>();
    for (const auto& field : declaration.fields) {
        members.push_back(
            TargetStructField {
                .name = context.name_allocator().source(
                    context.semantic().provenance().spelling(field.name),
                    context.semantic().provenance().spelling(declaration.name)
                ),
                .type = context.lower_type(field.type),
            }
        );
    }
    if (declaration.capabilities.equality) {
        const auto type = context.named_type(context.structure_name(id));
        members.push_back(
            TargetMemberFunctionDecl {
                .name = TargetOperatorName::Equality,
                .parameters = target_parameters(
                    {.name = std::nullopt, .type = context.reference_type(type, true)},
                    {.name = std::nullopt, .type = context.reference_type(type, true)}
                ),
                .result = context.intrinsic_type(TargetSymbol::Bool),
                .form = TargetMemberFunctionDefaulted {},
                .maybe_unused = true,
                .static_specifier = false,
                .constexpr_specifier = false,
                .friend_specifier = true,
                .result_reference = false,
                .const_qualified = false,
            }
        );
    }
    return TargetStructDecl {
        .name = context.names().structure_identifier(id),
        .members = std::move(members),
    };
}

namespace {

auto lower_numeric_enumeration(
    ModuleLowering& context,
    EnumID id,
    const NumericEnumRepresentation& representation
) noexcept -> std::vector<TargetItem> {
    const auto& declaration = context.semantic().declarations().enumeration(id);
    auto cases = std::vector<TargetEnumCase>();
    for (const auto case_id : declaration.cases) {
        cases.push_back({
            .name = context.names().enum_case_identifier(case_id),
            .value = numeric_enum_case_value_expression(context, case_id),
        });
    }
    auto result = std::vector<TargetItem>();
    result.push_back(source_item(
        context.semantic(),
        declaration.origin,
        TargetDecl {TargetEnumDecl {
            .name = context.names().enumeration_identifier(id),
            .underlying_type = context.lower_type(representation.underlying_type),
            .cases = std::move(cases),
        }}
    ));
    return result;
}

auto lower_payload_enumeration(ModuleLowering& context, EnumID id) noexcept
    -> std::vector<TargetItem> {
    struct LoweredCase final {
        EnumCaseID id;
        TargetIdentifier name;
        std::vector<TargetTypeID> payload_types;
    };

    const auto& declaration = context.semantic().declarations().enumeration(id);
    const auto& representation = context.payload_enum(id);
    const auto enum_name = context.names().enumeration_identifier(id);
    const auto enum_type = context.named_type(context.enumeration_name(id));
    auto cases = std::vector<LoweredCase>();
    for (const auto case_id : declaration.cases) {
        const auto& source = context.semantic().declarations().enum_case(case_id);
        auto payload = std::vector<TargetTypeID>();
        for (const auto type : source.payload_types) {
            payload.push_back(context.lower_type(type));
        }
        cases.push_back({
            .id = case_id,
            .name = context.names().enum_case_identifier(case_id),
            .payload_types = std::move(payload),
        });
    }

    auto public_members = std::vector<TargetClassMember>();
    for (auto case_index = 0uz; case_index < cases.size(); ++case_index) {
        const auto& sum_case = cases[case_index];
        auto parameters = std::vector<TargetParameter>();
        const auto& source = context.semantic().declarations().enum_case(sum_case.id);
        for (auto index = 0uz; index < source.payload_types.size(); ++index) {
            parameters.push_back({
                .name = TargetNameAllocator::enum_payload_field(index),
                .type = context.lower_type(source.payload_types[index]),
            });
        }
        public_members.push_back(
            TargetMemberFunctionDecl {
                .name = sum_case.name,
                .parameters = std::move(parameters),
                .result = enum_type,
                .form = TargetMemberFunctionDeclaration {},
                .maybe_unused = false,
                .static_specifier = true,
                .constexpr_specifier = false,
                .friend_specifier = false,
                .result_reference = false,
                .const_qualified = false,
            }
        );
    }
    if (declaration.capabilities.equality) {
        public_members.push_back(
            TargetMemberFunctionDecl {
                .name = TargetOperatorName::Equality,
                .parameters = target_parameters(
                    {.name = std::nullopt, .type = context.reference_type(enum_type, true)},
                    {.name = std::nullopt, .type = context.reference_type(enum_type, true)}
                ),
                .result = context.intrinsic_type(TargetSymbol::Bool),
                .form = TargetMemberFunctionDefaulted {},
                .maybe_unused = true,
                .static_specifier = false,
                .constexpr_specifier = false,
                .friend_specifier = true,
                .result_reference = false,
                .const_qualified = false,
            }
        );
    }

    auto private_members = std::vector<TargetClassMember>();
    auto alternatives = std::vector<TargetTypeID>();
    for (auto case_index = 0uz; case_index < cases.size(); ++case_index) {
        auto record_members = std::vector<TargetRecordMember>();
        for (auto payload_index = 0uz; payload_index < cases[case_index].payload_types.size();
             ++payload_index) {
            record_members.push_back(
                TargetStructField {
                    .name = TargetNameAllocator::enum_payload_field(payload_index),
                    .type = cases[case_index].payload_types[payload_index],
                }
            );
        }
        const auto record = representation.cases[case_index].record_type;
        const auto record_type = context.named_type(TargetName {record});
        if (declaration.capabilities.equality) {
            record_members.push_back(
                TargetMemberFunctionDecl {
                    .name = TargetOperatorName::Equality,
                    .parameters = target_parameters(
                        {.name = std::nullopt, .type = context.reference_type(record_type, true)},
                        {.name = std::nullopt, .type = context.reference_type(record_type, true)}
                    ),
                    .result = context.intrinsic_type(TargetSymbol::Bool),
                    .form = TargetMemberFunctionDefaulted {},
                    .maybe_unused = true,
                    .static_specifier = false,
                    .constexpr_specifier = false,
                    .friend_specifier = true,
                    .result_reference = false,
                    .const_qualified = false,
                }
            );
        }
        private_members.push_back(
            TargetNestedRecord {
                .name = record,
                .members = std::move(record_members),
            }
        );
        alternatives.push_back(record_type);
    }
    const auto variant = context.target().intern_type({
        .value =
            TargetIntrinsicType {
                .symbol = TargetSymbol::StdVariant,
                .type_argument_ids = std::move(alternatives),
            },
        .const_qualified = false,
    });
    private_members.push_back(
        TargetTypeAlias {
            .name = representation.storage_type,
            .type = variant,
        }
    );
    const auto storage_type = context.named_type(TargetName {representation.storage_type});
    private_members.push_back(
        TargetMemberVariable {
            .type = storage_type,
            .name = representation.storage_member,
            .static_specifier = false,
            .const_specifier = false,
        }
    );
    auto initializers = std::vector<TargetMemberInitializer>();
    initializers.push_back({
        .name = representation.storage_member,
        .value = transfer_expression(name_expression(representation.storage_member)),
    });
    private_members.push_back(
        TargetConstructorDecl {
            .name = enum_name,
            .parameters = target_parameters({
                .name = representation.storage_member,
                .type = context.reference_type(storage_type, false, true),
            }),
            .initializers = std::move(initializers),
            .constexpr_specifier = false,
            .explicit_specifier = true,
        }
    );

    auto projection_members = std::vector<TargetClassMember>();
    for (auto case_index = 0uz; case_index < cases.size(); ++case_index) {
        const auto record = representation.cases[case_index].record_type;
        auto arguments =
            target_expressions(address_expression(name_expression(representation.storage_member)));
        auto body = std::vector<TargetStmt>();
        body.push_back(generated_statement(
            TargetReturnStmt {
                .expression = TargetExpr {
                    .value = TargetCallExpr {
                        .callee = target_child(intrinsic_expression(TargetSymbol::StdGetIf)),
                        .template_argument_type_ids =
                            {
                                context.named_type(TargetName {record}),
                            },
                        .arguments = std::move(arguments),
                    },
                },
            }
        ));
        projection_members.push_back(
            TargetMemberFunctionDecl {
                .name = representation.cases[case_index].projection_function,
                .parameters = {},
                .result = context.pointer_type(context.named_type(TargetName {record}, true)),
                .form = TargetMemberFunctionDefinition {.body = std::move(body)},
                .maybe_unused = false,
                .static_specifier = false,
                .constexpr_specifier = false,
                .friend_specifier = false,
                .result_reference = false,
                .const_qualified = true,
            }
        );
    }

    auto sections = std::vector<TargetClassSection>();
    sections.push_back({
        .access = TargetClassAccess::Public,
        .members = std::move(public_members),
    });
    sections.push_back({
        .access = TargetClassAccess::Private,
        .members = std::move(private_members),
    });
    sections.push_back({
        .access = TargetClassAccess::Public,
        .members = std::move(projection_members),
    });
    auto result = std::vector<TargetItem>();
    result.push_back(source_item(
        context.semantic(),
        declaration.origin,
        TargetDecl {TargetClassDecl {
            .name = enum_name,
            .final_specifier = true,
            .sections = std::move(sections),
        }}
    ));

    for (auto case_index = 0uz; case_index < cases.size(); ++case_index) {
        const auto& sum_case = cases[case_index];
        const auto record_name = TargetName::from_components({
            enum_name,
            representation.cases[case_index].record_type,
        });
        const auto record_type = context.named_type(record_name);
        const auto member_name = TargetName::from_components({enum_name, sum_case.name});
        auto parameters = std::vector<TargetParameter>();
        auto arguments = std::vector<TargetExpr>();
        const auto& source = context.semantic().declarations().enum_case(sum_case.id);
        for (auto index = 0uz; index < source.payload_types.size(); ++index) {
            const auto name = TargetNameAllocator::enum_payload_field(index);
            parameters.push_back({
                .name = name,
                .type = context.lower_type(source.payload_types[index]),
            });
            arguments.push_back(transfer_expression(name_expression(name)));
        }
        auto record_arguments = std::move(arguments);
        auto result_arguments = std::vector<TargetExpr>();
        result_arguments.push_back(
            TargetExpr {
                .value = TargetConstructionExpr {
                    .type = record_type,
                    .initializer = std::move(record_arguments),
                },
            }
        );
        auto body = std::vector<TargetStmt>();
        body.push_back(generated_statement(
            TargetReturnStmt {
                .expression = TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = enum_type,
                        .initializer = std::move(result_arguments),
                    },
                },
            }
        ));
        result.push_back(expansion_item(
            context.semantic(),
            source.origin,
            TargetDecl {TargetFunctionDecl {
                .name = member_name,
                .parameters = std::move(parameters),
                .result = enum_type,
                .form = TargetFreeFunctionDefinition {.body = std::move(body)},
                .constexpr_specifier = false,
                .static_specifier = false,
                .inline_specifier = true,
            }}
        ));
    }
    return result;
}

} // namespace

auto lower_enumeration(ModuleLowering& context, EnumID id) noexcept -> std::vector<TargetItem> {
    const auto& declaration = context.semantic().declarations().enumeration(id);
    if (const auto* numeric = std::get_if<NumericEnumRepresentation>(&declaration.representation)) {
        return lower_numeric_enumeration(context, id, *numeric);
    }
    return lower_payload_enumeration(context, id);
}
