module carven:backend.generation.plan.names.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :semantic.semir.constant;
import :semantic.semir.content;
import :semantic.semir.generic;
import :semantic.semir.stage;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :semantic.visibility;
import :source.provenance;
import :support.function_ref;
import :support.invariant;
import :support.visit;
import std;

namespace {

template<typename ID, typename Value>
auto total_size(const Value& entries) noexcept -> std::size_t {
    auto size = 0uz;
    for (const auto entry : entries) {
        static_cast<void>(entry);
        ++size;
    }
    return size;
}

template<typename Value>
auto take_total(std::vector<std::optional<Value>> values, std::string_view fact) noexcept
    -> std::vector<Value> {
    auto result = std::vector<Value>();
    result.reserve(values.size());
    for (auto& value : values) {
        if (!value.has_value()) {
            invariant_violation(fact);
        }
        result.push_back(std::move(*value));
    }
    return result;
}

// A readable token for scalar static values; composite values use a digest.
auto static_value_token(const SemIRProgram& semantic, ConstantID id) noexcept
    -> std::optional<std::string> {
    return semantic.constants().constant(id).value.visit(
        Overloaded {
            [](const IntegerConstant& value) static noexcept -> std::optional<std::string> {
                return std::format("{}{}", value.negative() ? "n" : "", value.magnitude());
            },
            [](const BooleanConstant& value) static noexcept -> std::optional<std::string> {
                return value.value ? "true" : "false";
            },
            [](const CharacterConstant& value) static noexcept -> std::optional<std::string> {
                return std::format("u{:x}", static_cast<std::uint32_t>(value.scalar));
            },
            [](const auto&) static noexcept -> std::optional<std::string> { return std::nullopt; },
        }
    );
}

auto static_instance_spelling(
    const SemIRProgram& semantic,
    std::string_view function,
    std::span<const ConstantID> arguments,
    std::string_view content
) noexcept -> std::string {
    auto tokens = std::string();
    for (const auto argument : arguments) {
        auto token = static_value_token(semantic, argument);
        if (!token) {
            tokens.clear();
            break;
        }
        tokens += tokens.empty() ? "" : "_";
        tokens += *token;
    }
    if (tokens.empty() || tokens.size() > 48) {
        tokens = std::format("s{}", content_name_digest(content));
    }
    return std::format("{}{}{}", function, function.ends_with('_') ? "" : "_", tokens);
}

} // namespace

auto plan_closures(const SemIRProgram& semantic) noexcept -> TargetClosureCatalog {
    const auto& declarations = semantic.declarations();
    const auto callable_count = total_size<CallableID>(declarations.callables());
    const auto module_count = total_size<ModuleID>(declarations.modules());
    auto owners = std::vector<std::optional<ModuleID>>(callable_count);
    auto production = std::vector<std::uint8_t>(callable_count);
    auto tests = std::vector<std::uint8_t>(callable_count);
    auto scanned_production = std::vector<std::uint8_t>(callable_count);
    auto scanned_tests = std::vector<std::uint8_t>(callable_count);
    auto discovery = std::vector<CallableID>();
    auto source_modules = std::flat_map<ProgramSourceID, ModuleID>();
    for (const auto module : declarations.modules()) {
        source_modules.emplace(
            semantic.provenance().module_record(module.value.provenance_module).source_id,
            module.id
        );
    }

    const auto closure_origin = [&](CallableID callable) noexcept {
        const auto* implementation =
            std::get_if<ClosureBodyImplementation>(&declarations.callable(callable).implementation);
        if (implementation == nullptr) {
            invariant_violation("closure reference names a non-closure callable");
        }
        const auto origin = semantic.bodies().body(implementation->body).region().origin;
        return semantic.provenance().source_origin(origin);
    };
    const auto record = [&](CallableID callable, bool test) noexcept {
        if (callable.owner() != semantic.identity() || callable.index() >= owners.size()) {
            invariant_violation("closure discovery received a foreign callable");
        }
        auto& owner = owners[callable.index()];
        if (!owner.has_value()) {
            owner = source_modules.at(closure_origin(callable).source_id);
            discovery.push_back(callable);
        }
        (test ? tests : production)[callable.index()] = 1;
    };

    auto visit_callable = FunctionRef<void(CallableID, bool) noexcept>();
    const auto visit_closure = [&](CallableID closure, bool test) noexcept {
        record(closure, test);
        auto& scanned = (test ? scanned_tests : scanned_production)[closure.index()];
        if (scanned == 0) {
            scanned = 1;
            visit_callable(closure, test);
        }
    };
    const auto visit_callable_body = [&](CallableID callable, bool test) noexcept {
        for (const auto closure : semantic.callable_surface(callable).closures) {
            visit_closure(closure, test);
        }
    };
    visit_callable = visit_callable_body;
    const auto visit_body = [&](BodyID body_id, bool test) noexcept {
        for (const auto closure :
             body_closure_references(semantic, semantic.bodies().body(body_id))) {
            visit_closure(closure, test);
        }
    };

    for (const auto module_record : declarations.modules()) {
        for (const auto item : module_record.value.items) {
            item.visit(
                Overloaded {
                    [&](FunctionID id) noexcept {
                        visit_callable(declarations.function(id).callable, false);
                    },
                    [&](TestID id) noexcept {
                        const auto& test = semantic.tests().test(id);
                        if (!test.is_const) {
                            visit_body(*test.body, true);
                        }
                    },
                    [](StructID) static noexcept {},
                    [](EnumID) static noexcept {},
                    [](ModuleConstantID) static noexcept {},
                }
            );
        }
    }

    auto dependencies = std::vector<std::vector<CallableID>>(callable_count);
    const auto collect_value_closure_dependencies =
        [&](this const auto& self,
            TypeID type_id,
            std::vector<CallableID>& destination,
            std::flat_set<TypeID>& active_types) noexcept -> void {
        if (!active_types.insert(type_id).second) {
            invariant_violation("closure capture target type contains a structural cycle");
        }
        semantic.types().type(type_id).value.visit(
            Overloaded {
                [](const BuiltinTypeValue&) static noexcept {},
                [](const PointerTypeValue&) static noexcept {},
                [](const StructTypeValue&) static noexcept {},
                [](const EnumTypeValue&) static noexcept {},
                [&](const SliceTypeValue& value) noexcept {
                    self(value.element, destination, active_types);
                },
                [](const OwnedSequenceTypeValue&) static noexcept {},
                [&](const RangeTypeValue& value) noexcept {
                    self(value.element, destination, active_types);
                },
                [&](const ArrayTypeValue& value) noexcept {
                    self(value.element, destination, active_types);
                },
                [](const FunctionTypeValue&) static noexcept {},
                [&](const ClosureTypeValue& value) noexcept {
                    destination.push_back(value.callable);
                },
                [](const CallableViewTypeValue&) static noexcept {},
                [&](const CppTypeValue& value) noexcept {
                    for (const auto argument : cpp_type_references(value)) {
                        self(argument, destination, active_types);
                    }
                },
            }
        );
        active_types.erase(type_id);
    };
    for (const auto callable : discovery) {
        const auto& declaration = declarations.callable(callable);
        const auto body_id = std::get<ClosureBodyImplementation>(declaration.implementation).body;
        const auto& body = semantic.bodies().body(body_id);
        auto result_types = std::flat_set<TypeID>();
        collect_value_closure_dependencies(
            semantic.callable_signatures().signature(declaration.signature).result,
            dependencies[callable.index()],
            result_types
        );
        for (const auto capture : body.inputs().captures) {
            const auto& binding = body.binding(capture);
            const auto* storage = std::get_if<CaptureBindingStorage>(&binding.storage);
            if (storage == nullptr) {
                invariant_violation("closure BodyInputs names a non-capture binding");
            }
            switch (storage->mode) {
                case CaptureMode::Value: break;
                case CaptureMode::Write: continue;
            }
            auto active_types = std::flat_set<TypeID>();
            collect_value_closure_dependencies(
                binding.type,
                dependencies[callable.index()],
                active_types
            );
        }
        visit_semantic_nodes(body.region(), [&](const SemanticExpression& expression) noexcept {
            auto active_types = std::flat_set<TypeID>();
            collect_value_closure_dependencies(
                expression.type.resolved(),
                dependencies[callable.index()],
                active_types
            );
            if (const auto* child = std::get_if<SemClosure>(&expression.value)) {
                dependencies[callable.index()].push_back(child->callable);
            }
        });
        std::erase(dependencies[callable.index()], callable);
        std::ranges::sort(dependencies[callable.index()]);
        const auto unique = std::ranges::unique(dependencies[callable.index()]);
        dependencies[callable.index()].erase(unique.begin(), unique.end());
    }

    auto production_order = std::vector<std::vector<CallableID>>(module_count);
    auto test_order = std::vector<std::vector<CallableID>>(module_count);
    auto state = std::vector<std::uint8_t>(callable_count);
    auto definition_order = std::vector<CallableID>();
    auto order = std::function<void(CallableID, bool)>();
    order = [&](CallableID callable, bool test) noexcept {
        if (state[callable.index()] == 2) {
            return;
        }
        if (state[callable.index()] == 1) {
            invariant_violation("closure target-type dependency is cyclic");
        }
        state[callable.index()] = 1;
        const auto module_id = *owners[callable.index()];
        for (const auto dependency : dependencies[callable.index()]) {
            if (!owners[dependency.index()].has_value()) {
                invariant_violation("closure target type depends on an unowned closure");
            }
            if (production[dependency.index()] != 0 || (test && tests[dependency.index()] != 0)) {
                order(dependency, test && production[dependency.index()] == 0);
            }
        }
        state[callable.index()] = 2;
        auto& destination =
            test ? test_order[module_id.index()] : production_order[module_id.index()];
        destination.push_back(callable);
        definition_order.push_back(callable);
    };
    for (const auto callable : discovery) {
        if (production[callable.index()] != 0) {
            order(callable, false);
        }
    }
    for (const auto callable : discovery) {
        if (production[callable.index()] == 0 && tests[callable.index()] != 0) {
            order(callable, true);
        }
    }

    auto module_ordinals = std::vector<std::uint32_t>(callable_count);
    auto module_counts = std::vector<std::uint32_t>(module_count);
    auto lexical_order = discovery;
    std::ranges::sort(lexical_order, [&](CallableID left, CallableID right) noexcept {
        return std::tuple {*owners[left.index()], closure_origin(left).span, left}
        < std::tuple {*owners[right.index()], closure_origin(right).span, right};
    });
    for (const auto callable : lexical_order) {
        module_ordinals[callable.index()] = module_counts[owners[callable.index()]->index()]++;
    }

    return {
        .semantic_identity = semantic.identity(),
        .owner_modules = std::move(owners),
        .module_ordinals = std::move(module_ordinals),
        .production_definitions = std::move(production_order),
        .test_definitions = std::move(test_order),
        .definition_order = std::move(definition_order),
    };
}

auto plan_names(
    const SemIRProgram& semantic,
    const LinkageDomainID& linkage,
    const TargetClosureCatalog& closures
) noexcept -> TargetNamePlan {
    const auto& declarations = semantic.declarations();
    const auto provenance = semantic.provenance();

    const auto module_count = total_size<ModuleID>(declarations.modules());
    const auto structure_count = total_size<StructID>(declarations.structures());
    const auto enumeration_count = total_size<EnumID>(declarations.enumerations());
    const auto enum_case_count = total_size<EnumCaseID>(declarations.enum_cases());
    const auto callable_count = total_size<CallableID>(declarations.callables());

    auto structure_names = std::vector<std::optional<TargetEntityName>>(structure_count);
    auto enumeration_names = std::vector<std::optional<TargetEntityName>>(enumeration_count);
    auto callable_names = std::vector<std::optional<TargetEntityName>>(callable_count);
    auto closure_type_names = std::vector<std::optional<TargetEntityName>>(callable_count);
    auto enum_case_names = std::vector<std::optional<TargetIdentifier>>(enum_case_count);
    auto module_occupied = std::vector<std::flat_set<std::string>>(module_count);

    const auto claim_entity =
        [&](ModuleID module_id, ProgramSpellingID spelling, auto id, auto& rows) noexcept {
            if (module_id.owner() != semantic.identity()
                || id.owner() != semantic.identity()
                || module_id.index() >= module_occupied.size()
                || id.index() >= rows.size()) {
                invariant_violation("target name planning received a foreign semantic declaration");
            }
            const auto preferred = source_target_identifier(provenance.spelling(spelling));
            const auto claimed =
                claim_target_identifier(preferred.spelling(), module_occupied[module_id.index()]);
            rows[id.index()] = TargetEntityName {
                .owner_module = module_id,
                .relative_name = TargetName {claimed},
            };
        };

    const auto allocate_item = [&](ModuleID module_id, ModuleItem item, bool published) noexcept {
        item.visit(
            Overloaded {
                [&](FunctionID id) noexcept {
                    const auto& value = declarations.function(id);
                    if ((value.visibility != DeclarationVisibility::Module) == published
                        && !callable_names[value.callable.index()].has_value()) {
                        claim_entity(module_id, value.name, value.callable, callable_names);
                    }
                },
                [&](StructID id) noexcept {
                    const auto& value = declarations.structure(id);
                    if ((value.visibility != DeclarationVisibility::Module) == published
                        && semantic.generic_nominal_instance(id) == nullptr
                        && !structure_names[id.index()].has_value()) {
                        claim_entity(module_id, value.name, id, structure_names);
                    }
                },
                [&](EnumID id) noexcept {
                    const auto& value = declarations.enumeration(id);
                    if ((value.visibility != DeclarationVisibility::Module) == published
                        && semantic.generic_nominal_instance(id) == nullptr
                        && !enumeration_names[id.index()].has_value()) {
                        claim_entity(module_id, value.name, id, enumeration_names);
                    }
                },
                [](ModuleConstantID) static noexcept {},
                [](TestID) static noexcept {},
            }
        );
    };

    for (const auto module_record : declarations.modules()) {
        for (const auto item : module_record.value.items) {
            allocate_item(module_record.id, item, true);
        }
        for (const auto item : module_record.value.items) {
            allocate_item(module_record.id, item, false);
        }
    }

    for (const auto function : declarations.functions()) {
        const auto callable = function.value.callable;
        if (!callable_names[callable.index()].has_value()) {
            claim_entity(function.value.module_id, function.value.name, callable, callable_names);
        }
    }
    for (const auto structure : declarations.structures()) {
        if (!structure_names[structure.id.index()].has_value()
            && semantic.generic_nominal_instance(structure.id) == nullptr) {
            claim_entity(
                structure.value.module_id,
                structure.value.name,
                structure.id,
                structure_names
            );
        }
    }
    for (const auto enumeration : declarations.enumerations()) {
        if (!enumeration_names[enumeration.id.index()].has_value()
            && semantic.generic_nominal_instance(enumeration.id) == nullptr) {
            claim_entity(
                enumeration.value.module_id,
                enumeration.value.name,
                enumeration.id,
                enumeration_names
            );
        }
    }

    auto generic_types = std::map<NominalDeclarationRef, TypeID>();
    for (const auto type : semantic.types().entries()) {
        if (const auto* record = std::get_if<StructTypeValue>(&type.value.value)) {
            if (semantic.generic_nominal_instance(record->structure)) {
                generic_types.emplace(record->structure, type.id);
            }
        } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&type.value.value)) {
            if (semantic.generic_nominal_instance(enumeration->enumeration)) {
                generic_types.emplace(enumeration->enumeration, type.id);
            }
        }
    }
    for (const auto module : declarations.modules()) {
        auto instances = std::vector<const GenericNominalInstance*>();
        auto requests = std::vector<TargetContentName>();
        for (const auto& instance : semantic.generic_nominal_instances()) {
            const auto& definition = semantic.generic_declaration_contract(instance.definition);
            if (definition.module_id != module.id) {
                continue;
            }
            auto content = type_content_key(semantic, generic_types.at(instance.declaration));
            const auto source_name = source_target_identifier(provenance.spelling(definition.name));
            requests.push_back({
                .preferred =
                    std::format("{}_{}", source_name.spelling(), content_name_digest(content)),
                .content = std::move(content),
            });
            instances.push_back(std::addressof(instance));
        }
        const auto names = claim_content_identifiers(requests, module_occupied[module.id.index()]);
        for (const auto [instance, name] : std::views::zip(instances, names)) {
            instance->declaration.visit([&](auto id) noexcept {
                const auto entity = TargetEntityName {
                    .owner_module = module.id,
                    .relative_name = TargetName {name},
                };
                if constexpr (std::same_as<decltype(id), StructID>) {
                    structure_names[id.index()] = entity;
                } else {
                    enumeration_names[id.index()] = entity;
                }
            });
        }
    }
    for (const auto enumeration : declarations.enumerations()) {
        auto occupied = std::flat_set<std::string>();
        const auto enclosing =
            enumeration_names[enumeration.id.index()]->relative_name.components().back().spelling();
        for (const auto case_id : enumeration.value.cases) {
            const auto& enum_case = declarations.enum_case(case_id);
            const auto preferred =
                source_target_identifier(provenance.spelling(enum_case.name), enclosing);
            enum_case_names[case_id.index()] =
                claim_target_identifier(preferred.spelling(), occupied);
        }
    }
    const auto generated_namespace = generated_target_namespace();
    const auto domain_namespace = linkage_target_namespace(linkage);
    auto namespace_prefix = std::vector<TargetIdentifier>(
        generated_namespace.components().begin(),
        generated_namespace.components().end()
    );
    namespace_prefix.insert(
        namespace_prefix.end(),
        domain_namespace.components().begin(),
        domain_namespace.components().end()
    );

    auto modules = std::vector<std::optional<TargetModuleNames>>(module_count);
    for (const auto module_record : declarations.modules()) {
        const auto& path = provenance.module_record(module_record.value.provenance_module).path;
        const auto module_namespace = TargetIdentifier::from_spelling(
            derive_module_namespace_id(path.value()).namespace_identifier()
        );
        auto qualified = namespace_prefix;
        qualified.push_back(module_namespace);
        auto public_components = std::vector<TargetIdentifier> {
            TargetIdentifier::from_spelling("carven"),
            TargetIdentifier::from_spelling("api")
        };
        for (const auto& component : path.components()) {
            public_components.push_back(public_target_identifier(component));
        }
        auto public_functions = std::flat_map<FunctionID, TargetIdentifier>();
        for (const auto item : module_record.value.items) {
            if (const auto* id = std::get_if<FunctionID>(&item)) {
                const auto& function = declarations.function(*id);
                if (function.cpp_export_origin.has_value()) {
                    public_functions.emplace(
                        *id,
                        public_target_identifier(provenance.spelling(function.name))
                    );
                }
            }
        }
        modules[module_record.id.index()] = TargetModuleNames {
            .qualified_namespace_name = TargetName::from_components(std::move(qualified)),
            .module_namespace_name = TargetName {module_namespace},
            .public_namespace_name = TargetName::from_components(std::move(public_components)),
            .public_functions = std::move(public_functions),
            .reserved_identifiers = std::move(module_occupied[module_record.id.index()]),
            .body_reserved_identifiers = {},
        };
    }

    for (const auto module_record : declarations.modules()) {
        const auto allocate_closure = [&](CallableID callable) noexcept {
            if (closures.owner(callable) != module_record.id
                || closure_type_names[callable.index()].has_value()) {
                invariant_violation(
                    "target closure naming received a duplicate or foreign closure"
                );
            }
            const auto name = claim_target_type_identifier(
                std::format("Closure{}", closures.module_ordinals[callable.index()]),
                modules[module_record.id.index()]->reserved_identifiers
            );
            closure_type_names[callable.index()] = TargetEntityName {
                .owner_module = module_record.id,
                .relative_name = TargetName {name},
            };
        };
        for (const auto callable : closures.production(module_record.id)) {
            allocate_closure(callable);
        }
        for (const auto callable : closures.tests(module_record.id)) {
            allocate_closure(callable);
        }
    }

    auto total_case_names =
        take_total(std::move(enum_case_names), "target name plan did not name every enum case");
    auto payload_enums = std::vector<std::optional<TargetPayloadEnumNames>>(enumeration_count);
    for (const auto enumeration : declarations.enumerations()) {
        if (!std::holds_alternative<PayloadEnumRepresentation>(enumeration.value.representation)) {
            continue;
        }
        auto cases = std::vector<TargetIdentifier>();
        cases.reserve(enumeration.value.cases.size());
        for (const auto case_id : enumeration.value.cases) {
            cases.push_back(total_case_names[case_id.index()]);
        }
        payload_enums[enumeration.id.index()] = payload_enum_names(cases);
    }

    auto tests = std::vector<std::optional<TargetIdentifier>>(semantic.tests().size());
    auto module_test_ordinals = std::vector<std::size_t>(module_count);
    for (const auto test : semantic.tests().entries()) {
        if (test.value.is_const) {
            continue;
        }
        const auto module_id = test.value.module_id;
        if (module_id.owner() != semantic.identity() || module_id.index() >= module_count) {
            invariant_violation("test names a foreign or unknown module");
        }
        const auto ordinal = module_test_ordinals[module_id.index()]++;
        tests[test.id.index()] = claim_target_identifier(
            std::format("carven_generated_test_{}", ordinal),
            modules[module_id.index()]->reserved_identifiers
        );
    }

    auto module_runners = std::vector<std::optional<TargetIdentifier>>(module_count);
    for (const auto module_record : declarations.modules()) {
        module_runners[module_record.id.index()] = claim_target_identifier(
            "carven_run_module_tests",
            modules[module_record.id.index()]->reserved_identifiers
        );
    }

    for (auto& module_names : modules) {
        module_names->body_reserved_identifiers = module_names->reserved_identifiers;
    }

    auto content_names = TargetContentNames {
        .queries = std::vector<std::optional<TargetIdentifier>>(semantic.types().size()),
        .displays = std::vector<std::optional<TargetIdentifier>>(semantic.types().size()),
        .constants = std::vector<std::optional<TargetIdentifier>>(semantic.constants().size()),
    };
    auto requests = std::vector<TargetContentName>();
    auto destinations = std::vector<std::optional<TargetIdentifier>*>();
    const auto request_name =
        [&](std::string_view prefix, std::string key, auto& destination) noexcept {
            requests.push_back({
                .preferred = std::format("{}{}", prefix, content_name_digest(key)),
                .content = std::move(key),
            });
            destinations.push_back(std::addressof(destination));
        };
    for (const auto type : semantic.types().entries()) {
        const auto* native = std::get_if<CppTypeValue>(&type.value.value);
        if (native != nullptr && std::holds_alternative<CppQueryType>(native->form)) {
            request_name(
                "CarvenQuery_",
                type_content_key(semantic, type.id),
                content_names.queries[type.id.index()]
            );
        } else if (std::holds_alternative<StructTypeValue>(type.value.value)
                   || std::holds_alternative<EnumTypeValue>(type.value.value)) {
            request_name(
                "CarvenDisplay_",
                type_content_key(semantic, type.id),
                content_names.displays[type.id.index()]
            );
        }
    }
    for (const auto constant : semantic.constants().entries()) {
        if (std::holds_alternative<SliceConstant>(constant.value.value)) {
            request_name(
                "carven_constant_",
                constant_content_key(semantic, constant.id),
                content_names.constants[constant.id.index()]
            );
        }
    }
    auto occupied_content = std::flat_set<std::string>();
    const auto identifiers = claim_content_identifiers(requests, occupied_content);
    for (const auto [index, identifier] : std::views::enumerate(identifiers)) {
        *destinations[index] = identifier;
    }
    for (const auto module_record : declarations.modules()) {
        auto instances = std::vector<const StaticInstance*>();
        auto instance_requests = std::vector<TargetContentName>();
        for (const auto& instance : semantic.static_instances()) {
            const auto& function = declarations.function(instance.function);
            if (function.module_id != module_record.id) {
                continue;
            }
            const auto spelling = callable_names[function.callable.index()]
                                      ->relative_name.components()
                                      .back()
                                      .spelling();
            auto content = std::format("{}:{}", spelling.size(), spelling);
            for (const auto argument : instance.arguments) {
                const auto key = constant_content_key(semantic, argument);
                content += std::format("{}:{}", key.size(), key);
            }
            instance_requests.push_back({
                .preferred =
                    static_instance_spelling(semantic, spelling, instance.arguments, content),
                .content = std::move(content),
            });
            instances.push_back(std::addressof(instance));
        }
        const auto instance_names = claim_content_identifiers(
            instance_requests,
            modules[module_record.id.index()]->reserved_identifiers
        );
        for (const auto [index, instance] : std::views::enumerate(instances)) {
            callable_names[instance->callable.index()] = TargetEntityName {
                .owner_module = module_record.id,
                .relative_name = TargetName {instance_names[index]},
            };
        }
    }

    return TargetNamePlan(
        semantic.identity(),
        take_total(std::move(modules), "target name plan did not name every module"),
        generated_namespace,
        domain_namespace,
        take_total(std::move(structure_names), "target name plan did not name every structure"),
        take_total(std::move(enumeration_names), "target name plan did not name every enumeration"),
        std::move(callable_names),
        std::move(closure_type_names),
        std::move(total_case_names),
        std::move(payload_enums),
        std::move(tests),
        take_total(std::move(module_runners), "target name plan did not name every module runner"),
        std::move(content_names)
    );
}
