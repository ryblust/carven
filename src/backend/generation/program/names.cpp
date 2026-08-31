module carven:backend.generation.program.names.impl;

import :backend.generation.names;
import :backend.generation.program.construction;
import :semantic.hir.decl;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :semantic.visibility;
import :support.invariant;
import :support.visit;
import std;

auto TargetProgramBuilder::allocate_names() noexcept -> void {
    const auto allocator = TargetNameAllocator {};
    auto entity_identifiers =
        std::vector<std::optional<TargetIdentifier>>(semantic.symbols().size());
    auto entity_scope_names =
        std::flat_map<std::pair<std::uint32_t, std::uint32_t>, std::flat_set<std::string>> {};
    const auto allocate_entity = [&](this const auto& self, std::size_t index) noexcept -> void {
        if (entity_identifiers[index].has_value()) {
            return;
        }
        const auto symbol_id = SymbolID::from_index(static_cast<std::uint32_t>(index));
        const auto& symbol = semantic.symbol(symbol_id);
        if (!symbol.module_id.has_value()) {
            return;
        }
        if (symbol.parent.has_value()) {
            self(symbol.parent->index());
        }
        const auto enclosing = [&]() noexcept -> std::string_view {
            if (!symbol.parent.has_value()) {
                return {};
            }
            const auto parent_index = symbol.parent->index();
            if (parent_index < entity_identifiers.size()
                && entity_identifiers[parent_index].has_value()) {
                return entity_identifiers[parent_index]->spelling();
            }
            invariant_violation("target entity parent has no allocated name");
        }();
        const auto preferred = std::string(
            allocator.source(semantic.provenance().spelling(symbol.name), enclosing).spelling()
        );
        const auto scope_key = std::pair {
            symbol.module_id->index(),
            symbol.parent.has_value() ? symbol.parent->index()
                                      : std::numeric_limits<std::uint32_t>::max(),
        };
        auto& occupied = entity_scope_names[scope_key];
        entity_identifiers[index] = TargetNameAllocator::claim_source(preferred, occupied);
    };
    const auto allocate_declarations = [&](bool published) noexcept {
        for (auto module_index = 0uz; module_index < semantic.modules().size(); ++module_index) {
            const auto module_id =
                ProgramModuleID::from_index(static_cast<std::uint32_t>(module_index));
            for (const auto& item : semantic.hir_module(module_id).items) {
                std::visit(
                    Overloaded {
                        [&](FunctionID id) noexcept {
                            const auto& declaration = semantic.function(id);
                            if ((declaration.visibility != DeclarationVisibility::Module)
                                == published) {
                                allocate_entity(declaration.symbol.index());
                            }
                        },
                        [&](StructID id) noexcept {
                            const auto& declaration = semantic.structure(id);
                            if ((declaration.visibility != DeclarationVisibility::Module)
                                == published) {
                                allocate_entity(declaration.symbol.index());
                            }
                        },
                        [&](EnumID id) noexcept {
                            const auto& declaration = semantic.enumeration(id);
                            if ((declaration.visibility != DeclarationVisibility::Module)
                                != published) {
                                return;
                            }
                            allocate_entity(declaration.symbol.index());
                            for (const auto case_id : declaration.cases) {
                                allocate_entity(semantic.enum_case(case_id).symbol.index());
                            }
                        },
                        [](const auto&) static noexcept {},
                    },
                    item
                );
            }
        }
    };
    allocate_declarations(true);
    allocate_declarations(false);
    for (auto index = 0uz; index < semantic.symbols().size(); ++index) {
        allocate_entity(index);
    }

    auto module_scope_names = std::vector<std::flat_set<std::string>>(semantic.modules().size());
    for (auto index = 0uz; index < semantic.symbols().size(); ++index) {
        const auto& symbol =
            semantic.symbol(SymbolID::from_index(static_cast<std::uint32_t>(index)));
        if (!symbol.module_id.has_value() || symbol.parent.has_value()) {
            continue;
        }
        module_scope_names[symbol.module_id->index()].insert(
            std::string(entity_identifiers[index]->spelling())
        );
    }
    const auto generated_namespace = TargetNameAllocator::generated_namespace();
    const auto domain_namespace = TargetNameAllocator::domain_namespace(linkage_domain);
    auto linkage_components = std::vector<TargetIdentifier>(
        generated_namespace.components().begin(),
        generated_namespace.components().end()
    );
    linkage_components.insert(
        linkage_components.end(),
        domain_namespace.components().begin(),
        domain_namespace.components().end()
    );
    auto qualified_namespace_names =
        std::vector<std::optional<TargetName>>(semantic.modules().size());
    auto module_namespace_names = std::vector<std::optional<TargetName>>(semantic.modules().size());
    for (auto module_index = 0uz; module_index < semantic.modules().size(); ++module_index) {
        const auto module_id =
            ProgramModuleID::from_index(static_cast<std::uint32_t>(module_index));
        const auto module_namespace = TargetNameAllocator::fixed(
            derive_module_namespace_id(semantic.provenance().module_record(module_id).path.value())
                .namespace_identifier()
        );
        auto full = linkage_components;
        full.push_back(module_namespace);
        qualified_namespace_names[module_index] = TargetName::from_components(std::move(full));
        module_namespace_names[module_index] = TargetName::from_components({module_namespace});
    }

    auto entity_names = std::vector<std::optional<TargetEntityName>>(semantic.symbols().size());
    for (auto index = 0uz; index < semantic.symbols().size(); ++index) {
        const auto& symbol =
            semantic.symbol(SymbolID::from_index(static_cast<std::uint32_t>(index)));
        if (!symbol.module_id.has_value() || !entity_identifiers[index].has_value()) {
            continue;
        }
        auto components = std::vector<TargetIdentifier>();
        auto current = std::optional {SymbolID::from_index(static_cast<std::uint32_t>(index))};
        while (current.has_value()) {
            const auto current_index = current->index();
            if (current_index >= entity_identifiers.size()
                || !entity_identifiers[current_index].has_value()) {
                invariant_violation("target entity parent has no allocated name");
            }
            components.push_back(*entity_identifiers[current_index]);
            current = semantic.symbol(*current).parent;
        }
        std::ranges::reverse(components);
        entity_names[index] = TargetEntityName {
            .owner_module = *symbol.module_id,
            .relative_name = TargetName::from_components(std::move(components)),
        };
    }

    auto modules = std::vector<TargetModuleNames> {};
    modules.reserve(semantic.modules().size());
    for (auto index = 0uz; index < semantic.modules().size(); ++index) {
        if (!qualified_namespace_names[index].has_value()
            || !module_namespace_names[index].has_value()) {
            invariant_violation("target module has no resolved namespace");
        }
        modules.push_back({
            .qualified_namespace_name = std::move(*qualified_namespace_names[index]),
            .module_namespace_name = std::move(*module_namespace_names[index]),
            .reserved_identifiers = std::move(module_scope_names[index]),
        });
    }

    auto payload_enums =
        std::vector<std::optional<TargetPayloadEnumNames>>(semantic.enumerations().size());
    for (auto index = 0uz; index < semantic.enumerations().size(); ++index) {
        const auto enum_id = EnumID::from_index(static_cast<std::uint32_t>(index));
        const auto& enumeration = semantic.enumeration(enum_id);
        if (enumeration.profile != HIREnumProfile::Payload) {
            continue;
        }
        auto case_names = std::vector<TargetIdentifier> {};
        case_names.reserve(enumeration.cases.size());
        for (const auto case_id : enumeration.cases) {
            case_names.push_back(*entity_identifiers[semantic.enum_case(case_id).symbol.index()]);
        }
        payload_enums[index] =
            payload_enum_names(case_names, *entity_identifiers[enumeration.symbol.index()]);
    }

    auto scoped_source_names = std::vector<std::flat_set<std::string>>(semantic.scopes().size());
    for (auto index = 0uz; index < semantic.symbols().size(); ++index) {
        const auto symbol_id = SymbolID::from_index(static_cast<std::uint32_t>(index));
        const auto& symbol = semantic.symbol(symbol_id);
        const auto& binding = semantic.binding(symbol_id);
        if (symbol.module_id.has_value() || !binding.has_value()) {
            continue;
        }
        const auto enclosing = symbol.parent.has_value()
            ? semantic.provenance().spelling(semantic.symbol(*symbol.parent).name)
            : std::string_view {};
        const auto reserved =
            allocator.source(semantic.provenance().spelling(symbol.name), enclosing);
        const auto scope = binding->scope;
        scoped_source_names[scope.index()].insert(std::string(reserved.spelling()));
    }

    name_allocation = TargetNameAllocation {
        .modules = std::move(modules),
        .generated_namespace = generated_namespace,
        .domain_namespace = domain_namespace,
        .entities = std::move(entity_names),
        .payload_enums = std::move(payload_enums),
        .scoped_source_names = std::move(scoped_source_names),
    };
}
