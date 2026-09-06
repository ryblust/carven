module carven:semantic.analysis.nominal.containment.impl;

import :diagnostics.builder;
import :semantic.analysis.nominal.containment;
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
    return std::visit(
        Overloaded {
            [](StructID id) static noexcept -> std::size_t { return id.index(); },
            [&](EnumID id) noexcept -> std::size_t { return struct_count + id.index(); },
        },
        declaration
    );
}

auto nominal_declaration(ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<NominalDeclarationRef> {
    if (const auto* term = std::get_if<TypeTermID>(&type)) {
        const auto construction = draft.construction_type_copy(*term);
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
            return nominal_declaration(draft, array->element);
        }
        return std::nullopt;
    }
    return std::visit(
        Overloaded {
            [](const StructTypeValue& value) noexcept -> std::optional<NominalDeclarationRef> {
                return NominalDeclarationRef {value.structure};
            },
            [](const EnumTypeValue& value) noexcept -> std::optional<NominalDeclarationRef> {
                return NominalDeclarationRef {value.enumeration};
            },
            [&](const ArrayTypeValue& array) noexcept {
                return nominal_declaration(draft, ConstructionTypeRef {array.element});
            },
            []<typename Value>(const Value&) static noexcept
                -> std::optional<NominalDeclarationRef> {
                static_assert(
                    std::same_as<Value, BuiltinTypeValue>
                        || std::same_as<Value, FunctionTypeValue>
                        || std::same_as<Value, ClosureTypeValue>
                        || std::same_as<Value, CallableViewTypeValue>
                        || std::same_as<Value, CppTypeValue>,
                    "unhandled non-containing canonical type"
                );
                return std::nullopt;
            },
        },
        draft.type_copy(std::get<TypeID>(type)).value
    );
}

auto append_dependency(
    ProgramDraft& draft,
    NominalContainmentGraph& graph,
    std::size_t struct_count,
    std::size_t owner,
    ConstructionTypeRef type
) noexcept -> void {
    const auto declaration = nominal_declaration(draft, type);
    if (!declaration.has_value()) {
        return;
    }
    const auto dependency = declaration_index(struct_count, *declaration);
    if (dependency >= graph.declarations.size()) {
        invariant_violation("nominal containment references an unknown declaration");
    }
    auto& dependencies = graph.dependencies[owner];
    if (!std::ranges::contains(dependencies, dependency)) {
        dependencies.push_back(dependency);
    }
}

auto build_containment_graph(ProgramDraft& draft) noexcept -> NominalContainmentGraph {
    auto graph = NominalContainmentGraph();
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
        std::visit(
            Overloaded {
                [&](StructID id) noexcept {
                    for (const auto& field :
                         draft.construction_struct_declaration_copy(id).fields) {
                        append_dependency(draft, graph, structures.size(), owner, field.type);
                    }
                },
                [&](EnumID id) noexcept {
                    const auto enumeration = draft.construction_enum_declaration_copy(id);
                    for (const auto case_id : enumeration.cases) {
                        const auto enum_case =
                            draft.construction_enum_case_declaration_copy(case_id);
                        for (const auto payload : enum_case.payload_types) {
                            append_dependency(draft, graph, structures.size(), owner, payload);
                        }
                    }
                },
            },
            graph.declarations[owner]
        );
    }
    return graph;
}

auto declaration_origin(ProgramDraft& draft, NominalDeclarationRef declaration) noexcept
    -> ProgramOriginID {
    return std::visit(
        Overloaded {
            [&](StructID id) noexcept {
                return draft.construction_struct_declaration_copy(id).origin;
            },
            [&](EnumID id) noexcept { return draft.construction_enum_declaration_copy(id).origin; },
        },
        declaration
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
