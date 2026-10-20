module carven:backend.lowering.decl.interop.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.lowering.decl;
import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.provenance;
import std;

namespace {

auto string_view_expression(std::string value) noexcept -> TargetExpr {
    return {
        .value = TargetLiteralExpr {
            .value = TargetStringLiteral {
                .bytes = std::move(value),
                .kind = TargetStringLiteralKind::StringView
            }
        }
    };
}

auto host_descriptor(
    ModuleLowering& context,
    std::string_view name,
    TargetTypeID type,
    TargetExpr value
) noexcept -> TargetItem {
    auto body = std::vector<TargetStmt>();
    body.push_back(target_lowering_statement(TargetReturnStmt {.expression = std::move(value)}));
    return target_lowering_item(
        TargetDecl {TargetFunctionDecl {
            .name = TargetName(TargetIdentifier::from_spelling(name)),
            .parameters = target_parameters(
                TargetParameter {
                    .local = std::nullopt,
                    .type = context.pointer_type(type, false),
                    .default_value = std::nullopt
                }
            ),
            .result = context.intrinsic_type(TargetSymbol::Auto, false),
            .form = TargetFreeFunctionDefinition {.body = std::move(body)},
            .constexpr_specifier = true,
            .static_specifier = false,
            .inline_specifier = true
        }}
    );
}

} // namespace

auto lower_host_record(ModuleLowering& context, StructID id) noexcept -> std::vector<TargetItem> {
    const auto& semantic = context.semantic();
    const auto& record = semantic.declarations().structure(id);
    const auto& module_decl = semantic.declarations().module_decl(record.module_id);
    const auto& path = semantic.provenance().module_record(module_decl.provenance_module).path;
    const auto record_type =
        context.named_type(context.structure_name(id, TypeNameScope::Module), false);
    auto descriptors = std::vector<TargetExpr>();
    for (const auto [index, field] : std::views::enumerate(record.fields)) {
        auto field_name = context.structure_name(id, TypeNameScope::Module);
        field_name.append(context.field_identifier(id, index));
        descriptors.push_back(
            TargetExpr {
                .value = TargetConstructionExpr {
                    .type = context.intrinsic_type(TargetSymbol::StdPair, false),
                    .initializer = target_expressions(
                        string_view_expression(
                            std::string(semantic.provenance().spelling(field.name))
                        ),
                        prefix_expression(
                            TargetPrefixOperator::AddressOf,
                            name_expression(std::move(field_name))
                        )
                    )
                }
            }
        );
    }
    auto result = std::vector<TargetItem>();
    const auto append = [&](std::string_view name, TargetExpr value) noexcept {
        result.push_back(host_descriptor(context, name, record_type, std::move(value)));
    };
    append(
        "carven_type_name",
        string_view_expression(
            std::string(path.value()) + "."
            + std::string(semantic.provenance().spelling(record.name))
        )
    );
    append(
        "carven_fields",
        call_expression(intrinsic_expression(TargetSymbol::StdMakeTuple), std::move(descriptors))
    );
    return result;
}

auto lower_host_enumeration(ModuleLowering& context, EnumID id) noexcept
    -> std::vector<TargetItem> {
    const auto& semantic = context.semantic();
    const auto& declaration = semantic.declarations().enumeration(id);
    const auto& module_decl = semantic.declarations().module_decl(declaration.module_id);
    const auto& path = semantic.provenance().module_record(module_decl.provenance_module).path;
    const auto type =
        context.named_type(context.enumeration_name(id, TypeNameScope::Module), false);
    auto result = std::vector<TargetItem>();
    result.push_back(host_descriptor(
        context,
        "carven_type_name",
        type,
        string_view_expression(
            std::string(path.value()) + "."
            + std::string(semantic.provenance().spelling(declaration.name))
        )
    ));
    const auto* payload = std::get_if<PayloadEnumRepresentation>(&declaration.representation);
    auto cases = std::vector<TargetExpr>();
    for (const auto [index, case_id] : std::views::enumerate(declaration.cases)) {
        const auto& item = semantic.declarations().enum_case(case_id);
        auto name = context.enumeration_name(id, TypeNameScope::Module);
        name.append(context.names().enum_case_identifier(case_id));
        auto value = name_expression(std::move(name));
        if (payload) {
            value = prefix_expression(TargetPrefixOperator::AddressOf, std::move(value));
        }
        cases.push_back(
            TargetExpr {
                .value = TargetConstructionExpr {
                    .type = context.intrinsic_type(TargetSymbol::StdPair, false),
                    .initializer = target_expressions(
                        string_view_expression(
                            std::string(semantic.provenance().spelling(item.name))
                        ),
                        std::move(value)
                    )
                }
            }
        );
        if (payload) {
            const auto& representation = context.payload_enum(id);
            auto record_name = context.enumeration_name(id, TypeNameScope::Module);
            record_name.append(representation.case_records[index]);
            const auto record_type = context.named_type(record_name, false);
            auto fields = std::vector<TargetExpr>();
            for (auto field_index = 0uz; field_index < item.payload_types.size(); ++field_index) {
                auto member = record_name;
                member.append(enum_payload_field_identifier(field_index));
                fields.push_back(
                    TargetExpr {
                        .value = TargetConstructionExpr {
                            .type = context.intrinsic_type(TargetSymbol::StdPair, false),
                            .initializer = target_expressions(
                                string_view_expression({}),
                                prefix_expression(
                                    TargetPrefixOperator::AddressOf,
                                    name_expression(std::move(member))
                                )
                            )
                        }
                    }
                );
            }
            result.push_back(host_descriptor(
                context,
                "carven_fields",
                record_type,
                call_expression(intrinsic_expression(TargetSymbol::StdMakeTuple), std::move(fields))
            ));
        }
    }
    result.push_back(host_descriptor(
        context,
        "carven_cases",
        type,
        call_expression(intrinsic_expression(TargetSymbol::StdMakeTuple), std::move(cases))
    ));
    if (payload) {
        auto member = context.enumeration_name(id, TypeNameScope::Module);
        member.append(context.payload_enum(id).storage_member);
        result.push_back(host_descriptor(
            context,
            "carven_enum_storage",
            type,
            prefix_expression(TargetPrefixOperator::AddressOf, name_expression(std::move(member)))
        ));
    }
    return result;
}

auto lower_native_provider(ModuleLowering& context, FunctionID id) noexcept -> TargetItem {
    const auto& semantic = context.semantic();
    const auto& function = semantic.declarations().function(id);
    const auto& callable = semantic.declarations().callable(function.callable);
    const auto& signature = semantic.callable_signatures().signature(callable.signature);
    auto parameters = std::vector<TargetParameter>();
    for (const auto [index, parameter] : std::views::enumerate(signature.parameters)) {
        parameters.push_back(
            {.local = std::nullopt,
             .type = context.lower_parameter(
                 parameter.access,
                 parameter.type,
                 TypeNameScope::Global,
                 semantic.read_requires_stable_storage(function.callable, index)
             ),
             .default_value = std::nullopt}
        );
    }
    return namespace_item(
        cpp_contract_namespace(semantic, id),
        target_items(target_lowering_item(
            TargetDecl {TargetFunctionDecl {
                .name = TargetName(TargetIdentifier::from_spelling("invoke")),
                .parameters = std::move(parameters),
                .result = context.lower_signature_result(
                    callable.signature,
                    semantic.may_stop(function.callable),
                    TypeNameScope::Global
                ),
                .form = TargetFreeFunctionDeclaration {},
                .constexpr_specifier = false,
                .static_specifier = false,
                .inline_specifier = false
            }}
        )),
        TargetCompilerReason::ArtifactScaffolding,
        true
    );
}

auto lower_host_contract(ModuleLowering& lowering, FunctionID id) noexcept -> TargetItem {
    const auto& semantic = lowering.semantic();
    const auto& function = semantic.declarations().function(id);
    const auto& callable = semantic.declarations().callable(function.callable);
    const auto& signature = semantic.callable_signatures().signature(callable.signature);
    auto aliases = std::vector<TargetItem>();
    const auto alias = [&](std::string_view name, TargetTypeID type) noexcept {
        aliases.push_back(target_lowering_item(
            TargetDecl {TargetTypeAlias {
                .name = TargetIdentifier::from_spelling(name),
                .type = type,
            }}
        ));
    };
    const auto result = lowering.lower_type(signature.result, TypeNameScope::Global);
    alias("Result", result);
    const auto completion = lowering.lower_signature_result(
        callable.signature,
        semantic.may_stop(function.callable),
        TypeNameScope::Global
    );
    alias("Completion", completion);
    auto parameters = std::vector<TargetTypeID>();
    auto failures = std::vector<TargetTypeID>();
    auto accesses = std::vector<TargetExpr>();
    const auto failure_types = lowering.plan().failure_abi().members(signature.failures);
    for (const auto [index, parameter] : std::views::enumerate(signature.parameters)) {
        alias(
            std::format("Parameter{}", index),
            lowering.lower_type(parameter.type, TypeNameScope::Global)
        );
        const auto argument = lowering.lower_parameter(
            parameter.access,
            parameter.type,
            TypeNameScope::Global,
            semantic.read_requires_stable_storage(function.callable, index)
        );
        alias(std::format("Argument{}", index), argument);
        parameters.push_back(argument);
        const auto symbol = [&]() noexcept {
            switch (parameter.access) {
                case AccessMode::Read:  return TargetSymbol::RuntimeParameterRead;
                case AccessMode::Write: return TargetSymbol::RuntimeParameterWrite;
                case AccessMode::Take:  return TargetSymbol::RuntimeParameterTake;
            }
            std::unreachable();
        }();
        accesses.push_back(intrinsic_expression(symbol));
    }
    for (const auto [index, failure] : std::views::enumerate(failure_types)) {
        const auto type = lowering.lower_type(failure, TypeNameScope::Global);
        alias(std::format("Failure{}", index), type);
        failures.push_back(type);
    }
    const auto& path =
        semantic.provenance()
            .module_record(
                semantic.declarations().module_decl(function.module_id).provenance_module
            )
            .path;
    const auto source_name = std::string(semantic.provenance().spelling(function.name));
    const auto append_value = [&](std::string_view name, TargetExpr value) noexcept {
        aliases.push_back(target_lowering_item(
            TargetDecl {TargetVariableDecl {
                .name = TargetIdentifier::from_spelling(name),
                .type = lowering.intrinsic_type(TargetSymbol::Auto, false),
                .initializer = std::move(value),
                .inline_specifier = true,
                .constexpr_specifier = true
            }}
        ));
    };
    append_value("source_module", string_view_expression(std::string(path.value())));
    append_value("source_name", string_view_expression(source_name));
    const auto tuple_type = [&](std::vector<TargetTypeID> types) noexcept {
        if (types.empty()) {
            return lowering.target().intern_type(
                TargetType {
                    .value = TargetDecltypeType(
                        TargetExpr {
                            .value =
                                TargetConstructionExpr {
                                    .type = lowering.intrinsic_type(TargetSymbol::StdTuple, false),
                                    .initializer = std::vector<TargetExpr>()
                                }
                        }
                    ),
                    .const_qualified = false
                }
            );
        }
        return lowering.target().intern_type(
            TargetType {
                .value =
                    TargetIntrinsicType {
                        .symbol = TargetSymbol::StdTuple,
                        .type_argument_ids = std::move(types)
                    },
                .const_qualified = false
            }
        );
    };
    auto native_types = std::map<std::string, TypeID>();
    auto visited = std::set<TypeID>();
    const auto collect_native_types = [&](this const auto& self, TypeID type) noexcept -> void {
        if (!visited.insert(type).second) {
            return;
        }
        if (auto name = semantic.native_type_name(type)) {
            native_types.emplace(std::move(*name), type);
            return;
        }
        const auto& shape = semantic.types().type(type).value;
        if (const auto* record = std::get_if<StructTypeValue>(&shape)) {
            for (const auto& field : semantic.declarations().structure(record->structure).fields) {
                self(field.type);
            }
        } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&shape)) {
            for (const auto id :
                 semantic.declarations().enumeration(enumeration->enumeration).cases) {
                for (const auto payload : semantic.declarations().enum_case(id).payload_types) {
                    self(payload);
                }
            }
        } else if (const auto* vector = std::get_if<VectorTypeValue>(&shape)) {
            self(vector->element);
        } else if (const auto* array = std::get_if<ArrayTypeValue>(&shape)) {
            self(array->element);
        } else if (const auto* tuple = std::get_if<TupleTypeValue>(&shape)) {
            for (const auto element : tuple->elements) {
                self(element);
            }
        }
    };
    collect_native_types(signature.result);
    for (const auto& parameter : signature.parameters) {
        collect_native_types(parameter.type);
    }
    for (const auto failure : failure_types) {
        collect_native_types(failure);
    }
    auto native_descriptors = std::vector<TargetExpr>();
    for (const auto& [name, type] : native_types) {
        const auto identity = lowering.target().intern_type(
            TargetType {
                .value =
                    TargetIntrinsicType {
                        .symbol = TargetSymbol::StdTypeIdentityTag,
                        .type_argument_ids = {lowering.lower_type(type, TypeNameScope::Global)}
                    },
                .const_qualified = false
            }
        );
        native_descriptors.push_back(
            TargetExpr {
                .value = TargetConstructionExpr {
                    .type = lowering.intrinsic_type(TargetSymbol::StdPair, false),
                    .initializer = target_expressions(
                        string_view_expression(name),
                        TargetExpr {
                            .value = TargetConstructionExpr {.type = identity, .initializer = {}}
                        }
                    )
                }
            }
        );
    }
    append_value(
        "service",
        template_call_expression(
            intrinsic_expression(TargetSymbol::RuntimeMakeService),
            {TargetStringLiteral {
                 .bytes = std::string(path.value()) + "." + source_name,
                 .kind = TargetStringLiteralKind::String
             },
             result,
             tuple_type(std::move(parameters)),
             tuple_type(std::move(failures)),
             signature.execution == CallableExecutionKind::Async},
            target_expressions(
                TargetExpr {
                    .value =
                        TargetArrayExpr {
                            .element_type_id =
                                lowering
                                    .intrinsic_type(TargetSymbol::RuntimeParameterAccess, false),
                            .extent = target_child(integer_expression(signature.parameters.size())),
                            .elements = std::move(accesses)
                        }
                },
                call_expression(
                    intrinsic_expression(TargetSymbol::StdMakeTuple),
                    std::move(native_descriptors)
                ),
                string_view_expression(
                    semantic.callable_contract_identity(callable.signature).value_or("")
                )
            )
        )
    );
    return namespace_item(
        cpp_contract_namespace(semantic, id),
        std::move(aliases),
        TargetCompilerReason::ArtifactScaffolding,
        true
    );
}
