module carven:backend.generation.plan.names.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :semantic.semir.traversal;
import :semantic.visibility;
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

    const auto record = [&](ModuleID module_id, CallableID callable, bool test) noexcept {
        if (callable.owner() != semantic.identity() || callable.index() >= owners.size()) {
            invariant_violation("closure discovery received a foreign callable");
        }
        auto& owner = owners[callable.index()];
        if (owner.has_value() && *owner != module_id) {
            invariant_violation("one closure callable is constructed by multiple modules");
        }
        if (!owner.has_value()) {
            owner = module_id;
            discovery.push_back(callable);
        }
        (test ? tests : production)[callable.index()] = 1;
    };

    auto visit_body = std::function<void(ModuleID, BodyID, bool)>();
    visit_body = [&](ModuleID module_id, BodyID body_id, bool test) noexcept {
        const auto& body = semantic.bodies().body(body_id);
        visit_semantic_nodes(body.region(), [&](const SemIRExpression& expression) noexcept {
            const auto* closure = std::get_if<SemClosure<TypeID, FailureSetID>>(&expression.value);
            if (closure == nullptr) {
                return;
            }
            const auto& callable = declarations.callable(closure->callable);
            const auto* implementation =
                std::get_if<ClosureBodyImplementation>(&callable.implementation);
            if (implementation == nullptr) {
                invariant_violation(
                    "closure expression references a callable without a closure body"
                );
            }
            record(module_id, closure->callable, test);
            auto& scanned = (test ? scanned_tests : scanned_production)[closure->callable.index()];
            if (scanned == 0) {
                scanned = 1;
                visit_body(module_id, implementation->body, test);
            }
        });
    };

    for (const auto module_record : declarations.modules()) {
        for (const auto item : module_record.value.items) {
            std::visit(
                Overloaded {
                    [&](FunctionID id) noexcept {
                        const auto& callable =
                            declarations.callable(declarations.function(id).callable);
                        if (const auto* implementation =
                                std::get_if<FunctionBodyImplementation>(&callable.implementation)) {
                            visit_body(module_record.id, implementation->body, false);
                        }
                    },
                    [&](TestID id) noexcept {
                        visit_body(module_record.id, semantic.tests().test(id).body, true);
                    },
                    [](StructID) static noexcept {},
                    [](EnumID) static noexcept {},
                    [](ModuleConstantID) static noexcept {},
                },
                item
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
        std::visit(
            Overloaded {
                [](const BuiltinTypeValue&) static noexcept {},
                [](const StructTypeValue&) static noexcept {},
                [](const EnumTypeValue&) static noexcept {},
                [&](const ArrayTypeValue& value) noexcept {
                    self(value.element, destination, active_types);
                },
                [](const FunctionTypeValue&) static noexcept {},
                [&](const ClosureTypeValue& value) noexcept {
                    destination.push_back(value.callable);
                },
                [](const CallableViewTypeValue&) static noexcept {},
                [&](const CppTypeValue& value) noexcept {
                    if (const auto* named = std::get_if<CppNamedType>(&value.form)) {
                        for (const auto argument : named->arguments) {
                            self(argument, destination, active_types);
                        }
                    } else {
                        for (const auto& operand : std::get<CppDeducedType>(value.form).operands) {
                            const auto argument = operand.type;
                            self(argument, destination, active_types);
                        }
                    }
                },
            },
            semantic.types().type(type_id).value
        );
        active_types.erase(type_id);
    };
    for (const auto callable : discovery) {
        const auto& declaration = declarations.callable(callable);
        const auto body_id = std::get<ClosureBodyImplementation>(declaration.implementation).body;
        const auto& body = semantic.bodies().body(body_id);
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
        visit_semantic_nodes(body.region(), [&](const SemIRExpression& expression) noexcept {
            if (const auto* child =
                    std::get_if<SemClosure<TypeID, FailureSetID>>(&expression.value)) {
                dependencies[callable.index()].push_back(child->callable);
            }
        });
        std::ranges::sort(dependencies[callable.index()]);
        const auto unique = std::ranges::unique(dependencies[callable.index()]);
        dependencies[callable.index()].erase(unique.begin(), unique.end());
    }

    auto production_order = std::vector<std::vector<CallableID>>(module_count);
    auto test_order = std::vector<std::vector<CallableID>>(module_count);
    auto state = std::vector<std::uint8_t>(callable_count);
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
            if (!owners[dependency.index()].has_value()
                || *owners[dependency.index()] != module_id) {
                invariant_violation("closure target type depends on a foreign or unowned closure");
            }
            if (production[dependency.index()] != 0 || (test && tests[dependency.index()] != 0)) {
                order(dependency, test && production[dependency.index()] == 0);
            }
        }
        state[callable.index()] = 2;
        auto& destination =
            test ? test_order[module_id.index()] : production_order[module_id.index()];
        destination.push_back(callable);
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

    return {
        .semantic_identity = semantic.identity(),
        .owner_modules = std::move(owners),
        .production_definitions = std::move(production_order),
        .test_definitions = std::move(test_order),
    };
}

auto plan_names(
    const SemIRProgram& semantic,
    const LinkageDomainID& linkage,
    const TargetClosureCatalog& closures
) noexcept -> TargetNamePlan {
    const auto& declarations = semantic.declarations();
    const auto provenance = semantic.provenance();
    const auto allocator = TargetNameAllocator {};

    const auto module_count = total_size<ModuleID>(declarations.modules());
    const auto function_count = total_size<FunctionID>(declarations.functions());
    const auto structure_count = total_size<StructID>(declarations.structures());
    const auto enumeration_count = total_size<EnumID>(declarations.enumerations());
    const auto enum_case_count = total_size<EnumCaseID>(declarations.enum_cases());
    const auto callable_count = total_size<CallableID>(declarations.callables());

    auto function_names = std::vector<std::optional<TargetEntityName>>(function_count);
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
            const auto preferred = allocator.source(provenance.spelling(spelling));
            const auto claimed = TargetNameAllocator::claim_source(
                preferred.spelling(),
                module_occupied[module_id.index()]
            );
            rows[id.index()] = TargetEntityName {
                .owner_module = module_id,
                .relative_name = TargetName {claimed},
            };
        };

    const auto allocate_item = [&](ModuleID module_id, ModuleItem item, bool published) noexcept {
        std::visit(
            Overloaded {
                [&](FunctionID id) noexcept {
                    const auto& value = declarations.function(id);
                    if ((value.visibility != DeclarationVisibility::Module) == published
                        && !function_names[id.index()].has_value()) {
                        claim_entity(module_id, value.name, id, function_names);
                    }
                },
                [&](StructID id) noexcept {
                    const auto& value = declarations.structure(id);
                    if ((value.visibility != DeclarationVisibility::Module) == published
                        && !structure_names[id.index()].has_value()) {
                        claim_entity(module_id, value.name, id, structure_names);
                    }
                },
                [&](EnumID id) noexcept {
                    const auto& value = declarations.enumeration(id);
                    if ((value.visibility != DeclarationVisibility::Module) == published
                        && !enumeration_names[id.index()].has_value()) {
                        claim_entity(module_id, value.name, id, enumeration_names);
                    }
                },
                [](ModuleConstantID) static noexcept {},
                [](TestID) static noexcept {},
            },
            item
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
        if (!function_names[function.id.index()].has_value()) {
            claim_entity(
                function.value.module_id,
                function.value.name,
                function.id,
                function_names
            );
        }
        const auto callable = function.value.callable;
        if (callable.owner() != semantic.identity() || callable.index() >= callable_names.size()) {
            invariant_violation("function names a foreign or unknown callable");
        }
        if (callable_names[callable.index()].has_value()) {
            invariant_violation("multiple functions assign names to one callable");
        }
        callable_names[callable.index()] = function_names[function.id.index()];
    }
    for (const auto structure : declarations.structures()) {
        if (!structure_names[structure.id.index()].has_value()) {
            claim_entity(
                structure.value.module_id,
                structure.value.name,
                structure.id,
                structure_names
            );
        }
    }
    for (const auto enumeration : declarations.enumerations()) {
        if (!enumeration_names[enumeration.id.index()].has_value()) {
            claim_entity(
                enumeration.value.module_id,
                enumeration.value.name,
                enumeration.id,
                enumeration_names
            );
        }
        auto occupied = std::flat_set<std::string>();
        const auto enclosing =
            enumeration_names[enumeration.id.index()]->relative_name.components().back().spelling();
        for (const auto case_id : enumeration.value.cases) {
            const auto& enum_case = declarations.enum_case(case_id);
            const auto preferred = allocator.source(provenance.spelling(enum_case.name), enclosing);
            enum_case_names[case_id.index()] =
                TargetNameAllocator::claim_source(preferred.spelling(), occupied);
        }
    }
    const auto generated_namespace = TargetNameAllocator::generated_namespace();
    const auto domain_namespace = TargetNameAllocator::domain_namespace(linkage);
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
        const auto module_namespace = TargetNameAllocator::fixed(
            derive_module_namespace_id(path.value()).namespace_identifier()
        );
        auto qualified = namespace_prefix;
        qualified.push_back(module_namespace);
        modules[module_record.id.index()] = TargetModuleNames {
            .qualified_namespace_name = TargetName::from_components(std::move(qualified)),
            .module_namespace_name = TargetName {module_namespace},
            .reserved_identifiers = std::move(module_occupied[module_record.id.index()]),
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
            const auto name = TargetNameAllocator::claim_type(
                std::format("Closure{}", callable.index()),
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
        const auto module_id = test.value.module_id;
        if (module_id.owner() != semantic.identity() || module_id.index() >= module_count) {
            invariant_violation("test names a foreign or unknown module");
        }
        const auto ordinal = module_test_ordinals[module_id.index()]++;
        tests[test.id.index()] = TargetNameAllocator::claim_source(
            std::format("carven_generated_test_{}", ordinal),
            modules[module_id.index()]->reserved_identifiers
        );
    }

    auto module_runners = std::vector<std::optional<TargetIdentifier>>(module_count);
    for (const auto module_record : declarations.modules()) {
        module_runners[module_record.id.index()] = TargetNameAllocator::claim_source(
            "carven_run_module_tests",
            modules[module_record.id.index()]->reserved_identifiers
        );
    }

    return TargetNamePlan(
        semantic.identity(),
        take_total(std::move(modules), "target name plan did not name every module"),
        generated_namespace,
        domain_namespace,
        take_total(std::move(function_names), "target name plan did not name every function"),
        take_total(std::move(structure_names), "target name plan did not name every structure"),
        take_total(std::move(enumeration_names), "target name plan did not name every enumeration"),
        std::move(callable_names),
        std::move(closure_type_names),
        std::move(total_case_names),
        std::move(payload_enums),
        take_total(std::move(tests), "target name plan did not name every test"),
        take_total(std::move(module_runners), "target name plan did not name every module runner")
    );
}
