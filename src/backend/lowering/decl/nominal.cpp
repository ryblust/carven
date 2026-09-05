module carven:backend.lowering.decl.nominal.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body;
import :backend.lowering.context;
import :backend.lowering.decl;
import :backend.lowering.decl.lowerer;
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

namespace {

auto owned_parameter_type(ModuleLowering& context, TypeID type) noexcept -> TargetTypeID {
    return context.lower_type(type);
}

auto is_trivially_copyable_type(const ModuleLowering& context, TypeID type) noexcept -> bool;

auto is_trivially_copyable_type(const ModuleLowering& context, TypeID type) noexcept -> bool {
    const auto* builtin =
        std::get_if<BuiltinTypeValue>(&context.semantic().types().type(type).value);
    if (builtin != nullptr) {
        switch (builtin->kind) {
            case BuiltinType::Bool:
            case BuiltinType::Char:
            case BuiltinType::I8:
            case BuiltinType::I16:
            case BuiltinType::I32:
            case BuiltinType::I64:
            case BuiltinType::U8:
            case BuiltinType::U16:
            case BuiltinType::U32:
            case BuiltinType::U64:
            case BuiltinType::Isize:
            case BuiltinType::Usize:
            case BuiltinType::F32:
            case BuiltinType::F64:   return true;
            default:                 return false;
        }
    }
    const auto& canonical = context.semantic().types().type(type).value;
    if (const auto* structure = std::get_if<StructTypeValue>(&canonical)) {
        const auto& declaration = context.semantic().declarations().structure(structure->structure);
        return std::ranges::all_of(declaration.fields, [&context](const auto& field) {
            return is_trivially_copyable_type(context, field.type);
        });
    }
    if (const auto* enumeration = std::get_if<EnumTypeValue>(&canonical)) {
        const auto& declaration =
            context.semantic().declarations().enumeration(enumeration->enumeration);
        return std::ranges::all_of(declaration.cases, [&context](const auto case_id) {
            const auto& sum_case = context.semantic().declarations().enum_case(case_id);
            return std::ranges::all_of(sum_case.payload_types, [&context](const auto payload_type) {
                return is_trivially_copyable_type(context, payload_type);
            });
        });
    }
    if (const auto* array = std::get_if<ArrayTypeValue>(&canonical)) {
        return is_trivially_copyable_type(context, array->element);
    }
    return false;
}

} // namespace

namespace decl_lowering {

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
                .static_specifier = false,
                .constexpr_specifier = true,
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
            .value = lower_numeric_enum_case_value_expression(context, case_id),
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
    auto scalar_storage = true;
    auto cases = std::vector<LoweredCase>();
    for (const auto case_id : declaration.cases) {
        const auto& source = context.semantic().declarations().enum_case(case_id);
        auto payload = std::vector<TargetTypeID>();
        for (const auto type : source.payload_types) {
            scalar_storage = scalar_storage && is_trivially_copyable_type(context, type);
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
                .type = owned_parameter_type(context, source.payload_types[index]),
            });
        }
        public_members.push_back(
            TargetMemberFunctionDecl {
                .name = sum_case.name,
                .parameters = std::move(parameters),
                .result = enum_type,
                .form = TargetMemberFunctionDeclaration {},
                .static_specifier = true,
                .constexpr_specifier = true,
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
                .static_specifier = false,
                .constexpr_specifier = true,
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
                    .static_specifier = false,
                    .constexpr_specifier = true,
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
        .value = scalar_storage ? name_expression(representation.storage_member)
                                : move_expression(name_expression(representation.storage_member)),
    });
    private_members.push_back(
        TargetConstructorDecl {
            .name = enum_name,
            .parameters = target_parameters({
                .name = representation.storage_member,
                .type = context.reference_type(storage_type, false, true),
            }),
            .initializers = std::move(initializers),
            .constexpr_specifier = true,
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
                .static_specifier = false,
                .constexpr_specifier = true,
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
                .type = owned_parameter_type(context, source.payload_types[index]),
            });
            if (is_trivially_copyable_type(context, source.payload_types[index])) {
                arguments.push_back(name_expression(name));
            } else {
                arguments.push_back(move_expression(name_expression(name)));
            }
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
                .constexpr_specifier = true,
                .static_specifier = false,
                .inline_specifier = true,
            }}
        ));
    }
    return result;
}

auto lower_enumeration(ModuleLowering& context, EnumID id) noexcept -> std::vector<TargetItem> {
    const auto& declaration = context.semantic().declarations().enumeration(id);
    if (const auto* numeric = std::get_if<NumericEnumRepresentation>(&declaration.representation)) {
        return lower_numeric_enumeration(context, id, *numeric);
    }
    return lower_payload_enumeration(context, id);
}


} // namespace decl_lowering
