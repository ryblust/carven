module carven:semantic.analysis.generics.validation.impl;

import :diagnostics.builder;
import :semantic.analysis.construction.limits;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.generic;
import :semantic.semir.sequence;
import :support.graph;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto definition_contract(const GenericNominalDefinition& definition) noexcept
    -> const GenericDeclarationContract& {
    return definition.visit([](const auto& value) noexcept -> const GenericDeclarationContract& {
        return value.contract;
    });
}

auto definition_fields(const GenericNominalDefinition& definition) noexcept
    -> std::vector<std::pair<GenericTypeID, ProgramOriginID>> {
    auto result = std::vector<std::pair<GenericTypeID, ProgramOriginID>>();
    definition.visit(
        Overloaded {
            [&](const GenericRecordDefinition& record) noexcept {
                for (const auto& field : record.fields) {
                    result.emplace_back(field.type, field.origin);
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
        [&](auto&& self, GenericTypeID type, ProgramOriginID origin) noexcept -> void {
        generic_type_copy(type).visit(
            Overloaded {
                [](TypeID) static noexcept {},
                [](const GenericTypeParameter&) static noexcept {},
                [&](const GenericArrayType& array) noexcept { self(self, array.element, origin); },
                [&](const GenericSliceType& slice) noexcept { self(self, slice.element, origin); },
                [&](const GenericOwnedSequenceType& sequence) noexcept {
                    self(self, sequence.element, origin);
                },
                [&](const GenericPointerType& pointer) noexcept {
                    self(self, pointer.target, origin);
                },
                [&](const GenericNominalApplication& application) noexcept {
                    pending.emplace_back(application.definition, origin);
                    for (const auto argument : application.arguments) {
                        self(self, argument, origin);
                    }
                },
            }
        );
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
        for (const auto [type, origin] : definition_fields(definition)) {
            collect_dependencies(collect_dependencies, type, origin);
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
        if (selected.contains(owner)) {
            count = definition_contract(*storage.generic_definitions[index]).parameters.size();
        }
        offsets.push_back(offsets.back() + count);
    }
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
        auto parameter_names = std::set<ProgramSpellingID>();
        for (const auto parameter : contract.parameters) {
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
                GenericTypeID type,
                bool constructs,
                std::vector<std::pair<GenericTypeParameter, bool>>& result) noexcept -> void {
            generic_type_copy(type).visit(
                Overloaded {
                    [](TypeID) static noexcept {},
                    [&](const GenericTypeParameter& parameter) noexcept {
                        result.emplace_back(parameter, constructs);
                    },
                    [&](const GenericArrayType& array) noexcept {
                        self(self, array.element, true, result);
                    },
                    [&](const GenericSliceType& slice) noexcept {
                        self(self, slice.element, true, result);
                    },
                    [&](const GenericOwnedSequenceType& sequence) noexcept {
                        self(self, sequence.element, true, result);
                    },
                    [&](const GenericPointerType& pointer) noexcept {
                        self(self, pointer.target, true, result);
                    },
                    [&](const GenericNominalApplication& application) noexcept {
                        for (const auto argument : application.arguments) {
                            self(self, argument, true, result);
                        }
                    },
                }
            );
        };
        const auto check = [&](auto&& self, GenericTypeID type) noexcept -> void {
            generic_type_copy(type).visit(
                Overloaded {
                    [](TypeID) static noexcept {},
                    [&](const GenericTypeParameter& parameter) noexcept {
                        if (parameter.definition != owner
                            || parameter.index >= contract.parameters.size()) {
                            diagnose(
                                DiagnosticCode::TypeGenericDefinition,
                                "generic field refers to a type parameter outside its definition",
                                contract.origin,
                                "type parameters are scoped to their declaring definition"
                            );
                        }
                    },
                    [&](const GenericArrayType& array) noexcept { self(self, array.element); },
                    [&](const GenericSliceType& slice) noexcept { self(self, slice.element); },
                    [&](const GenericOwnedSequenceType& sequence) noexcept {
                        self(self, sequence.element);
                    },
                    [&](const GenericPointerType& pointer) noexcept { self(self, pointer.target); },
                    [&](const GenericNominalApplication& application) noexcept {
                        const auto target = generic_declaration_copy(application.definition);
                        const auto target_count = definition_contract(target).parameters.size();
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
                            self(self, application.arguments[argument]);
                            if (argument >= target_count) {
                                continue;
                            }
                            auto origins = std::vector<std::pair<GenericTypeParameter, bool>>();
                            collect_origins(
                                collect_origins,
                                application.arguments[argument],
                                false,
                                origins
                            );
                            for (const auto& [parameter, constructs] : origins) {
                                if (parameter.definition == owner
                                    && parameter.index < contract.parameters.size()) {
                                    flow.push_back(
                                        {.source = offsets[index] + parameter.index,
                                         .target =
                                             offsets[application.definition.index()] + argument,
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
    using ApplicationKey = std::tuple<GenericDeclarationID, std::vector<GenericTypeID>, bool>;
    auto finished = std::set<ApplicationKey>();
    auto active = std::set<ApplicationKey>();
    auto work = 0uz;
    auto depth = 0uz;
    const auto visit_storage = [&](auto&& self,
                                   GenericTypeID type,
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
        const auto active_depth = StorageDepth {depth};
        generic_type_copy(type).visit(
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
                [&](const GenericOwnedSequenceType& sequence) noexcept {
                    // Dynamic owning storage breaks layout recursion. Its element
                    // value contract is checked after source heads are complete.
                    self(self, sequence.element, false, origin);
                },
                [&](const GenericNominalApplication& application) noexcept {
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
                    const auto definition = generic_declaration_copy(application.definition);
                    for (const auto [field, field_origin] : definition_fields(definition)) {
                        self(
                            self,
                            substitute_generic_type(
                                field,
                                application.definition,
                                application.arguments
                            ),
                            owns_storage,
                            field_origin
                        );
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
        auto arguments = std::vector<GenericTypeID>();
        for (auto parameter = 0uz; parameter < contract.parameters.size(); ++parameter) {
            arguments.push_back(intern_generic_type(
                GenericTypeParameter {owner, static_cast<std::uint32_t>(parameter)}
            ));
        }
        const auto application =
            intern_generic_type(GenericNominalApplication {owner, std::move(arguments)});
        visit_storage(visit_storage, application, true, contract.origin);
    }
    if (failure) {
        return std::unexpected(*failure);
    }
    storage.checked_generic_definitions.insert(selected.begin(), selected.end());
    return {};
}
