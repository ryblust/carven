module carven:semantic.analysis.generics.validation.impl;

import :diagnostics.builder;
import :semantic.analysis.construction.limits;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.generic;
import :semantic.semir.traversal;
import :support.graph;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto definition_contract(const GenericDeclarationDefinition& definition) noexcept
    -> const GenericDeclarationContract& {
    return definition.visit(
        [](const auto& value) static noexcept -> const GenericDeclarationContract& {
            return value.contract;
        }
    );
}

auto definition_fields(const GenericDeclarationDefinition& definition) noexcept
    -> std::vector<std::pair<GenericTypeID, ProgramOriginID>> {
    auto result = std::vector<std::pair<GenericTypeID, ProgramOriginID>>();
    definition.visit(
        Overloaded {
            [&](const GenericRecordDefinition& record) noexcept {
                for (const auto& field : record.fields) {
                    result.emplace_back(field.type, field.origin);
                }
            },
            [&](const GenericCallableDefinition& function) noexcept {
                for (const auto& parameter : function.signature.parameters) {
                    if (const auto* type = std::get_if<GenericTypeID>(&parameter.type)) {
                        result.emplace_back(*type, function.contract.origin);
                    }
                }
                if (const auto* type = std::get_if<GenericTypeID>(&function.signature.result)) {
                    result.emplace_back(*type, function.contract.origin);
                }
            },
            [&](const GenericEnumDefinition& enumeration) noexcept {
                for (const auto& member : enumeration.cases) {
                    for (const auto payload : member.payload_types) {
                        result.emplace_back(payload, member.origin);
                    }
                }
            },
        }
    );
    return result;
}

struct ParameterFlowEdge final {
    std::size_t source;
    std::size_t target;
    bool constructs;
    ProgramOriginID origin;
};

} // namespace

auto ProgramDraft::validate_generic_definitions(std::optional<GenericDeclarationID> root) noexcept
    -> AnalysisResult<void> {
    if (root && storage.checked_generic_definitions.contains(*root)) {
        return {};
    }
    auto failure = std::optional<AnalysisFailure>();
    const auto diagnose = [&](DiagnosticCode code,
                              std::string_view message,
                              ProgramOriginID origin,
                              std::string_view label) noexcept {
        auto diagnostic = DiagnosticBuilder(code, std::string(message));
        diagnostic.primary(source_span(origin), std::string(label));
        failure = diagnostics().error(diagnostic.build());
    };
    auto selected = std::set<GenericDeclarationID>();
    auto pending = std::vector<std::pair<GenericDeclarationID, ProgramOriginID>>();
    if (root) {
        const auto definition = generic_declaration_copy(*root);

        pending.emplace_back(*root, definition_contract(definition).origin);

    } else {
        for (auto index = 0uz; index < storage.generic_definitions.size(); ++index) {
            const auto owner =
                GenericDeclarationID(program_identity, static_cast<std::uint32_t>(index));
            const auto definition = generic_declaration_copy(owner);

            pending.emplace_back(owner, definition_contract(definition).origin);
        }
    }
    const auto collect_dependencies =
        [&](auto&& self, ConstructionTypeRef type, ProgramOriginID origin) noexcept -> void {
        const auto* generic = std::get_if<GenericTypeID>(&type);
        if (!generic) {
            return;
        }
        const auto expression = generic_type_copy(*generic);
        if (const auto* application = std::get_if<GenericNominalApplication>(&expression)) {
            pending.emplace_back(application->definition, origin);
        } else if (const auto* closure = std::get_if<GenericClosureType>(&expression)) {
            pending.emplace_back(closure->definition, origin);
        }
        visit_generic_type_children(expression, [&](ConstructionTypeRef child) noexcept {
            self(self, child, origin);
        });
    };
    while (!pending.empty()) {
        const auto [owner, origin] = pending.back();
        pending.pop_back();
        if (!selected.insert(owner).second) {
            continue;
        }
        if (!storage.generic_definitions[owner.index()]) {
            diagnose(
                DiagnosticCode::TypeGenericDefinition,
                "generic dependency head is not complete",
                origin,
                "this checked definition depends on an unfinished generic declaration"
            );
            return std::unexpected(*failure);
        }
        const auto definition = generic_declaration_copy(owner);
        if (const auto scope = definition_contract(definition).parameter_scope) {
            pending.emplace_back(*scope, origin);
        }
        for (const auto [type, origin] : definition_fields(definition)) {
            collect_dependencies(collect_dependencies, type, origin);
        }
        if (std::holds_alternative<GenericCallableDefinition>(definition)
            && has_generic_callable_body(owner)) {
            const auto calls = [&](const SemanticExpression& expression) noexcept {
                collect_dependencies(
                    collect_dependencies,
                    expression.type.construction(),
                    expression.origin
                );
                if (const auto* call = std::get_if<SemGenericCall>(&expression.value)) {
                    pending.emplace_back(call->definition, expression.origin);
                    for (const auto& argument : call->environment) {
                        visit_generic_argument_types(
                            argument,
                            [&](ConstructionTypeRef type) noexcept {
                                collect_dependencies(collect_dependencies, type, expression.origin);
                            }
                        );
                    }
                } else if (const auto* closure = std::get_if<SemClosure>(&expression.value)) {
                    if (const auto* type = std::get_if<GenericTypeID>(&closure->callable)) {
                        collect_dependencies(collect_dependencies, *type, expression.origin);
                    }
                } else if (const auto* call = std::get_if<SemCall>(&expression.value);
                           call && call->target) {
                    if (const auto* type = std::get_if<GenericTypeID>(&*call->target)) {
                        collect_dependencies(collect_dependencies, *type, expression.origin);
                    }
                }
            };
            const auto& body = generic_callable_body(owner);
            visit_semantic_nodes(body.region, calls);
            if (body.residual) {
                visit_semantic_nodes(*body.residual, calls);
            }
        }
    }
    if (std::ranges::all_of(selected, [&](auto id) noexcept {
            return storage.checked_generic_definitions.contains(id);
        })) {
        return {};
    }
    auto offsets = std::vector<std::size_t> {0uz};
    for (auto index = 0uz; index < storage.generic_definitions.size(); ++index) {
        const auto owner =
            GenericDeclarationID(program_identity, static_cast<std::uint32_t>(index));
        auto count = 0uz;
        if (selected.contains(owner) && generic_parameter_owner(owner) == owner) {
            count = generic_parameter_contract_copy(owner).parameters.size();
        }
        offsets.push_back(offsets.back() + count);
    }
    const auto parameter_offset = [&](GenericDeclarationID definition) noexcept {
        return offsets[generic_parameter_owner(definition).index()];
    };
    auto flow = std::vector<ParameterFlowEdge>();
    // A descriptor binds both the declaration and parameter identities; source
    // names and source trees do not participate in checking or substitution.
    for (auto index = 0uz; index < storage.generic_definitions.size(); ++index) {
        const auto owner =
            GenericDeclarationID(program_identity, static_cast<std::uint32_t>(index));
        if (!selected.contains(owner)) {
            continue;
        }
        const auto definition = generic_declaration_copy(owner);
        const auto& contract = definition_contract(definition);
        const auto parameter_owner = generic_parameter_owner(owner);
        const auto parameters = generic_parameter_contract_copy(owner);

        auto parameter_names = std::set<ProgramSpellingID>();
        for (const auto parameter : parameters.parameters) {
            if (!parameter_names.insert(parameter).second) {
                diagnose(
                    DiagnosticCode::TypeGenericDefinition,
                    "generic type parameter is declared more than once",
                    contract.origin,
                    "parameter names must be distinct"
                );
            }
        }
        const auto collect_origins =
            [&](auto&& self,
                ConstructionTypeRef type,
                bool constructs,
                std::vector<std::pair<GenericTypeParameter, bool>>& result) noexcept -> void {
            const auto* generic = std::get_if<GenericTypeID>(&type);
            if (!generic) {
                return;
            }
            generic_type_copy(*generic).visit(
                Overloaded {
                    [](TypeID) static noexcept {},
                    [&](const GenericTypeParameter& parameter) noexcept {
                        result.emplace_back(parameter, constructs);
                    },
                    [&](const GenericArrayType& array) noexcept {
                        self(self, array.element, true, result);
                    },
                    [&](const GenericTupleType& tuple) noexcept {
                        for (const auto element : tuple.elements) {
                            self(self, element, true, result);
                        }
                    },
                    [&](const GenericSliceType& slice) noexcept {
                        self(self, slice.element, true, result);
                    },
                    [&](const GenericVectorType& sequence) noexcept {
                        self(self, sequence.element, true, result);
                    },
                    [&](const GenericPointerType& pointer) noexcept {
                        self(self, pointer.target, true, result);
                    },
                    [&](const GenericOperationType& operation) noexcept {
                        self(self, operation.success, true, result);
                    },
                    [&](const GenericCallableViewType& view) noexcept {
                        for (const auto& parameter : view.parameters) {
                            self(self, parameter.type, true, result);
                        }
                        self(self, view.result, true, result);
                    },
                    [&](const GenericCppNamedType& native) noexcept {
                        for (const auto argument : native.arguments) {
                            self(self, argument, true, result);
                        }
                    },
                    [&](const GenericCppQueryType& query) noexcept {
                        visit_cpp_query_types(query, [&](ConstructionTypeRef type) noexcept {
                            self(self, type, true, result);
                        });
                    },
                    [&]<typename Application>(const Application& application) noexcept
                        requires (std::same_as<Application, GenericNominalApplication>
                                  || std::same_as<Application, GenericClosureType>)
                    {
                        for (const auto& argument : application.arguments) {
                            visit_generic_argument_types(
                                argument,
                                [&](ConstructionTypeRef type) noexcept {
                                    self(self, type, true, result);
                                }
                            );
                        }
                    },
                    }
            );
        };
        const auto check = [&](auto&& self, ConstructionTypeRef type) noexcept -> void {
            const auto* generic = std::get_if<GenericTypeID>(&type);
            if (!generic) {
                return;
            }
            generic_type_copy(*generic).visit(
                Overloaded {
                    [](TypeID) static noexcept {},
                    [&](const GenericTypeParameter& parameter) noexcept {
                        if (parameter.definition != parameter_owner
                            || parameter.index >= parameters.parameters.size()) {
                            diagnose(
                                DiagnosticCode::TypeGenericDefinition,
                                "generic field refers to a type parameter outside its definition",
                                contract.origin,
                                "type parameters are scoped to their declaring definition"
                            );
                        }
                    },
                    [&](const GenericArrayType& array) noexcept { self(self, array.element); },
                    [&](const GenericTupleType& tuple) noexcept {
                        for (const auto element : tuple.elements) {
                            self(self, element);
                        }
                    },
                    [&](const GenericSliceType& slice) noexcept { self(self, slice.element); },
                    [&](const GenericVectorType& sequence) noexcept {
                        self(self, sequence.element);
                    },
                    [&](const GenericPointerType& pointer) noexcept { self(self, pointer.target); },
                    [&](const GenericOperationType& operation) noexcept {
                        self(self, operation.success);
                    },
                    [&](const GenericCallableViewType& view) noexcept {
                        for (const auto& parameter : view.parameters) {
                            self(self, parameter.type);
                        }
                        self(self, view.result);
                    },
                    [&](const GenericCppNamedType& native) noexcept {
                        for (const auto argument : native.arguments) {
                            self(self, argument);
                        }
                    },
                    [&](const GenericCppQueryType& query) noexcept {
                        visit_cpp_query_types(query, [&](ConstructionTypeRef type) noexcept {
                            self(self, type);
                        });
                    },
                    [&]<typename Application>(const Application& application) noexcept
                        requires (std::same_as<Application, GenericNominalApplication>
                                  || std::same_as<Application, GenericClosureType>)
                    {
                        const auto target = generic_parameter_contract_copy(application.definition);
                        auto bounds = check_generic_bounds(
                            application.definition,
                            application.arguments,
                            contract.origin
                        );
                        if (!bounds) {
                            failure = bounds.error();
                        }
                        const auto target_count = target.parameters.size();
                        if (target_count != application.arguments.size()) {
                            diagnose(
                                DiagnosticCode::TypeGenericArguments,
                                "generic type argument count differs from its declaration",
                                contract.origin,
                                "every generic application supplies its complete type argument list"
                            );
                        }
                        for (auto argument = 0uz; argument < application.arguments.size();
                             ++argument) {
                            if (argument < target_count
                                && !generic_argument_matches(
                                    target.parameter_kinds[argument],
                                    application.arguments[argument]
                                )) {
                                diagnose(
                                    DiagnosticCode::TypeGenericArguments,
                                    "generic argument kind differs from its parameter",
                                    contract.origin,
                                    "object types and failure sets are distinct argument domains"
                                );
                                continue;
                            }
                            visit_generic_argument_types(
                                application.arguments[argument],
                                [&](ConstructionTypeRef type) noexcept { self(self, type); }
                            );
                            if (argument >= target_count) {
                                continue;
                            }
                            auto origins = std::vector<std::pair<GenericTypeParameter, bool>>();
                            visit_generic_argument_types(
                                application.arguments[argument],
                                [&](ConstructionTypeRef type) noexcept {
                                    collect_origins(collect_origins, type, false, origins);
                                }
                            );

                            for (const auto& [parameter, constructs] : origins) {
                                if (parameter.definition == parameter_owner
                                    && parameter.index < parameters.parameters.size()) {
                                    flow.push_back(
                                        {.source = parameter_offset(owner) + parameter.index,
                                         .target =
                                             parameter_offset(application.definition) + argument,
                                         .constructs = constructs,
                                         .origin = contract.origin}
                                    );
                                }
                            }
                        }
                    },
                    }
            );
        };
        for (const auto& field : definition_fields(definition)) {
            check(check, field.first);
        }
        if (std::holds_alternative<GenericCallableDefinition>(definition)
            && has_generic_callable_body(owner)) {
            const auto calls = [&](const SemanticExpression& expression) noexcept {
                check(check, expression.type.construction());
                if (const auto* call = std::get_if<SemCall>(&expression.value);
                    call && call->target) {
                    if (const auto* type = std::get_if<GenericTypeID>(&*call->target)) {
                        check(check, *type);
                    }
                }
                const auto* call = std::get_if<SemGenericCall>(&expression.value);
                if (!call) {
                    return;
                }
                for (const auto [argument, type] : std::views::enumerate(call->environment)) {
                    auto origins = std::vector<std::pair<GenericTypeParameter, bool>>();
                    visit_generic_argument_types(type, [&](ConstructionTypeRef type) noexcept {
                        collect_origins(collect_origins, type, false, origins);
                    });

                    for (const auto& [parameter, constructs] : origins) {
                        if (parameter.definition == parameter_owner) {
                            flow.push_back({
                                parameter_offset(owner) + parameter.index,
                                parameter_offset(call->definition) + argument,
                                constructs,
                                expression.origin,
                            });
                        }
                    }
                }
            };
            const auto& body = generic_callable_body(owner);
            visit_semantic_nodes(body.region, calls);
            if (body.residual) {
                visit_semantic_nodes(*body.residual, calls);
            }
        }
    }
    if (failure) {
        return std::unexpected(*failure);
    }
    auto adjacency = std::vector<std::vector<std::uint32_t>>(offsets.back());
    for (const auto& edge : flow) {
        adjacency[edge.source].push_back(static_cast<std::uint32_t>(edge.target));
    }
    const auto components = strongly_connected_components(std::move(adjacency));
    auto reported = std::set<std::uint32_t>();
    for (const auto& edge : flow) {
        const auto component = components.component_of[edge.source];
        if (edge.constructs
            && component == components.component_of[edge.target]
            && reported.insert(component).second) {
            diagnose(
                DiagnosticCode::TypeGenericExpansion,
                "generic parameter flow grows a type argument around a recursive cycle",
                edge.origin,
                "recursive applications may forward or permute parameters without constructing larger arguments"
            );
        }
    }
    if (failure) {
        return std::unexpected(*failure);
    }
    // The finite flow graph also permits symbolic storage checking with rigid
    // arguments. Substitution exposes cycles hidden behind another owner's T.
    using ApplicationKey = std::tuple<GenericDeclarationID, GenericEnvironment, bool>;
    auto finished = std::set<ApplicationKey>();
    auto active = std::set<ApplicationKey>();
    auto work = 0uz;
    auto depth = 0uz;
    const auto visit_storage = [&](auto&& self,
                                   ConstructionTypeRef type,
                                   bool owns_storage,
                                   ProgramOriginID origin) noexcept -> void {
        if (failure) {
            return;
        }
        if (++work > maximum_generic_type_work || depth >= maximum_generic_depth) {
            diagnose(
                DiagnosticCode::TypeGenericLimits,
                depth >= maximum_generic_depth
                    ? "generic storage checking exceeded its depth budget"
                    : "generic storage checking exceeded its work budget",
                origin,
                "the compiler's generic construction budget was exhausted"
            );
            return;
        }
        struct StorageDepth final {
            std::size_t& depth;
            ~StorageDepth() { --depth; }
        };
        ++depth;
        const auto active_depth = StorageDepth {.depth = depth};
        const auto* generic = std::get_if<GenericTypeID>(&type);
        if (!generic) {
            if (type_contains_callable_view(*this, type)) {
                diagnose(
                    DiagnosticCode::TypeCallableViewEscape,
                    "non-owning callable view cannot be stored in a structure or enum",
                    origin,
                    "this field or payload contains a callable view"
                );
            }
            return;
        }
        generic_type_copy(*generic).visit(
            Overloaded {
                [&](TypeID concrete) noexcept {
                    if (type_contains_callable_view(*this, ConstructionTypeRef {concrete})) {
                        diagnose(
                            DiagnosticCode::TypeCallableViewEscape,
                            "non-owning callable view cannot be stored in a structure or enum",
                            origin,
                            "this field or payload contains a callable view"
                        );
                    }
                },
                [](const GenericTypeParameter&) static noexcept {},
                [](const GenericPointerType&) static noexcept {},
                [&](const GenericSliceType& slice) noexcept {
                    // Slices propagate callable containment without owning their elements.
                    self(self, slice.element, false, origin);
                },
                [&](const GenericArrayType& array) noexcept {
                    self(self, array.element, owns_storage, origin);
                },
                [&](const GenericTupleType& tuple) noexcept {
                    for (const auto element : tuple.elements) {
                        self(self, element, owns_storage, origin);
                    }
                },
                [&](const GenericVectorType& sequence) noexcept {
                    // Dynamic owning storage breaks layout recursion. Its element
                    // value contract is checked after source heads are complete.
                    self(self, sequence.element, false, origin);
                },
                [&](const GenericOperationType& operation) noexcept {
                    self(self, operation.success, false, origin);
                },
                [&](const GenericCallableViewType&) noexcept {
                    if (owns_storage) {
                        diagnose(
                            DiagnosticCode::TypeCallableViewEscape,
                            "callable views cannot be held by owning aggregate storage",
                            origin,
                            "this field or payload contains a callable view"
                        );
                    }
                },
                [&](const GenericCppNamedType& native) noexcept {
                    for (const auto argument : native.arguments) {
                        self(self, argument, false, origin);
                    }
                },
                [&](const GenericCppQueryType& query) noexcept {
                    visit_cpp_query_types(query, [&](ConstructionTypeRef type) noexcept {
                        self(self, type, false, origin);
                    });
                },
                [&]<typename Application>(const Application& application) noexcept
                    requires (std::same_as<Application, GenericNominalApplication>
                              || std::same_as<Application, GenericClosureType>)
                {
                    const auto key =
                        ApplicationKey(application.definition, application.arguments, owns_storage);
                    if (active.contains(key)) {
                        if (!owns_storage) {
                            return;
                        }
                        diagnose(
                            DiagnosticCode::TypeRecursiveStorage,
                            "generic declarations form recursive by-value storage",
                            origin,
                            "an owning field returns to the same nominal application"
                        );
                        return;
                    }
                    if (finished.contains(key)) {
                        return;
                    }
                    active.insert(key);
                    const auto parameter_owner = generic_parameter_owner(application.definition);
                    if constexpr (std::same_as<Application, GenericClosureType>) {
                        if (has_generic_callable_body(application.definition)) {
                            const auto& body = generic_callable_body(application.definition);
                            for (const auto binding : body.inputs.captures) {
                                const auto capture = body.bindings.get(binding);
                                self(
                                    self,
                                    substitute_generic_type(
                                        capture.type,
                                        parameter_owner,
                                        application.arguments
                                    ),
                                    owns_storage
                                        && std::get<CaptureBindingStorage>(capture.storage).mode
                                            == CaptureMode::Value,
                                    capture.origin
                                );
                            }
                        }
                    } else {
                        const auto definition = generic_declaration_copy(application.definition);
                        for (const auto [field, field_origin] : definition_fields(definition)) {
                            self(
                                self,
                                substitute_generic_type(
                                    field,
                                    parameter_owner,
                                    application.arguments
                                ),
                                owns_storage,
                                field_origin
                            );
                        }
                    }
                    active.erase(key);
                    finished.insert(key);
                },
                }
        );
    };
    for (auto index = 0uz; index < storage.generic_definitions.size(); ++index) {
        const auto owner =
            GenericDeclarationID(program_identity, static_cast<std::uint32_t>(index));
        if (!selected.contains(owner)) {
            continue;
        }
        const auto definition = generic_declaration_copy(owner);
        const auto& contract = definition_contract(definition);

        if (std::holds_alternative<GenericCallableDefinition>(definition)) {
            continue;
        }
        auto arguments = generic_parameter_environment(owner);
        const auto application = intern_generic_type(
            GenericNominalApplication {.definition = owner, .arguments = std::move(arguments)}
        );
        visit_storage(visit_storage, application, true, contract.origin);
    }
    if (failure) {
        return std::unexpected(*failure);
    }
    storage.checked_generic_definitions.insert(selected.begin(), selected.end());
    return {};
}
