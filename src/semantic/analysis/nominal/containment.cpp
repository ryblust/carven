module carven:semantic.analysis.nominal.containment.impl;

import :diagnostics.builder;
import :semantic.analysis.nominal.containment;
import :semantic.hir.decl;
import :semantic.hir.type;
import :support.graph;
import :support.visit;
import std;

namespace {

struct NominalContainmentEdge final {
    std::size_t dependency;
};

struct NominalContainmentGraph final {
    std::vector<HIRNominalDeclRef> declarations;
    std::vector<std::vector<NominalContainmentEdge>> dependencies;
};

auto declaration_index(SemanticDraftView construction, HIRNominalDeclRef declaration) noexcept
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

auto nominal_declaration(SemanticDraftView construction, HIRTypeID type) noexcept
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

auto append_dependency(
    SemanticDraftView construction,
    NominalContainmentGraph& graph,
    std::size_t owner,
    HIRTypeID type
) noexcept -> void {
    const auto declaration = nominal_declaration(construction, type);
    if (!declaration.has_value()) {
        return;
    }
    const auto dependency = declaration_index(construction, *declaration);
    auto& dependencies = graph.dependencies[owner];
    if (!std::ranges::contains(dependencies, dependency, &NominalContainmentEdge::dependency)) {
        dependencies.push_back({.dependency = dependency});
    }
}

auto build_containment_graph(SemanticDraftView construction) noexcept -> NominalContainmentGraph {
    auto graph = NominalContainmentGraph();
    graph.declarations.reserve(
        construction.structures().size() + construction.enumerations().size()
    );
    for (auto index = 0uz; index < construction.structures().size(); ++index) {
        graph.declarations.emplace_back(StructID::from_index(static_cast<std::uint32_t>(index)));
    }
    for (auto index = 0uz; index < construction.enumerations().size(); ++index) {
        graph.declarations.emplace_back(EnumID::from_index(static_cast<std::uint32_t>(index)));
    }
    graph.dependencies.resize(graph.declarations.size());
    for (auto owner = 0uz; owner < graph.declarations.size(); ++owner) {
        std::visit(
            Overloaded {
                [&](StructID id) noexcept {
                    for (const auto& field : construction.structure(id).fields) {
                        append_dependency(construction, graph, owner, field.type);
                    }
                },
                [&](EnumID id) noexcept {
                    const auto& enumeration = construction.enumeration(id);
                    for (const auto case_id : enumeration.cases) {
                        for (const auto payload : construction.enum_case(case_id).payload_types) {
                            append_dependency(construction, graph, owner, payload);
                        }
                    }
                },
            },
            graph.declarations[owner]
        );
    }
    return graph;
}

auto declaration_origin(SemanticDraftView construction, HIRNominalDeclRef declaration) noexcept
    -> ProgramOriginID {
    return std::visit(
        Overloaded {
            [&](StructID id) noexcept { return construction.structure(id).origin; },
            [&](EnumID id) noexcept { return construction.enumeration(id).origin; },
        },
        declaration
    );
}

auto components(const NominalContainmentGraph& graph) noexcept
    -> std::vector<std::vector<std::uint32_t>> {
    auto adjacency = std::vector<std::vector<std::uint32_t>>(graph.declarations.size());
    for (auto owner = 0uz; owner < graph.dependencies.size(); ++owner) {
        for (const auto edge : graph.dependencies[owner]) {
            adjacency[owner].push_back(static_cast<std::uint32_t>(edge.dependency));
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
               component.front(),
               &NominalContainmentEdge::dependency
        );
}

auto published_dependencies(const NominalContainmentGraph& graph) noexcept
    -> std::vector<std::vector<HIRNominalDeclRef>> {
    auto result = std::vector<std::vector<HIRNominalDeclRef>>();
    result.reserve(graph.dependencies.size());
    for (const auto& edges : graph.dependencies) {
        auto dependencies = std::vector<HIRNominalDeclRef>();
        dependencies.reserve(edges.size());
        for (const auto edge : edges) {
            dependencies.push_back(graph.declarations[edge.dependency]);
        }
        result.push_back(std::move(dependencies));
    }
    return result;
}

} // namespace

auto analyze_nominal_containment(SemanticDraft& construction, DiagnosticSink& diagnostics) noexcept
    -> void {
    const auto graph = build_containment_graph(construction);
    auto found_cycle = false;
    for (const auto& component : components(graph)) {
        if (!cyclic(graph, component)) {
            continue;
        }
        found_cycle = true;
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::TypeRecursiveStorage,
            "nominal declarations form recursive by-value storage"
        );
        diagnostic.primary(
            construction.provenance().source_span(
                declaration_origin(construction, graph.declarations[component.front()])
            ),
            "cycle begins here"
        );
        for (auto offset = 1uz; offset < component.size(); ++offset) {
            diagnostic.related(
                construction.provenance().source_span(
                    declaration_origin(construction, graph.declarations[component[offset]])
                ),
                "declaration in the same storage cycle"
            );
        }
        diagnostics.emit(diagnostic.build());
    }
    if (!found_cycle) {
        construction.publish_nominal_containment({
            .direct_dependencies = published_dependencies(graph),
        });
    }
}
