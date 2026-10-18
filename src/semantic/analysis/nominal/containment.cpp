module carven:semantic.analysis.nominal.containment.impl;

import :diagnostics.builder;
import :semantic.analysis.nominal.containment;
import :semantic.analysis.program;
import :semantic.semir.decl;
import :semantic.semir.type;
import :support.graph;
import :support.invariant;
import :support.visit;
import std;

namespace {

struct NominalContainmentGraph final {
    std::vector<NominalDeclarationRef> declarations;
    std::vector<std::vector<std::size_t>> dependencies;
};

auto declaration_index(std::size_t struct_count, NominalDeclarationRef declaration) noexcept
    -> std::size_t {
    return declaration.visit(
        Overloaded {
            [](StructID id) static noexcept -> std::size_t { return id.index(); },
            [&](EnumID id) noexcept -> std::size_t { return struct_count + id.index(); },
        }
    );
}

auto append_dependency(
    const ProgramDraft& draft,
    NominalContainmentGraph& graph,
    std::size_t struct_count,
    std::size_t owner,
    ConstructionTypeRef type
) noexcept -> void {
    auto pending = std::vector<ConstructionTypeRef> {type};
    auto visited = std::set<ConstructionTypeRef>();
    const auto append_nominal = [&](NominalDeclarationRef declaration) noexcept {
        const auto dependency = declaration_index(struct_count, declaration);
        if (dependency >= graph.declarations.size()) {
            invariant_violation("nominal containment references an unknown declaration");
        }
        auto& dependencies = graph.dependencies[owner];
        if (!std::ranges::contains(dependencies, dependency)) {
            dependencies.push_back(dependency);
        }
    };
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        if (const auto* term = std::get_if<TypeTermID>(&current)) {
            const auto construction = draft.construction_type_copy(*term);
            if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
                pending.push_back(array->element);
            } else if (const auto* tuple =
                           std::get_if<ConstructionTupleTypeValue>(&construction.value)) {
                pending.insert(pending.end(), tuple->elements.begin(), tuple->elements.end());
            }
            continue;
        }
        draft.type_copy(std::get<TypeID>(current))
            .value.visit(
                Overloaded {
                    [&](const StructTypeValue& value) noexcept {
                        append_nominal(NominalDeclarationRef {value.structure});
                    },
                    [&](const EnumTypeValue& value) noexcept {
                        append_nominal(NominalDeclarationRef {value.enumeration});
                    },
                    [&](const ArrayTypeValue& value) noexcept { pending.push_back(value.element); },
                    [&](const TupleTypeValue& value) noexcept {
                        for (const auto element : value.elements) {
                            pending.emplace_back(element);
                        }
                    },
                    []<typename Value>(const Value&) static noexcept {
                        static_assert(
                            std::same_as<Value, BuiltinTypeValue>
                                || std::same_as<Value, FunctionTypeValue>
                                || std::same_as<Value, ClosureTypeValue>
                                || std::same_as<Value, CallableViewTypeValue>
                                || std::same_as<Value, OperationTypeValue>
                                || std::same_as<Value, CppTypeValue>
                                || std::same_as<Value, PointerTypeValue>
                                || std::same_as<Value, VectorTypeValue>
                                || std::same_as<Value, SliceTypeValue>
                                || std::same_as<Value, RangeTypeValue>,
                            "unhandled non-containing canonical type"
                        );
                    },
                }
            );
    }
}

auto build_containment_graph(ProgramDraft& draft) noexcept -> NominalContainmentGraph {
    auto graph = NominalContainmentGraph {.declarations = {}, .dependencies = {}};
    const auto structures = draft.struct_declaration_ids();
    const auto enumerations = draft.enum_declaration_ids();
    graph.declarations.reserve(structures.size() + enumerations.size());
    for (const auto structure : structures) {
        graph.declarations.emplace_back(structure);
    }
    for (const auto enumeration : enumerations) {
        graph.declarations.emplace_back(enumeration);
    }
    graph.dependencies.resize(graph.declarations.size());
    for (auto owner = 0uz; owner < graph.declarations.size(); ++owner) {
        graph.declarations[owner].visit(
            Overloaded {
                [&](StructID id) noexcept {
                    for (const auto& field :
                         draft.construction_struct_declaration_copy(id).fields) {
                        append_dependency(draft, graph, structures.size(), owner, field.type);
                    }
                },
                [&](EnumID id) noexcept {
                    const auto enumeration = draft.enum_declaration_copy(id);
                    for (const auto case_id : enumeration.cases) {
                        const auto enum_case =
                            draft.construction_enum_case_declaration_copy(case_id);
                        for (const auto payload : enum_case.payload_types) {
                            append_dependency(draft, graph, structures.size(), owner, payload);
                        }
                    }
                },
            }
        );
    }
    return graph;
}

auto declaration_origin(ProgramDraft& draft, NominalDeclarationRef declaration) noexcept
    -> ProgramOriginID {
    return declaration.visit(
        Overloaded {
            [&](StructID id) noexcept {
                if (const auto instance = draft.generic_nominal_instance_copy(id)) {
                    return instance->origin;
                }
                return draft.construction_struct_declaration_copy(id).origin;
            },
            [&](EnumID id) noexcept {
                if (const auto instance = draft.generic_nominal_instance_copy(id)) {
                    return instance->origin;
                }
                return draft.enum_declaration_copy(id).origin;
            },
        }
    );
}

auto components(const NominalContainmentGraph& graph) noexcept
    -> std::vector<std::vector<std::uint32_t>> {
    auto adjacency = std::vector<std::vector<std::uint32_t>>(graph.declarations.size());
    for (auto owner = 0uz; owner < graph.dependencies.size(); ++owner) {
        for (const auto dependency : graph.dependencies[owner]) {
            adjacency[owner].push_back(static_cast<std::uint32_t>(dependency));
        }
    }
    return strongly_connected_components(std::move(adjacency)).dependency_first;
}

auto cyclic(
    const NominalContainmentGraph& graph,
    const std::vector<std::uint32_t>& component
) noexcept -> bool {
    return component.size() > 1
        || std::ranges::contains(
               graph.dependencies[component.front()],
               static_cast<std::size_t>(component.front())
        );
}

} // namespace

auto analyze_nominal_containment(ProgramDraft& draft) noexcept -> AnalysisResult<void> {
    const auto graph = build_containment_graph(draft);
    auto failure = std::optional<AnalysisFailure>();
    for (const auto& component : components(graph)) {
        if (!cyclic(graph, component)) {
            continue;
        }
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::TypeRecursiveStorage,
            "nominal declarations form recursive by-value storage"
        );
        diagnostic.primary(
            draft.source_span(declaration_origin(draft, graph.declarations[component.front()])),
            "cycle begins here"
        );
        for (auto offset = 1uz; offset < component.size(); ++offset) {
            diagnostic.related(
                draft.source_span(declaration_origin(draft, graph.declarations[component[offset]])),
                "declaration in the same storage cycle"
            );
        }
        failure = draft.diagnostics().error(diagnostic.build());
    }
    return failure.has_value() ? AnalysisResult<void>(std::unexpected(*failure))
                               : AnalysisResult<void>();
}
