module carven:semantic.analysis.storage_order.impl;

import :diagnostics.builder;
import :semantic.analysis.storage_order;
import :semantic.hir.decl;
import :semantic.hir.type;
import :support.graph;
import :support.visit;
import std;

namespace {

struct NominalStorageEdge final {
    std::size_t target;
};

struct NominalStorageGraph final {
    std::vector<HIRNominalDeclRef> nodes;
    std::vector<std::vector<NominalStorageEdge>> edges;
};

auto node_index(const SemanticConstruction& construction, HIRNominalDeclRef declaration) noexcept
    -> std::size_t {
    return std::visit(
        Overloaded {
            [](StructID id) static noexcept -> std::size_t { return id.index(); },
            [&](EnumID id) noexcept -> std::size_t {
                return construction.structures().size() + id.index();
            },
        },
        declaration
    );
}

auto nominal_declaration(const SemanticConstruction& construction, HIRTypeID type) noexcept
    -> std::optional<HIRNominalDeclRef> {
    return std::visit(
        Overloaded {
            [](const HIRStructTypeValue& value) static noexcept
                -> std::optional<HIRNominalDeclRef> { return HIRNominalDeclRef {value.structure}; },
            [](const HIREnumTypeValue& value) static noexcept -> std::optional<HIRNominalDeclRef> {
                return HIRNominalDeclRef {value.enumeration};
            },
            [&](const HIRArrayTypeValue& array) noexcept {
                return nominal_declaration(construction, array.element_type_id);
            },
            [](const auto&) static noexcept -> std::optional<HIRNominalDeclRef> {
                return std::nullopt;
            },
        },
        construction.type(type).value
    );
}

auto append_edge(
    const SemanticConstruction& construction,
    NominalStorageGraph& graph,
    std::size_t source,
    HIRTypeID type
) noexcept -> void {
    const auto target = nominal_declaration(construction, type);
    if (!target.has_value()) {
        return;
    }
    const auto target_index = node_index(construction, *target);
    if (!std::ranges::contains(graph.edges[source], target_index, &NominalStorageEdge::target)) {
        graph.edges[source].push_back({.target = target_index});
    }
}

auto build_storage_graph(const SemanticConstruction& construction) noexcept -> NominalStorageGraph {
    auto graph = NominalStorageGraph();
    graph.nodes.reserve(construction.structures().size() + construction.enumerations().size());
    for (auto index = 0uz; index < construction.structures().size(); ++index) {
        graph.nodes.emplace_back(StructID::from_index(static_cast<std::uint32_t>(index)));
    }
    for (auto index = 0uz; index < construction.enumerations().size(); ++index) {
        graph.nodes.emplace_back(EnumID::from_index(static_cast<std::uint32_t>(index)));
    }
    graph.edges.resize(graph.nodes.size());
    for (auto source = 0uz; source < graph.nodes.size(); ++source) {
        std::visit(
            Overloaded {
                [&](StructID id) noexcept {
                    for (const auto& field : construction.structure(id).fields) {
                        append_edge(construction, graph, source, field.type);
                    }
                },
                [&](EnumID id) noexcept {
                    const auto& enumeration = construction.enumeration(id);
                    if (enumeration.underlying_type.has_value()) {
                        append_edge(construction, graph, source, *enumeration.underlying_type);
                    }
                    for (const auto case_id : enumeration.cases) {
                        const auto& enum_case = construction.enum_case(case_id);
                        for (const auto payload : enum_case.payload_types) {
                            append_edge(construction, graph, source, payload);
                        }
                    }
                },
            },
            graph.nodes[source]
        );
        std::ranges::sort(graph.edges[source], {}, &NominalStorageEdge::target);
    }
    return graph;
}

auto declaration_origin(
    const SemanticConstruction& construction,
    HIRNominalDeclRef declaration
) noexcept -> ProgramOriginID {
    return std::visit(
        Overloaded {
            [&](StructID id) noexcept { return construction.structure(id).origin; },
            [&](EnumID id) noexcept { return construction.enumeration(id).origin; },
        },
        declaration
    );
}

auto storage_components(const NominalStorageGraph& graph) noexcept
    -> std::vector<std::vector<std::uint32_t>> {
    auto adjacency = std::vector<std::vector<std::uint32_t>>(graph.nodes.size());
    for (auto source = 0uz; source < graph.edges.size(); ++source) {
        for (const auto edge : graph.edges[source]) {
            adjacency[source].push_back(static_cast<std::uint32_t>(edge.target));
        }
    }
    return strongly_connected_components(std::move(adjacency)).dependency_first;
}

auto is_cycle(
    const NominalStorageGraph& graph,
    const std::vector<std::uint32_t>& component
) noexcept -> bool {
    return component.size() > 1
        || std::ranges::contains(
               graph.edges[component.front()],
               component.front(),
               &NominalStorageEdge::target
        );
}

auto storage_order(const NominalStorageGraph& graph) noexcept -> std::vector<HIRNominalDeclRef> {
    auto visited = std::vector<bool>(graph.nodes.size());
    auto result = std::vector<HIRNominalDeclRef>();
    result.reserve(graph.nodes.size());
    const auto visit = [&](this const auto& self, std::size_t node) noexcept -> void {
        if (visited[node]) {
            return;
        }
        visited[node] = true;
        for (const auto edge : graph.edges[node]) {
            self(edge.target);
        }
        result.push_back(graph.nodes[node]);
    };
    for (auto node = 0uz; node < graph.nodes.size(); ++node) {
        visit(node);
    }
    return result;
}

auto direct_dependencies(const NominalStorageGraph& graph) noexcept
    -> std::vector<std::vector<HIRNominalDeclRef>> {
    auto result = std::vector<std::vector<HIRNominalDeclRef>>();
    result.reserve(graph.edges.size());
    for (const auto& edges : graph.edges) {
        auto dependencies = std::vector<HIRNominalDeclRef>();
        dependencies.reserve(edges.size());
        for (const auto edge : edges) {
            dependencies.push_back(graph.nodes[edge.target]);
        }
        result.push_back(std::move(dependencies));
    }
    return result;
}

} // namespace

auto diagnose_nominal_storage(
    SemanticConstruction& construction,
    DiagnosticSink& diagnostics
) noexcept -> void {
    const auto graph = build_storage_graph(construction);
    auto found_cycle = false;
    for (const auto& component : storage_components(graph)) {
        if (!is_cycle(graph, component)) {
            continue;
        }
        found_cycle = true;
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::TypeRecursiveStorage,
            "nominal declarations form recursive by-value storage"
        );
        diagnostic.primary(
            construction.provenance().source_span(
                declaration_origin(construction, graph.nodes[component.front()])
            ),
            "cycle begins here"
        );
        for (auto offset = 1uz; offset < component.size(); ++offset) {
            diagnostic.related(
                construction.provenance().source_span(
                    declaration_origin(construction, graph.nodes[component[offset]])
                ),
                "declaration in the same storage cycle"
            );
        }
        diagnostics.emit(diagnostic.build());
    }
    if (!found_cycle) {
        construction.publish_nominal_storage({
            .order = storage_order(graph),
            .direct_dependencies = direct_dependencies(graph),
        });
    }
}
